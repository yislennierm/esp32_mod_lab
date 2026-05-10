#!/usr/bin/env python3
"""Live browser viewer for fast LCD_CAM RAW8 streams with decode sliders."""

from __future__ import annotations

import argparse
import csv
import json
import mimetypes
from pathlib import Path
import signal
import threading
import time
from collections import Counter
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any
from urllib.parse import parse_qs, urlparse

from analyze_timing_edges import summarize_capture
from analyze_timing_relationships import analyze as analyze_timing_relationships
from export_pulseview import write_vcd
from gbc_probe import DEFAULT_BAUD, ProbeClient, autodetect_port
from render_dvp_raw import render_rgb565


class LiveLcdcamState:
    def __init__(
        self,
        port: str,
        baud: int,
        timeout: float,
        command: str,
        data_mode: str,
        crop_offset: int,
        crop_len: int,
        firmware_binary: bool,
        stream_batch_size: int,
        pclk_invert: bool,
        source_driver: bool,
        capture_timeout_ms: int,
    ) -> None:
        self._lock = threading.Lock()
        self._serial_lock = threading.Lock()
        self.port = port
        self.baud = baud
        self.timeout = timeout
        self._client = ProbeClient(port, baud, timeout)
        self._client.drain_startup()
        self.command = command
        self.data_mode = data_mode
        self.crop_offset = crop_offset
        self.crop_len = crop_len
        self.firmware_binary = firmware_binary
        self.stream_batch_size = stream_batch_size
        self.pclk_invert = pclk_invert
        self.source_driver = source_driver
        self.capture_timeout_ms = capture_timeout_ms
        self._capture_thread: threading.Thread | None = None
        self._capture_stop = threading.Event()
        self._latest_payload = b""
        self._latest_metadata: dict[str, Any] = {"ok": False, "error": "no frame captured yet"}
        self._latest_error = ""
        self._frame_count = 0
        self._last_capture_ms = 0
        self._fps_window_start = time.monotonic()
        self._fps_window_count = 0
        self._capture_fps = 0.0
        self._consecutive_errors = 0
        self._latest_frame_monotonic = 0.0
        self._max_live_frame_age_s = 1.2
        self._source_state = "starting"
        self._source_last_probe: dict[str, Any] = {}
        self._source_wait_since_monotonic = 0.0
        self._single_frame_burst_remaining = 0
        self._boot_thread: threading.Thread | None = None
        self._boot_stop = threading.Event()
        self._boot_status: dict[str, Any] = {
            "ok": True,
            "running": False,
            "state": "idle",
            "frames_captured": 0,
            "frame_count": 0,
            "output_dir": "",
            "error": "",
        }
        self._power_thread: threading.Thread | None = None
        self._power_stop = threading.Event()
        self._power_samples: list[dict[str, Any]] = []
        self._power_status: dict[str, Any] = {
            "ok": True,
            "running": False,
            "state": "idle",
            "sample_count": 0,
            "output_dir": "",
            "error": "",
        }

    def close(self) -> None:
        self.stop_power_monitor()
        self.stop_boot_capture()
        self.stop_continuous(safe_idle=False)
        self._client.close()

    def capture(self) -> dict[str, Any]:
        with self._serial_lock:
            response = self._client.command(self.command)
        return dict(response.data)

    def capture_payload(self) -> tuple[bytes, dict[str, Any]]:
        with self._serial_lock:
            if self.firmware_binary:
                response = self._client.command_binary(self.command)
                payload = response.payload
                metadata = dict(response.data)
                metadata["transport"] = "firmware_binary"
            else:
                response = self._client.command(self.command)
                payload, metadata = split_frame_payload(response.data, 0, 0)
        if self.crop_len > 0:
            end = min(len(payload), self.crop_offset + self.crop_len)
            payload = payload[self.crop_offset:end]
            metadata["host_crop_offset"] = self.crop_offset
            metadata["host_crop_len"] = self.crop_len
            metadata["host_cropped_size"] = len(payload)
        return payload, metadata

    def start_continuous(self) -> dict[str, Any]:
        if self._capture_thread is not None and self._capture_thread.is_alive():
            return self.status(True)
        self._capture_stop.clear()
        self._capture_thread = threading.Thread(target=self._capture_loop, name="lcdcam-capture", daemon=True)
        self._capture_thread.start()
        return self.status(True)

    def stop_continuous(self, safe_idle: bool = True, isolate: bool = False) -> dict[str, Any]:
        self._capture_stop.set()
        thread = self._capture_thread
        if thread is not None and thread.is_alive():
            thread.join(timeout=5.0)
        if safe_idle:
            try:
                if isolate:
                    self.electrical_isolate()
                else:
                    self.safe_idle()
            except Exception as exc:
                self._latest_error = str(exc)
        with self._lock:
            self._latest_payload = b""
            self._latest_metadata = {"ok": False, "error": "live capture stopped; no current frame"}
        return self.status(False)

    def latest_payload(self) -> tuple[bytes, dict[str, Any]]:
        with self._lock:
            payload = bytes(self._latest_payload)
            metadata = dict(self._latest_metadata)
            metadata["source_state"] = self._source_state
            metadata["server_frame_count"] = self._frame_count
            metadata["server_last_capture_ms"] = self._last_capture_ms
            metadata["server_capture_fps"] = round(self._capture_fps, 2)
            metadata["server_running"] = self._capture_thread is not None and self._capture_thread.is_alive()
            frame_age_s = time.monotonic() - self._latest_frame_monotonic if self._latest_frame_monotonic > 0 else None
            metadata["server_frame_age_ms"] = None if frame_age_s is None else int(frame_age_s * 1000)
            metadata["server_frame_is_live"] = bool(metadata["server_running"] and frame_age_s is not None and frame_age_s <= self._max_live_frame_age_s)
            if self._latest_error:
                metadata["server_error"] = self._latest_error
        if not payload:
            if metadata.get("source_state") in {"no_signal", "clock_detected"}:
                raise ValueError(metadata.get("error") or "source not present; waiting for DCLK")
            raise ValueError(metadata.get("error") or metadata.get("server_error") or "no frame captured yet")
        if not metadata.get("server_frame_is_live"):
            raise ValueError("latest frame is stale; no current live frame")
        return payload, metadata

    def status(self, running: bool | None = None) -> dict[str, Any]:
        with self._lock:
            is_running = self._capture_thread is not None and self._capture_thread.is_alive()
            source_waiting = is_running and self._source_state in {"no_signal", "clock_detected"}
            return {
                "ok": source_waiting or not self._latest_error,
                "running": is_running if running is None else running,
                "source_state": self._source_state,
                "source_wait_ms": 0 if self._source_wait_since_monotonic <= 0 else int((time.monotonic() - self._source_wait_since_monotonic) * 1000),
                "source_last_probe": dict(self._source_last_probe),
                "server_frame_count": self._frame_count,
                "server_last_capture_ms": self._last_capture_ms,
                "server_capture_fps": round(self._capture_fps, 2),
                "server_frame_age_ms": None if self._latest_frame_monotonic <= 0 else int((time.monotonic() - self._latest_frame_monotonic) * 1000),
                "consecutive_errors": self._consecutive_errors,
                "error": self._latest_error,
            }

    def _capture_loop(self) -> None:
        while not self._capture_stop.is_set():
            if self._source_state == "no_signal" and not self._wait_for_source():
                continue
            if self.stream_batch_size > 1 and self.firmware_binary and self._single_frame_burst_remaining <= 0:
                self._capture_batch()
            else:
                start = time.monotonic()
                try:
                    payload, metadata = self.capture_payload()
                    if self._single_frame_burst_remaining > 0:
                        metadata["server_reacquire_single_frame"] = True
                        self._single_frame_burst_remaining -= 1
                    self._store_frame(payload, metadata, int((time.monotonic() - start) * 1000))
                except Exception as exc:
                    self._handle_capture_error(exc)
                    time.sleep(0.05)

    def _capture_batch(self) -> None:
        start = time.monotonic()
        try:
            with self._serial_lock:
                emit_len = self.crop_len if self.crop_len > 0 else 0
                if self.source_driver:
                    stream_command = (
                        f"GBC_SOURCE_STREAM_BIN {self.stream_batch_size} {self.capture_timeout_ms} "
                        f"{self.data_mode} {emit_len} {int(self.pclk_invert)}"
                    )
                else:
                    stream_command = (
                        f"LCDCAM_RAW_STREAM_BIN {self.stream_batch_size} "
                        f"{int(self.pclk_invert)} {self.data_mode} {emit_len}"
                    )
                responses = self._client.command_binary_sequence(
                    stream_command,
                    self.stream_batch_size,
                )
                for response in responses:
                    payload = response.payload
                    metadata = dict(response.data)
                    metadata["transport"] = "firmware_binary_batch"
                    if self.crop_len > 0:
                        end = min(len(payload), self.crop_offset + self.crop_len)
                        payload = payload[self.crop_offset:end]
                        metadata["host_crop_offset"] = self.crop_offset
                        metadata["host_crop_len"] = self.crop_len
                        metadata["host_cropped_size"] = len(payload)
                    elapsed_ms = int((time.monotonic() - start) * 1000)
                    self._store_frame_locked(payload, metadata, elapsed_ms)
                    start = time.monotonic()
                    if self._capture_stop.is_set():
                        break
        except Exception as exc:
            self._handle_capture_error(exc)
            time.sleep(0.05)

    def _store_frame(self, payload: bytes, metadata: dict[str, Any], elapsed_ms: int) -> None:
        with self._lock:
            self._store_frame_locked(payload, metadata, elapsed_ms)

    def _store_frame_locked(self, payload: bytes, metadata: dict[str, Any], elapsed_ms: int) -> None:
        metadata = dict(metadata)
        metadata["source_state"] = "live"
        self._latest_payload = payload
        self._latest_metadata = metadata
        self._latest_error = ""
        self._consecutive_errors = 0
        self._source_state = "live"
        self._source_wait_since_monotonic = 0.0
        self._frame_count += 1
        self._last_capture_ms = elapsed_ms
        self._latest_frame_monotonic = time.monotonic()
        self._fps_window_count += 1
        now = time.monotonic()
        window_elapsed = now - self._fps_window_start
        if window_elapsed >= 1.0:
            self._capture_fps = self._fps_window_count / window_elapsed
            self._fps_window_count = 0
            self._fps_window_start = now

    def _handle_capture_error(self, exc: Exception) -> None:
        message = str(exc)
        with self._lock:
            self._latest_error = message
            self._latest_payload = b""
            self._latest_metadata = {"ok": False, "error": message}
            self._consecutive_errors += 1
        # A missing source makes LCD_CAM waits expensive and may disturb power-up.
        # After the first frame failure, fall back to a passive DCLK probe loop.
        self._enter_source_wait(message)

    def _enter_source_wait(self, reason: str) -> None:
        with self._lock:
            if self._source_state != "no_signal":
                self._source_wait_since_monotonic = time.monotonic()
            self._source_state = "no_signal"
            self._single_frame_burst_remaining = 0
            self._latest_payload = b""
            self._latest_metadata = {"ok": False, "source_state": "no_signal", "error": "source not present; waiting for DCLK"}
            self._latest_error = reason
        try:
            self.safe_idle()
        except Exception as exc:
            with self._lock:
                self._latest_error = f"safe idle while waiting for source failed: {exc}"

    def _wait_for_source(self) -> bool:
        while not self._capture_stop.is_set() and self._source_state == "no_signal":
            try:
                dclk = self.count_gpio_edges(22, 5)
                dclk_edges = int(dclk.get("rising_edges", 0) or 0)
                probe = {
                    "dclk_edges_5ms": dclk_edges,
                    "checked_ms": int(time.monotonic() * 1000),
                }
                with self._lock:
                    self._source_last_probe = probe
                    self._latest_metadata = {"ok": False, "source_state": "no_signal", "error": "source not present; waiting for DCLK", **probe}
                if dclk_edges > 20:
                    with self._lock:
                        self._source_state = "clock_detected"
                        self._latest_error = ""
                        self._single_frame_burst_remaining = 12
                    return True
            except Exception as exc:
                with self._lock:
                    self._latest_error = f"source probe failed: {exc}"
                    self._consecutive_errors += 1
                if self._consecutive_errors >= 3:
                    self._recover_serial_locked_stopless()
            time.sleep(0.01)
        return False

    def safe_idle(self) -> dict[str, Any]:
        with self._serial_lock:
            response = self._client.command("SAFE_IDLE")
        return dict(response.data)

    def electrical_isolate(self) -> dict[str, Any]:
        with self._serial_lock:
            response = self._client.command("ELECTRICAL_ISOLATE")
        return dict(response.data)

    def probe_command(self, command: str) -> dict[str, Any]:
        with self._serial_lock:
            response = self._client.command(command)
        return dict(response.data)

    def read_gpio(self, gpio: int) -> dict[str, Any]:
        return self.probe_command(f"READ_GPIO {gpio}")

    def count_gpio_edges(self, gpio: int, duration_ms: int) -> dict[str, Any]:
        return self.probe_command(f"COUNT_GPIO_EDGES {gpio} {duration_ms}")

    def measure_clock(self, gpio: int, duration_ms: int) -> dict[str, Any]:
        return self.probe_command(f"MEASURE_DCLK {gpio} {duration_ms}")

    def capture_timing_edges(self, duration_ms: int) -> dict[str, Any]:
        return self.probe_command(f"CAPTURE_TIMING_EDGES {duration_ms}")

    def capture_line_clocks(self, marker: str, edge: str, line_count: int, timeout_ms: int) -> dict[str, Any]:
        return self.probe_command(f"CAPTURE_LINE_CLOCKS {marker} {edge} {line_count} {timeout_ms}")

    def boot_status(self) -> dict[str, Any]:
        with self._lock:
            status = dict(self._boot_status)
            status["running"] = self._boot_thread is not None and self._boot_thread.is_alive()
            return status

    def start_boot_capture(self, frame_count: int, wait_ms: int, probe_ms: int) -> dict[str, Any]:
        if self._boot_thread is not None and self._boot_thread.is_alive():
            return self.boot_status()
        self._boot_stop.clear()
        with self._lock:
            self._boot_status = {
                "ok": True,
                "running": True,
                "state": "starting",
                "frames_captured": 0,
                "frame_count": frame_count,
                "output_dir": "",
                "error": "",
                "data_mode": self.data_mode,
            }
        self._boot_thread = threading.Thread(
            target=self._boot_capture_loop,
            args=(frame_count, wait_ms, probe_ms),
            name="boot-capture",
            daemon=True,
        )
        self._boot_thread.start()
        return self.boot_status()

    def stop_boot_capture(self) -> dict[str, Any]:
        self._boot_stop.set()
        thread = self._boot_thread
        if thread is not None and thread.is_alive():
            thread.join(timeout=3.0)
        with self._lock:
            self._boot_status["running"] = False
            if self._boot_status.get("state") not in {"done", "error"}:
                self._boot_status["state"] = "stopped"
        return self.boot_status()

    def _set_boot_status(self, **updates: Any) -> None:
        with self._lock:
            self._boot_status.update(updates)

    def _boot_capture_loop(self, frame_count: int, wait_ms: int, probe_ms: int) -> None:
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        output_dir = Path("captures/experiments") / f"{stamp}-boot_capture_{self.data_mode.lower()}"
        frames_dir = output_dir / "frames"
        raw_dir = output_dir / "raw"
        frames_dir.mkdir(parents=True, exist_ok=True)
        raw_dir.mkdir(parents=True, exist_ok=True)
        metadata: list[dict[str, Any]] = []
        raw_paths: list[Path] = []
        try:
            self._set_boot_status(state="safe_idle", output_dir=str(output_dir))
            self.stop_continuous(safe_idle=True)

            deadline = time.monotonic() + wait_ms / 1000.0
            self._set_boot_status(state="waiting_for_source")
            locked = False
            while time.monotonic() < deadline and not self._boot_stop.is_set():
                dclk = self.count_gpio_edges(22, probe_ms)
                sps = self.count_gpio_edges(33, probe_ms)
                dclk_edges = int(dclk.get("rising_edges", 0) or 0)
                sps_edges = int(sps.get("rising_edges", 0) or 0)
                self._set_boot_status(last_dclk_edges=dclk_edges, last_sps_edges=sps_edges)
                if dclk_edges > 100 and sps_edges > 0:
                    locked = True
                    break
            if self._boot_stop.is_set():
                self._set_boot_status(state="stopped", running=False)
                return
            if not locked:
                self._set_boot_status(ok=False, state="error", running=False, error="source_lock_timeout")
                return

            self._set_boot_status(state="recording", frames_captured=0)
            for index in range(frame_count):
                if self._boot_stop.is_set():
                    break
                start = time.monotonic()
                payload, frame_meta = self.capture_payload()
                elapsed_ms = int((time.monotonic() - start) * 1000)
                raw_path = raw_dir / f"frame_{index:04d}.bin"
                raw_path.write_bytes(payload)
                frame_meta = dict(frame_meta)
                frame_meta["index"] = index
                frame_meta["host_elapsed_ms"] = elapsed_ms
                frame_meta["raw"] = str(raw_path)
                metadata.append(frame_meta)
                raw_paths.append(raw_path)
                self._store_frame(payload, frame_meta, elapsed_ms)
                self._set_boot_status(frames_captured=index + 1)

            self._set_boot_status(state="rendering")
            png_count = 0
            if self.data_mode == "RGB565":
                for index, raw_path in enumerate(raw_paths):
                    payload = raw_path.read_bytes()
                    visible = crop_rgb16_visible(payload, 161, 145, 160, 144)
                    png_path = frames_dir / f"frame_{index:04d}.png"
                    render_rgb565(visible, 160, 144, png_path, invert=False)
                    metadata[index]["png"] = str(png_path)
                    png_count += 1

            summary = {
                "ok": True,
                "created_utc": stamp,
                "data_mode": self.data_mode,
                "requested_frames": frame_count,
                "captured_frames": len(metadata),
                "png_frames": png_count,
                "output_dir": str(output_dir),
                "frames": metadata,
            }
            (output_dir / "summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True), encoding="utf-8")
            self._set_boot_status(state="done", running=False, frames_captured=len(metadata), summary=str(output_dir / "summary.json"))
        except Exception as exc:
            self._set_boot_status(ok=False, state="error", running=False, error=str(exc))

    def power_monitor_status(self) -> dict[str, Any]:
        with self._lock:
            status = dict(self._power_status)
            status["running"] = self._power_thread is not None and self._power_thread.is_alive()
            status["samples"] = list(self._power_samples[-120:])
            status["sample_count"] = len(self._power_samples)
            if self._power_samples:
                status["latest"] = dict(self._power_samples[-1])
            return status

    def start_power_monitor(self, duration_ms: int, window_ms: int) -> dict[str, Any]:
        if self._power_thread is not None and self._power_thread.is_alive():
            return self.power_monitor_status()
        self._power_stop.clear()
        with self._lock:
            self._power_samples = []
            self._power_status = {
                "ok": True,
                "running": True,
                "state": "starting",
                "sample_count": 0,
                "output_dir": "",
                "error": "",
                "duration_ms": duration_ms,
                "window_ms": window_ms,
            }
        self._power_thread = threading.Thread(
            target=self._power_monitor_loop,
            args=(duration_ms, window_ms),
            name="power-cycle-monitor",
            daemon=True,
        )
        self._power_thread.start()
        return self.power_monitor_status()

    def stop_power_monitor(self) -> dict[str, Any]:
        self._power_stop.set()
        thread = self._power_thread
        if thread is not None and thread.is_alive():
            thread.join(timeout=3.0)
        with self._lock:
            self._power_status["running"] = False
            if self._power_status.get("state") not in {"done", "error"}:
                self._power_status["state"] = "stopped"
        return self.power_monitor_status()

    def _set_power_status(self, **updates: Any) -> None:
        with self._lock:
            self._power_status.update(updates)

    def _power_monitor_loop(self, duration_ms: int, window_ms: int) -> None:
        signals = [
            ("DCLK", 22),
            ("SPS", 33),
            ("SPL", 19),
            ("LP", 21),
            ("PS", 20),
            ("CLS", 3),
        ]
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        output_dir = Path("captures/experiments") / f"{stamp}-power_cycle_monitor"
        output_dir.mkdir(parents=True, exist_ok=True)
        started = time.monotonic()
        try:
            self._set_power_status(state="safe_idle", output_dir=str(output_dir))
            self.stop_continuous(safe_idle=True)
            self._set_power_status(state="monitoring")
            while not self._power_stop.is_set():
                elapsed_ms = int((time.monotonic() - started) * 1000)
                if elapsed_ms >= duration_ms:
                    break
                edges: dict[str, int] = {}
                falling_edges: dict[str, int] = {}
                levels: dict[str, int | None] = {}
                errors: dict[str, str] = {}
                for name, gpio in signals:
                    if self._power_stop.is_set():
                        break
                    try:
                        edge_data = self.count_gpio_edges(gpio, window_ms)
                        edges[name] = int(edge_data.get("rising_edges", 0) or 0)
                        falling_edges[name] = int(edge_data.get("falling_edges", 0) or 0)
                    except Exception as exc:
                        errors[name] = str(exc)
                        edges[name] = 0
                        falling_edges[name] = 0
                    try:
                        level_data = self.read_gpio(gpio)
                        levels[name] = int(level_data.get("level")) if level_data.get("level") is not None else None
                    except Exception as exc:
                        errors[f"{name}_level"] = str(exc)
                        levels[name] = None
                line_edges = edges.get("SPL", 0) + falling_edges.get("SPL", 0) + edges.get("LP", 0) + falling_edges.get("LP", 0)
                sample = {
                    "t_ms": elapsed_ms,
                    "window_ms": window_ms,
                    "state": classify_power_sample(edges, falling_edges),
                    "edges": edges,
                    "falling_edges": falling_edges,
                    "levels": levels,
                    "line_edges": line_edges,
                    "errors": errors,
                }
                with self._lock:
                    self._power_samples.append(sample)
                    self._power_status.update({
                        "state": sample["state"],
                        "sample_count": len(self._power_samples),
                        "latest": sample,
                    })
            self._write_power_monitor_artifacts(output_dir, stamp, duration_ms, window_ms, signals)
            if self._power_stop.is_set():
                self._set_power_status(state="stopped", running=False)
            else:
                self._set_power_status(state="done", running=False)
        except Exception as exc:
            self._set_power_status(ok=False, state="error", running=False, error=str(exc))

    def _write_power_monitor_artifacts(
        self,
        output_dir: Path,
        stamp: str,
        duration_ms: int,
        window_ms: int,
        signals: list[tuple[str, int]],
    ) -> None:
        with self._lock:
            samples = list(self._power_samples)
        raw = {
            "ok": True,
            "created_utc": stamp,
            "duration_ms": duration_ms,
            "window_ms": window_ms,
            "signals": [{"name": name, "gpio": gpio} for name, gpio in signals],
            "samples": samples,
        }
        raw_path = output_dir / "raw.json"
        raw_path.write_text(json.dumps(raw, indent=2, sort_keys=True), encoding="utf-8")
        csv_path = output_dir / "samples.csv"
        fieldnames = [
            "t_ms",
            "window_ms",
            "state",
            "dclk_rise",
            "sps_rise",
            "spl_rise",
            "lp_rise",
            "ps_rise",
            "cls_rise",
            "dclk_level",
            "sps_level",
            "spl_level",
            "lp_level",
            "ps_level",
            "cls_level",
            "line_edges",
            "error_count",
        ]
        with csv_path.open("w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=fieldnames)
            writer.writeheader()
            for sample in samples:
                edges = sample.get("edges", {})
                levels = sample.get("levels", {})
                writer.writerow({
                    "t_ms": sample.get("t_ms"),
                    "window_ms": sample.get("window_ms"),
                    "state": sample.get("state"),
                    "dclk_rise": edges.get("DCLK"),
                    "sps_rise": edges.get("SPS"),
                    "spl_rise": edges.get("SPL"),
                    "lp_rise": edges.get("LP"),
                    "ps_rise": edges.get("PS"),
                    "cls_rise": edges.get("CLS"),
                    "dclk_level": levels.get("DCLK"),
                    "sps_level": levels.get("SPS"),
                    "spl_level": levels.get("SPL"),
                    "lp_level": levels.get("LP"),
                    "ps_level": levels.get("PS"),
                    "cls_level": levels.get("CLS"),
                    "line_edges": sample.get("line_edges"),
                    "error_count": len(sample.get("errors", {})),
                })
        states = Counter(str(sample.get("state")) for sample in samples)
        summary = {
            "ok": True,
            "created_utc": stamp,
            "sample_count": len(samples),
            "duration_ms": duration_ms,
            "window_ms": window_ms,
            "state_counts": dict(states),
            "raw_json": str(raw_path),
            "samples_csv": str(csv_path),
        }
        summary_path = output_dir / "summary.json"
        summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True), encoding="utf-8")
        self._set_power_status(summary=str(summary_path), raw_json=str(raw_path), samples_csv=str(csv_path))

    def capture_rgb666_payload(self,
                               width: int = 160,
                               height: int = 144,
                               timeout_ms: int = 2500,
                               stop_on_next_frame: bool = False) -> tuple[bytes, dict[str, Any]]:
        command = (
            "CAPTURE_RGB666_LINE_BURSTS "
            f"{width} {height} {timeout_ms} rising SPL 0 0 1 0 {int(stop_on_next_frame)}"
        )
        start = time.monotonic()
        with self._serial_lock:
            response = self._client.command(command)
        metadata = dict(response.data)
        data_hex = metadata.pop("data_hex", None)
        if not isinstance(data_hex, str):
            raise ValueError(metadata.get("error") or "RGB666 response did not include data_hex")
        payload = bytes.fromhex(data_hex)
        metadata["transport"] = "rgb666_json_hex"
        metadata["pixel_format"] = "RGB666"
        metadata["server_single_capture"] = True
        metadata["server_last_capture_ms"] = int((time.monotonic() - start) * 1000)
        with self._lock:
            self._frame_count += 1
            metadata["server_frame_count"] = self._frame_count
        return payload, metadata

    def recover(self) -> dict[str, Any]:
        self._capture_stop.set()
        thread = self._capture_thread
        if thread is not None and thread.is_alive():
            thread.join(timeout=2.0)
        safe_idle_data: dict[str, Any] = {}
        error = ""
        with self._serial_lock:
            try:
                self._client.close()
            except Exception:
                pass
            try:
                self._client = ProbeClient(self.port, self.baud, self.timeout)
                self._client.drain_startup()
                safe_idle_data = dict(self._client.command("SAFE_IDLE").data)
            except Exception as exc:
                error = str(exc)
                with self._lock:
                    self._latest_error = error
                    self._consecutive_errors += 1
        ok = not error and safe_idle_data.get("ok") is True
        if ok:
            with self._lock:
                self._latest_error = ""
                self._consecutive_errors = 0
        return {
            "ok": ok,
            "error": error,
            "safe_idle_ok": safe_idle_data.get("ok", False),
            "safe_idle": safe_idle_data,
            **self.status(False),
        }

    def _recover_serial_locked_stopless(self) -> None:
        with self._serial_lock:
            try:
                self._client.close()
            except Exception:
                pass
            try:
                self._client = ProbeClient(self.port, self.baud, self.timeout)
                self._client.drain_startup()
                self._client.command("SAFE_IDLE")
                with self._lock:
                    self._latest_error = "auto recovered after capture errors"
                    self._consecutive_errors = 0
            except Exception as exc:
                with self._lock:
                    self._latest_error = f"auto recovery failed: {exc}"


def split_frame_payload(data: dict[str, Any], crop_offset: int, crop_len: int) -> tuple[bytes, dict[str, Any]]:
    data_hex = data.get("data_hex")
    if not isinstance(data_hex, str):
        raise ValueError(data.get("error") or "capture response did not include data_hex")
    payload = bytes.fromhex(data_hex)
    metadata = dict(data)
    metadata.pop("data_hex", None)
    metadata["transport"] = "binary"
    if crop_len > 0:
        end = min(len(payload), crop_offset + crop_len)
        payload = payload[crop_offset:end]
        metadata["host_crop_offset"] = crop_offset
        metadata["host_crop_len"] = crop_len
        metadata["host_cropped_size"] = len(payload)
    return payload, metadata


def crop_rgb16_visible(payload: bytes,
                       stream_width: int,
                       stream_height: int,
                       visible_width: int,
                       visible_height: int) -> bytes:
    row_bytes = stream_width * 2
    visible_row_bytes = visible_width * 2
    if len(payload) < row_bytes * stream_height:
        raise ValueError("short RGB16 payload for visible crop")
    cropped = bytearray(visible_row_bytes * visible_height)
    for y in range(visible_height):
        src = y * row_bytes
        dst = y * visible_row_bytes
        cropped[dst:dst + visible_row_bytes] = payload[src:src + visible_row_bytes]
    return bytes(cropped)


def profile_gpio_rows(profile: dict[str, Any]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []

    def add(signal: str, role: str, bus_pin: Any, gpio: Any) -> None:
        if gpio is None:
            return
        try:
            gpio_int = int(gpio)
        except (TypeError, ValueError):
            return
        rows.append({
            "signal": signal,
            "role": role,
            "bus_pin": bus_pin,
            "gpio": gpio_int,
        })

    signals = profile.get("signals", {})
    for signal in signals.get("timing_or_control", []):
        role = ", ".join(signal.get("candidate_roles", []))
        bus_pin = signal.get("display_bus_pin", signal.get("display_bus_pins"))
        add(signal.get("name", ""), role, bus_pin, signal.get("current_esp32p4_gpio"))

    pixel_bus = signals.get("pixel_bus", {})
    for color, bits in pixel_bus.items():
        if color == "hypothesis" or not isinstance(bits, dict):
            continue
        for name, bit_info in bits.items():
            if isinstance(bit_info, dict):
                add(name, f"{color} pixel data", bit_info.get("display_bus_pin"), bit_info.get("esp32p4_gpio"))

    return sorted(rows, key=lambda row: (row["gpio"], row["signal"]))


def profile_gpio_set(profile: dict[str, Any]) -> set[int]:
    return {row["gpio"] for row in profile_gpio_rows(profile)}


def query_int(query: dict[str, list[str]], name: str, default: int, minimum: int, maximum: int) -> int:
    raw = (query.get(name) or [str(default)])[0]
    try:
        value = int(raw)
    except ValueError as exc:
        raise ValueError(f"{name}_invalid") from exc
    if value < minimum or value > maximum:
        raise ValueError(f"{name}_out_of_range_{minimum}_to_{maximum}")
    return value


def classify_power_sample(edges: dict[str, int], falling_edges: dict[str, int]) -> str:
    dclk_edges = edges.get("DCLK", 0) + falling_edges.get("DCLK", 0)
    sps_edges = edges.get("SPS", 0) + falling_edges.get("SPS", 0)
    spl_edges = edges.get("SPL", 0) + falling_edges.get("SPL", 0)
    lp_edges = edges.get("LP", 0) + falling_edges.get("LP", 0)
    cls_edges = edges.get("CLS", 0) + falling_edges.get("CLS", 0)
    ps_edges = edges.get("PS", 0) + falling_edges.get("PS", 0)
    line_edges = spl_edges + lp_edges
    if dclk_edges < 10 and sps_edges == 0 and line_edges == 0 and cls_edges == 0:
        return "off"
    if dclk_edges >= 10 and sps_edges == 0 and line_edges == 0:
        return "clock_only"
    if dclk_edges >= 10 and sps_edges > 0 and line_edges == 0:
        return "frame_no_line"
    if dclk_edges >= 10 and line_edges > 0 and sps_edges == 0:
        return "line_no_frame"
    if dclk_edges >= 100 and sps_edges > 0 and line_edges > 0:
        return "locked"
    if ps_edges > 0 or cls_edges > 0 or dclk_edges > 0 or sps_edges > 0 or line_edges > 0:
        return "unstable"
    return "unknown"


def write_events_csv(path: Path, events: list[dict[str, Any]]) -> None:
    fieldnames = ["t_us", "signal", "gpio", "level", "red6"]
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(events)


def save_workbench_json(kind: str, data: dict[str, Any], profile: dict[str, Any]) -> dict[str, str]:
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    profile_id = str(profile.get("profile_id", "unknown"))
    output_dir = Path("captures/experiments") / f"{stamp}-{profile_id}-{kind}"
    output_dir.mkdir(parents=True, exist_ok=True)
    raw_path = output_dir / "raw.json"
    raw_path.write_text(json.dumps(data, indent=2, sort_keys=True), encoding="utf-8")
    manifest = {
        "created_utc": stamp,
        "kind": kind,
        "profile_id": profile_id,
        "profile_schema_version": profile.get("schema_version"),
        "raw_json": str(raw_path),
    }
    manifest_path = output_dir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True), encoding="utf-8")
    return {"dir": str(output_dir), "raw_json": str(raw_path), "manifest": str(manifest_path)}


def summarize_line_clocks(data: dict[str, Any]) -> dict[str, Any]:
    samples = data.get("samples", [])
    deltas: list[int] = []
    for sample in samples:
        if "dclk_delta" in sample:
            deltas.append(int(sample["dclk_delta"]))
    delta_counts = Counter(deltas)
    return {
        "sample_count": len(samples),
        "delta_count": len(deltas),
        "delta_values": [
            {"delta": delta, "count": count}
            for delta, count in delta_counts.most_common(16)
        ],
    }


def load_profile(profile_path: str) -> dict[str, Any]:
    with Path(profile_path).open("r", encoding="utf-8") as handle:
        return json.load(handle)


def recent_artifacts(root: Path = Path("captures/experiments"), limit: int = 40) -> list[dict[str, Any]]:
    if not root.exists():
        return []
    folders = [path for path in root.iterdir() if path.is_dir()]
    folders.sort(key=lambda path: path.stat().st_mtime, reverse=True)
    items: list[dict[str, Any]] = []
    for folder in folders[:limit]:
        try:
            files = sorted(path.name for path in folder.iterdir() if path.is_file())
            manifest = "manifest.json" if (folder / "manifest.json").exists() else ""
            items.append({
                "name": folder.name,
                "path": str(folder),
                "modified_utc": datetime.fromtimestamp(folder.stat().st_mtime, timezone.utc).isoformat(),
                "manifest": manifest,
                "files": files[:12],
                "file_count": len(files),
            })
        except OSError:
            continue
    return items


def frontend_dist_dir() -> Path:
    return Path(__file__).resolve().parent / "workbench" / "frontend" / "dist"


def project_root_dir() -> Path:
    return Path(__file__).resolve().parent.parent


def make_handler(
    state: LiveLcdcamState,
    interval_ms: int,
    continuous_capture: bool,
    profile: dict[str, Any],
    destination_profile: dict[str, Any],
) -> type[BaseHTTPRequestHandler]:
    dist_dir = frontend_dist_dir()
    allowed_probe_commands = {
        "PING",
        "GET_VERSION",
        "GET_PINMAP",
        "EXPORT_STATS",
        "SAFE_IDLE",
        "SAFE_ISOLATE",
        "ELECTRICAL_ISOLATE",
    }
    allowed_gpios = profile_gpio_set(profile)
    profile_gpio_list = profile_gpio_rows(profile)
    allowed_line_markers = {
        row["signal"]
        for row in profile_gpio_list
        if "line_marker_candidate" in row.get("role", "")
    } or {"LP", "SPL"}

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, format: str, *args: Any) -> None:
            return

        def send_body(self, status: int, content_type: str, body: bytes) -> None:
            try:
                self.send_response(status)
                self.send_header("Content-Type", content_type)
                self.send_header("Content-Length", str(len(body)))
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                self.wfile.write(body)
            except BrokenPipeError:
                return

        def send_binary_frame(self, body: bytes, metadata: dict[str, Any]) -> None:
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Capture-Meta", json.dumps(metadata, separators=(",", ":")))
            self.end_headers()
            self.wfile.write(body)

        def send_error_with_status_meta(self, code: int, message: str) -> None:
            metadata = state.status()
            body = message.encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Capture-Meta", json.dumps(metadata, separators=(",", ":")))
            self.end_headers()
            self.wfile.write(body)

        def send_frontend_file(self, request_path: str) -> bool:
            if not dist_dir.exists():
                body = (
                    "Ant Design frontend build not found. Run "
                    "`cd host/workbench/frontend && npm install && npm run build`.\n"
                ).encode("utf-8")
                self.send_body(503, "text/plain; charset=utf-8", body)
                return True
            relative = request_path.lstrip("/") or "index.html"
            candidate = (dist_dir / relative).resolve()
            if not str(candidate).startswith(str(dist_dir.resolve())):
                self.send_body(403, "text/plain; charset=utf-8", b"forbidden\n")
                return True
            if not candidate.exists() or not candidate.is_file():
                candidate = dist_dir / "index.html"
            content_type = mimetypes.guess_type(candidate.name)[0] or "application/octet-stream"
            if candidate.name == "index.html":
                content_type = "text/html; charset=utf-8"
            self.send_body(200, content_type, candidate.read_bytes())
            return True

        def do_GET(self) -> None:
            parsed = urlparse(self.path)
            path = parsed.path
            if path == "/assets/game_boy_color_lense_mask.png":
                mask_path = project_root_dir() / "tools" / "game_boy_color_lense_mask.png"
                if mask_path.exists() and mask_path.is_file():
                    self.send_body(200, "image/png", mask_path.read_bytes())
                else:
                    self.send_body(404, "text/plain; charset=utf-8", b"lens mask not found\n")
                return
            if path == "/api/profile":
                self.send_body(200, "application/json", json.dumps(profile).encode("utf-8"))
                return
            if path == "/api/destination-profile":
                self.send_body(200, "application/json", json.dumps(destination_profile).encode("utf-8"))
                return
            if path == "/api/artifacts/recent":
                self.send_body(200, "application/json", json.dumps({
                    "ok": True,
                    "root": "captures/experiments",
                    "items": recent_artifacts(),
                }).encode("utf-8"))
                return
            if path == "/api/workbench/gpios":
                self.send_body(200, "application/json", json.dumps({
                    "ok": True,
                    "profile_id": profile.get("profile_id"),
                    "gpios": profile_gpio_list,
                }).encode("utf-8"))
                return
            if path == "/api/frame":
                try:
                    self.send_body(200, "application/json", json.dumps(state.capture()).encode("utf-8"))
                except Exception as exc:
                    self.send_body(500, "application/json", json.dumps({"ok": False, "error": str(exc)}).encode("utf-8"))
                return
            if path == "/api/frame.bin":
                try:
                    if continuous_capture:
                        current_status = state.status()
                        if not current_status.get("running"):
                            self.send_body(409, "text/plain; charset=utf-8", b"live capture is stopped; no current frame\n")
                            return
                        payload, metadata = state.latest_payload()
                    else:
                        payload, metadata = state.capture_payload()
                    self.send_binary_frame(payload, metadata)
                except Exception as exc:
                    current_status = state.status()
                    waiting = current_status.get("running") and current_status.get("source_state") in {"no_signal", "clock_detected"}
                    message = "source not present; waiting for DCLK" if waiting else str(exc)
                    self.send_error_with_status_meta(409 if waiting else 500, message)
                return
            if path == "/api/single-frame.bin":
                try:
                    payload, metadata = state.capture_payload()
                    metadata["server_single_capture"] = True
                    self.send_binary_frame(payload, metadata)
                except Exception as exc:
                    self.send_body(500, "text/plain; charset=utf-8", str(exc).encode("utf-8"))
                return
            if path == "/api/rgb666-frame.bin":
                try:
                    payload, metadata = state.capture_rgb666_payload()
                    self.send_binary_frame(payload, metadata)
                except Exception as exc:
                    self.send_body(500, "text/plain; charset=utf-8", str(exc).encode("utf-8"))
                return
            if path == "/api/safe-idle":
                try:
                    self.send_body(200, "application/json", json.dumps(state.safe_idle()).encode("utf-8"))
                except Exception as exc:
                    self.send_body(500, "application/json", json.dumps({"ok": False, "error": str(exc)}).encode("utf-8"))
                return
            if path == "/api/start":
                self.send_body(200, "application/json", json.dumps(state.start_continuous()).encode("utf-8"))
                return
            if path == "/api/stop":
                self.send_body(200, "application/json", json.dumps(state.stop_continuous(isolate=True)).encode("utf-8"))
                return
            if path == "/api/status":
                self.send_body(200, "application/json", json.dumps(state.status()).encode("utf-8"))
                return
            if path == "/api/recover":
                self.send_body(200, "application/json", json.dumps(state.recover()).encode("utf-8"))
                return
            if path == "/api/boot/status":
                self.send_body(200, "application/json", json.dumps(state.boot_status()).encode("utf-8"))
                return
            if path == "/api/boot/arm":
                query = parse_qs(parsed.query)
                try:
                    frame_count = query_int(query, "frames", 180, 1, 600)
                    wait_ms = query_int(query, "wait_ms", 15000, 1000, 60000)
                    probe_ms = query_int(query, "probe_ms", 100, 50, 1000)
                    self.send_body(200, "application/json", json.dumps(
                        state.start_boot_capture(frame_count, wait_ms, probe_ms)
                    ).encode("utf-8"))
                except Exception as exc:
                    self.send_body(400, "application/json", json.dumps({"ok": False, "error": str(exc)}).encode("utf-8"))
                return
            if path == "/api/boot/stop":
                self.send_body(200, "application/json", json.dumps(state.stop_boot_capture()).encode("utf-8"))
                return
            if path == "/api/power-monitor/status":
                self.send_body(200, "application/json", json.dumps(state.power_monitor_status()).encode("utf-8"))
                return
            if path == "/api/power-monitor/start":
                query = parse_qs(parsed.query)
                try:
                    duration_ms = query_int(query, "duration_ms", 30000, 1000, 120000)
                    window_ms = query_int(query, "window_ms", 50, 10, 1000)
                    self.send_body(200, "application/json", json.dumps(
                        state.start_power_monitor(duration_ms, window_ms)
                    ).encode("utf-8"))
                except Exception as exc:
                    self.send_body(400, "application/json", json.dumps({"ok": False, "error": str(exc)}).encode("utf-8"))
                return
            if path == "/api/power-monitor/stop":
                self.send_body(200, "application/json", json.dumps(state.stop_power_monitor()).encode("utf-8"))
                return
            if path == "/api/probe-command":
                query = parse_qs(parsed.query)
                command = (query.get("cmd") or [""])[0].strip()
                if command not in allowed_probe_commands:
                    self.send_body(400, "application/json", json.dumps({
                        "ok": False,
                        "error": "command_not_allowed",
                        "allowed": sorted(allowed_probe_commands),
                    }).encode("utf-8"))
                    return
                try:
                    self.send_body(200, "application/json", json.dumps(state.probe_command(command)).encode("utf-8"))
                except Exception as exc:
                    self.send_body(500, "application/json", json.dumps({"ok": False, "error": str(exc)}).encode("utf-8"))
                return
            if path == "/api/workbench/read-gpios":
                results = []
                try:
                    for row in profile_gpio_list:
                        result = state.read_gpio(row["gpio"])
                        result.update({"signal": row["signal"], "role": row["role"], "bus_pin": row["bus_pin"]})
                        results.append(result)
                    self.send_body(200, "application/json", json.dumps({"ok": True, "results": results}).encode("utf-8"))
                except Exception as exc:
                    self.send_body(500, "application/json", json.dumps({"ok": False, "error": str(exc), "results": results}).encode("utf-8"))
                return
            if path == "/api/workbench/count-edges-all":
                query = parse_qs(parsed.query)
                try:
                    duration_ms = query_int(query, "duration_ms", 1000, 1, 10000)
                    results = []
                    for row in profile_gpio_list:
                        result = state.count_gpio_edges(row["gpio"], duration_ms)
                        result.update({"signal": row["signal"], "role": row["role"], "bus_pin": row["bus_pin"]})
                        results.append(result)
                    self.send_body(200, "application/json", json.dumps({
                        "ok": True,
                        "duration_ms": duration_ms,
                        "results": results,
                    }).encode("utf-8"))
                except Exception as exc:
                    self.send_body(500, "application/json", json.dumps({"ok": False, "error": str(exc)}).encode("utf-8"))
                return
            if path == "/api/workbench/measure-clock":
                query = parse_qs(parsed.query)
                try:
                    gpio = query_int(query, "gpio", -1, 0, 54)
                    duration_ms = query_int(query, "duration_ms", 1000, 1, 10000)
                    if gpio not in allowed_gpios:
                        raise ValueError("gpio_not_in_active_profile")
                    result = state.measure_clock(gpio, duration_ms)
                    self.send_body(200, "application/json", json.dumps(result).encode("utf-8"))
                except Exception as exc:
                    self.send_body(400, "application/json", json.dumps({"ok": False, "error": str(exc)}).encode("utf-8"))
                return
            if path == "/api/workbench/capture-timing":
                query = parse_qs(parsed.query)
                try:
                    duration_ms = query_int(query, "duration_ms", 100, 1, 250)
                    data = state.capture_timing_edges(duration_ms)
                    summary = summarize_capture(data)
                    relationships = analyze_timing_relationships(data.get("events", []))
                    artifacts = save_workbench_json("timing_edges", data, profile)
                    events = data.get("events", [])
                    if isinstance(events, list):
                        csv_path = Path(artifacts["dir"]) / "events.csv"
                        write_events_csv(csv_path, events)
                        artifacts["events_csv"] = str(csv_path)
                        vcd_path = Path(artifacts["dir"]) / "timing_edges.vcd"
                        write_vcd(data, vcd_path)
                        artifacts["pulseview_vcd"] = str(vcd_path)
                    summary_path = Path(artifacts["dir"]) / "summary.json"
                    relationships_path = Path(artifacts["dir"]) / "relationships.json"
                    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True), encoding="utf-8")
                    relationships_path.write_text(json.dumps(relationships, indent=2, sort_keys=True), encoding="utf-8")
                    artifacts["summary_json"] = str(summary_path)
                    artifacts["relationships_json"] = str(relationships_path)
                    self.send_body(200, "application/json", json.dumps({
                        "ok": data.get("ok", False),
                        "summary": summary,
                        "relationships": relationships,
                        "artifacts": artifacts,
                    }).encode("utf-8"))
                except Exception as exc:
                    self.send_body(500, "application/json", json.dumps({"ok": False, "error": str(exc)}).encode("utf-8"))
                return
            if path == "/api/workbench/line-clocks":
                query = parse_qs(parsed.query)
                try:
                    marker = (query.get("marker") or ["LP"])[0].strip().upper()
                    edge = (query.get("edge") or ["falling"])[0].strip().lower()
                    line_count = query_int(query, "line_count", 180, 1, 256)
                    timeout_ms = query_int(query, "timeout_ms", 2000, 1, 5000)
                    if marker not in allowed_line_markers:
                        raise ValueError("marker_not_a_profile_line_candidate")
                    if edge not in {"falling", "rising"}:
                        raise ValueError("edge_invalid")
                    data = state.capture_line_clocks(marker, edge, line_count, timeout_ms)
                    summary = summarize_line_clocks(data)
                    artifacts = save_workbench_json("line_clocks", data, profile)
                    summary_path = Path(artifacts["dir"]) / "summary.json"
                    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True), encoding="utf-8")
                    artifacts["summary_json"] = str(summary_path)
                    self.send_body(200, "application/json", json.dumps({
                        "ok": data.get("ok", False),
                        "summary": summary,
                        "raw": data,
                        "artifacts": artifacts,
                    }).encode("utf-8"))
                except Exception as exc:
                    self.send_body(400, "application/json", json.dumps({"ok": False, "error": str(exc)}).encode("utf-8"))
                return
            if not path.startswith("/api/"):
                self.send_frontend_file(path)
                return
            self.send_body(404, "text/plain; charset=utf-8", b"not found\n")

    return Handler


def run(args: argparse.Namespace) -> int:
    port = args.port or autodetect_port()
    profile = load_profile(args.profile)
    destination_profile = load_profile(args.destination_profile)
    bytes_per_sample = 2 if args.data_mode in {"RGB664", "RGB565"} else 1
    crop_len = args.crop_width * args.crop_height * bytes_per_sample if args.host_crop else 0
    firmware_emit_len = args.firmware_emit_len
    if args.gbc_source_driver:
        if args.data_mode != "RGB565":
            raise ValueError("--gbc-source-driver supports RGB565 for the GBC source path")
        command = (
            f"GBC_SOURCE_FRAME_BIN {args.capture_timeout_ms} {args.data_mode} "
            f"{firmware_emit_len} {int(args.pclk_invert)}"
        )
        if firmware_emit_len > 0:
            crop_len = 0
    elif args.source_binary:
        command = "LCDCAM_RAW_CAPTURE_SRC_BIN"
        crop_len = 0
        firmware_emit_len = args.crop_width * args.crop_height * bytes_per_sample
    else:
        command_name = "LCDCAM_RAW_CAPTURE_BIN" if args.firmware_binary else "LCDCAM_RAW_CAPTURE"
        command = (
            f"{command_name} HIGH {args.capture_timeout_ms} "
            f"0 0 {int(args.pclk_invert)} 1 {args.width} {args.height} 1 0 {args.data_mode}"
        )
        if args.firmware_binary and firmware_emit_len > 0:
            command += f" {firmware_emit_len}"
    state = LiveLcdcamState(port,
                            args.baud,
                            args.timeout,
                            command,
                            args.data_mode,
                            args.crop_offset,
                            crop_len,
                            args.firmware_binary,
                            args.stream_batch_size,
                            args.pclk_invert,
                            args.gbc_source_driver,
                            args.capture_timeout_ms)
    server = ThreadingHTTPServer(
        (args.listen_host, args.listen_port),
        make_handler(state, args.interval_ms, args.continuous_capture, profile, destination_profile),
    )

    def stop(_signum: int, _frame: Any) -> None:
        server.shutdown()

    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)
    url = f"http://{args.listen_host}:{server.server_port}/"
    if args.continuous_capture:
        state.start_continuous()
    print(json.dumps({
        "ok": True,
        "url": url,
        "serial_port": port,
        "command": command,
        "firmware_binary": args.firmware_binary,
        "firmware_emit_len": firmware_emit_len,
        "source_binary": args.source_binary,
        "gbc_source_driver": args.gbc_source_driver,
        "continuous_capture": args.continuous_capture,
        "stream_batch_size": args.stream_batch_size,
        "host_crop": args.host_crop,
        "crop_offset": args.crop_offset,
        "crop_len": crop_len,
        "profile": args.profile,
        "profile_id": profile.get("profile_id"),
        "destination_profile": args.destination_profile,
        "destination_profile_id": destination_profile.get("profile_id"),
    }, sort_keys=True), flush=True)
    try:
        server.serve_forever()
    finally:
        try:
            state.safe_idle()
        except Exception:
            pass
        state.close()
        server.server_close()
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="serial port, for example /dev/cu.usbmodem14401")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--width", type=int, default=320)
    parser.add_argument("--height", type=int, default=204)
    parser.add_argument("--capture-timeout-ms", type=int, default=2500)
    parser.add_argument("--pclk-invert", action=argparse.BooleanOptionalAction, default=False)
    parser.add_argument("--data-mode", choices=["RG44", "RGB332", "RGB664", "RGB565"], default="RGB565")
    parser.add_argument("--firmware-binary", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--firmware-emit-len", type=int, default=0)
    parser.add_argument("--source-binary", action=argparse.BooleanOptionalAction, default=False)
    parser.add_argument("--gbc-source-driver", action=argparse.BooleanOptionalAction, default=False)
    parser.add_argument("--continuous-capture", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--stream-batch-size", type=int, default=8)
    parser.add_argument("--host-crop", action=argparse.BooleanOptionalAction, default=False)
    parser.add_argument("--crop-offset", type=int, default=0)
    parser.add_argument("--crop-width", type=int, default=161)
    parser.add_argument("--crop-height", type=int, default=145)
    parser.add_argument("--interval-ms", type=int, default=1400)
    parser.add_argument("--listen-host", default="127.0.0.1")
    parser.add_argument("--listen-port", type=int, default=8772)
    parser.add_argument("--profile", default="profiles/gbc_lcd.json")
    parser.add_argument("--destination-profile", default="profiles/spi_lcd_destination.json")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    try:
        return run(args)
    except Exception as exc:
        print(f"error: {exc}")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
