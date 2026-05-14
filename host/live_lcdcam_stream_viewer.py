#!/usr/bin/env python3
"""Live browser viewer for fast LCD_CAM RAW8 streams with decode sliders."""

from __future__ import annotations

import argparse
import csv
import json
import mimetypes
import os
from pathlib import Path
import signal
import subprocess
import threading
import time
from collections import Counter
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any
from urllib.parse import parse_qs, unquote, urlparse

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
        self._client: ProbeClient | None = None
        self._serial_owner = "backend"
        self._device_error = ""
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
        with self._serial_lock:
            self._connect_serial_locked(clear_error=False)

    def _connect_serial_locked(self, clear_error: bool) -> bool:
        try:
            self._client = ProbeClient(self.port, self.baud, self.timeout)
            self._client.drain_startup()
            self._serial_owner = "backend"
            self._device_error = ""
            if clear_error:
                with self._lock:
                    self._latest_error = ""
                    self._consecutive_errors = 0
                    if self._source_state == "device_unavailable":
                        self._source_state = "starting"
            return True
        except Exception as exc:
            self._client = None
            if self._serial_owner != "browser_flash":
                self._serial_owner = "disconnected"
            self._device_error = str(exc)
            with self._lock:
                self._source_state = "device_unavailable"
                self._latest_error = self._device_error
            return False

    def _require_client_locked(self) -> ProbeClient:
        if self._client is None:
            raise ConnectionError(f"device unavailable on {self.port}: {self._device_error or 'not connected'}")
        return self._client

    def close(self) -> None:
        self.stop_power_monitor()
        self.stop_boot_capture()
        self.stop_continuous(safe_idle=False)
        with self._serial_lock:
            if self._client is not None:
                self._client.close()
                self._client = None
            self._serial_owner = "closed"

    def release_serial_for_deploy(self) -> dict[str, Any]:
        self.stop_power_monitor()
        self.stop_boot_capture()
        self.stop_continuous(isolate=True)
        with self._serial_lock:
            if self._client is not None:
                self._client.close()
                self._client = None
            self._serial_owner = "browser_flash"
        return {"ok": True, "port": self.port, "state": "serial_released_for_browser_flash", "serial_owner": self._serial_owner}

    def reconnect_serial_after_deploy(self) -> dict[str, Any]:
        with self._serial_lock:
            if self._client is None:
                if not self._connect_serial_locked(clear_error=True):
                    return {
                        "ok": False,
                        "port": self.port,
                        "state": "reconnect_failed",
                        "serial_owner": self._serial_owner,
                        "error": self._device_error or "reconnect_failed",
                    }
            self._serial_owner = "backend"
        return {
            "ok": True,
            "port": self.port,
            "state": "serial_reconnected",
            "serial_owner": self._serial_owner,
            "status": self.status(),
        }

    def capture(self) -> dict[str, Any]:
        with self._serial_lock:
            response = self._require_client_locked().command(self.command)
        return dict(response.data)

    def capture_payload(self) -> tuple[bytes, dict[str, Any]]:
        with self._serial_lock:
            client = self._require_client_locked()
            if self.firmware_binary:
                response = client.command_binary(self.command)
                payload = response.payload
                metadata = dict(response.data)
                metadata["transport"] = "firmware_binary"
            else:
                response = client.command(self.command)
                payload, metadata = split_frame_payload(response.data, 0, 0)
        if self.crop_len > 0:
            end = min(len(payload), self.crop_offset + self.crop_len)
            payload = payload[self.crop_offset:end]
            metadata["host_crop_offset"] = self.crop_offset
            metadata["host_crop_len"] = self.crop_len
            metadata["host_cropped_size"] = len(payload)
        return payload, metadata

    def start_continuous(self) -> dict[str, Any]:
        if self._client is None:
            with self._lock:
                self._source_state = "device_unavailable"
                self._latest_error = self._device_error or f"device unavailable on {self.port}"
            return self.status(False)
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
                "device_connected": self._client is not None,
                "device_error": self._device_error,
                "serial_port": self.port,
                "serial_owner": self._serial_owner,
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
                responses = self._require_client_locked().command_binary_sequence(
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
                dclk = self.measure_clock(22, 20)
                dclk_hz = float(dclk.get("rising_edge_hz", 0.0) or 0.0)
                dclk_edges = int(dclk.get("rising_edges", 0) or 0)
                probe = {
                    "dclk_edges_20ms": dclk_edges,
                    "dclk_hz": dclk_hz,
                    "checked_ms": int(time.monotonic() * 1000),
                }
                with self._lock:
                    self._source_last_probe = probe
                    self._latest_metadata = {"ok": False, "source_state": "no_signal", "error": "source not present; waiting for DCLK", **probe}
                if dclk_hz > 10000.0:
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
            response = self._require_client_locked().command("SAFE_IDLE")
        return dict(response.data)

    def electrical_isolate(self) -> dict[str, Any]:
        with self._serial_lock:
            response = self._require_client_locked().command("ELECTRICAL_ISOLATE")
        return dict(response.data)

    def probe_command(self, command: str) -> dict[str, Any]:
        with self._serial_lock:
            response = self._require_client_locked().command(command)
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
            response = self._require_client_locked().command(command)
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
                if self._client is not None:
                    self._client.close()
            except Exception:
                pass
            try:
                if not self._connect_serial_locked(clear_error=True):
                    raise ConnectionError(self._device_error)
                safe_idle_data = dict(self._require_client_locked().command("SAFE_IDLE").data)
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
                if self._client is not None:
                    self._client.close()
            except Exception:
                pass
            try:
                if not self._connect_serial_locked(clear_error=True):
                    raise ConnectionError(self._device_error)
                self._require_client_locked().command("SAFE_IDLE")
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
                       visible_height: int,
                       linear_shift_pixels: int = -4) -> bytes:
    row_bytes = stream_width * 2
    visible_row_bytes = visible_width * 2
    if len(payload) < row_bytes * stream_height:
        raise ValueError("short RGB16 payload for visible crop")
    cropped = bytearray(visible_row_bytes * visible_height)
    for y in range(visible_height):
        for x in range(visible_width):
            src_pixel = y * stream_width + x + linear_shift_pixels
            dst = (y * visible_width + x) * 2
            if src_pixel < 0:
                cropped[dst:dst + 2] = payload[0:2]
            else:
                src = src_pixel * 2
                if src + 2 <= len(payload):
                    cropped[dst:dst + 2] = payload[src:src + 2]
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


def save_destination_profile(
    profile_path: str,
    destination_profile: dict[str, Any],
    pin_mapping: list[dict[str, Any]],
    destination_settings: dict[str, Any] | None = None,
) -> dict[str, Any]:
    path = Path(profile_path)
    if not path.exists() or not path.is_file():
        raise ValueError("destination_profile_not_found")
    connector = destination_profile.setdefault("connector", {})
    pins = connector.setdefault("pins", [])
    existing_by_name = {
        str(pin.get("name")): pin
        for pin in pins
        if isinstance(pin, dict) and pin.get("name") is not None
    }
    next_pins: list[dict[str, Any]] = []
    for item in pin_mapping:
        if not isinstance(item, dict):
            raise ValueError("pin_mapping_item_invalid")
        name = str(item.get("panel_pin", "")).strip()
        role = str(item.get("role", "")).strip()
        gpio_value = item.get("esp32p4_gpio")
        notes = str(item.get("notes", ""))
        if not name or any(ch not in "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_/-" for ch in name):
            raise ValueError("invalid_panel_pin")
        if not role:
            raise ValueError("invalid_role")
        if gpio_value is not None:
            if not isinstance(gpio_value, int) or gpio_value < 0 or gpio_value > 54:
                raise ValueError("invalid_gpio")
        base = dict(existing_by_name.get(name, {}))
        base.update({
            "name": name,
            "role": role,
            "esp32p4_gpio": gpio_value,
            "notes": notes,
        })
        next_pins.append(base)

    connector["pins"] = next_pins
    connector["pin_count"] = len(next_pins)
    destination_profile["status"] = "draft_wired" if any(pin.get("esp32p4_gpio") is not None for pin in next_pins) else "draft_unwired"

    gpio_by_name = {str(pin["name"]): pin.get("esp32p4_gpio") for pin in next_pins}
    signals = destination_profile.get("signals", {})
    for signal in signals.get("timing_or_control", []):
        if isinstance(signal, dict) and signal.get("name") in gpio_by_name:
            signal["current_esp32p4_gpio"] = gpio_by_name[str(signal["name"])]
    pixel_bus = signals.get("pixel_bus", {})
    if isinstance(pixel_bus, dict):
        data_signal = pixel_bus.get("data_signal")
        if isinstance(data_signal, dict) and data_signal.get("name") in gpio_by_name:
            data_signal["current_esp32p4_gpio"] = gpio_by_name[str(data_signal["name"])]

    if destination_settings is not None:
        if not isinstance(destination_settings, dict):
            raise ValueError("destination_settings_invalid")
        destination = destination_profile.setdefault("destination", {})
        controller_ic = destination_settings.get("controller_ic")
        if controller_ic is not None:
            controller_ic = str(controller_ic).strip()
            destination["controller_ic"] = None if controller_ic in {"", "unknown"} else controller_ic
        resolution = destination_settings.get("native_resolution")
        if resolution is not None:
            if not isinstance(resolution, dict):
                raise ValueError("native_resolution_invalid")
            width = resolution.get("width")
            height = resolution.get("height")
            if width is None or height is None:
                destination["native_resolution"] = None
            else:
                if not isinstance(width, int) or not isinstance(height, int) or width < 1 or height < 1 or width > 4096 or height > 4096:
                    raise ValueError("native_resolution_invalid")
                destination["native_resolution"] = {"width": width, "height": height}
        orientation = destination_settings.get("orientation")
        if isinstance(orientation, dict):
            target_orientation = destination.setdefault("orientation", {})
            for key in ("swap_xy", "mirror_x", "mirror_y"):
                if key in orientation:
                    target_orientation[key] = bool(orientation[key])
        color = destination_settings.get("color")
        if isinstance(color, dict):
            target_color = destination.setdefault("color", {})
            if "color_order" in color:
                color_order = str(color.get("color_order") or "unknown")
                if color_order not in {"unknown", "rgb", "bgr"}:
                    raise ValueError("color_order_invalid")
                target_color["color_order"] = color_order
            if "invert_color" in color:
                target_color["invert_color"] = bool(color["invert_color"])
        spi = destination_settings.get("spi")
        if isinstance(spi, dict):
            target_spi = destination.setdefault("spi", {})
            int_fields = {
                "pclk_hz_initial": (100000, 80000000),
                "mode": (0, 3),
                "cmd_bits": (8, 16),
                "param_bits": (8, 16),
                "max_transfer_lines_initial": (1, 512),
            }
            for key, (minimum, maximum) in int_fields.items():
                if key in spi:
                    value = spi[key]
                    if not isinstance(value, int) or value < minimum or value > maximum:
                        raise ValueError(f"{key}_invalid")
                    target_spi[key] = value

    path.write_text(json.dumps(destination_profile, indent=2, sort_keys=False) + "\n", encoding="utf-8")
    return destination_profile


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


def projects_dir() -> Path:
    return project_root_dir() / "projects"


def load_project_profiles() -> list[dict[str, Any]]:
    items: list[dict[str, Any]] = []
    root = projects_dir()
    if not root.exists():
        return items
    for path in sorted(root.glob("*.json")):
        try:
            project = json.loads(path.read_text(encoding="utf-8"))
            if isinstance(project, dict):
                project["_path"] = str(path.relative_to(project_root_dir()))
                items.append(project_with_normalized_profiles(project))
        except Exception:
            continue
    return items


def public_project(project: dict[str, Any]) -> dict[str, Any]:
    normalized = project_with_normalized_profiles(project)
    return {k: v for k, v in normalized.items() if k != "_path"}


def legacy_production_build_profile(project: dict[str, Any]) -> dict[str, Any]:
    production = project.get("production")
    if isinstance(production, dict):
        return {
            "id": "production",
            "name": "Production",
            "role": "production",
            "description": "Clean deployable firmware image with no extra lab control path in the hot loop.",
            "build_script": production.get("build_script"),
            "flash_script": production.get("flash_script"),
            "default_env": production.get("default_env", {}) if isinstance(production.get("default_env"), dict) else {},
            "known_good_command": production.get("known_good_command"),
        }
    return {
        "id": "production",
        "name": "Production",
        "role": "production",
        "description": "Clean deployable firmware image with no extra lab control path in the hot loop.",
        "build_script": None,
        "flash_script": None,
        "default_env": {},
        "known_good_command": None,
    }


def normalize_build_profiles(project: dict[str, Any]) -> dict[str, dict[str, Any]]:
    raw_profiles = project.get("build_profiles")
    profiles: dict[str, dict[str, Any]] = {}
    if isinstance(raw_profiles, dict):
        for profile_id, raw in raw_profiles.items():
            if not isinstance(raw, dict):
                continue
            profiles[str(profile_id)] = {
                "id": str(profile_id),
                "name": str(raw.get("name") or str(profile_id).replace("_", " ").title()),
                "role": str(raw.get("role") or profile_id),
                "description": str(raw.get("description") or ""),
                "build_script": raw.get("build_script"),
                "flash_script": raw.get("flash_script"),
                "default_env": raw.get("default_env", {}) if isinstance(raw.get("default_env"), dict) else {},
                "known_good_command": raw.get("known_good_command"),
            }
    if "production" not in profiles:
        profiles["production"] = legacy_production_build_profile(project)
    return profiles


def project_with_normalized_profiles(project: dict[str, Any]) -> dict[str, Any]:
    normalized = dict(project)
    normalized["build_profiles"] = normalize_build_profiles(project)
    if "production" not in normalized or not isinstance(normalized.get("production"), dict):
        normalized["production"] = normalized["build_profiles"].get("production", {})
    return normalized


def project_path_for_id(project_id: str) -> Path:
    if not project_id or any(ch not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-" for ch in project_id):
        raise ValueError("invalid_project_id")
    return projects_dir() / f"{project_id}.json"


def find_project(projects: list[dict[str, Any]], project_id: str) -> dict[str, Any]:
    for project in projects:
        if project.get("id") == project_id:
            return project
    raise ValueError(f"unknown_project:{project_id}")


def create_project_profile(payload: dict[str, Any]) -> dict[str, Any]:
    project_id = str(payload.get("id") or "").strip()
    name = str(payload.get("name") or project_id or "Untitled Project").strip()
    if not project_id:
        raise ValueError("id_required")
    path = project_path_for_id(project_id)
    if path.exists():
        raise ValueError("project_exists")
    project = {
        "schema_version": "0.1",
        "id": project_id,
        "name": name,
        "status": "draft",
        "description": str(payload.get("description") or "Draft project created from the workbench."),
        "source": payload.get("source") if isinstance(payload.get("source"), dict) else {"block": "unassigned_source", "profile": None},
        "processing": payload.get("processing") if isinstance(payload.get("processing"), list) else [],
        "destination": payload.get("destination") if isinstance(payload.get("destination"), dict) else {"block": "unassigned_destination", "profile": None},
        "graph": payload.get("graph") if isinstance(payload.get("graph"), dict) else {"nodes": [], "edges": []},
        "build_profiles": payload.get("build_profiles") if isinstance(payload.get("build_profiles"), dict) else {
            "lab": {
                "name": "Lab",
                "role": "lab",
                "description": "Research, probing, capture, and validation firmware with the interactive command path enabled.",
                "build_script": "scripts/build_lab_firmware.sh",
                "flash_script": "scripts/flash_lab_firmware.sh",
                "default_env": {"LAB_BUILD_DIR": "build_lab"},
                "known_good_command": "./scripts/flash_lab_firmware.sh ${ESP32P4_PORT}",
            },
            "telemetry": {
                "name": "Telemetry",
                "role": "telemetry",
                "description": "Runtime observation firmware for selected ESP32-P4 blocks with the command path enabled.",
                "build_script": "scripts/build_telemetry_firmware.sh",
                "flash_script": "scripts/flash_telemetry_firmware.sh",
                "default_env": {"TELEMETRY_BUILD_DIR": "build_telemetry"},
                "known_good_command": "./scripts/flash_telemetry_firmware.sh ${ESP32P4_PORT}",
            },
            "production": {
                "name": "Production",
                "role": "production",
                "description": "Deployable project firmware with minimal overhead in the hot path.",
                "build_script": None,
                "flash_script": None,
                "default_env": {},
                "known_good_command": None,
            },
        },
        "production": payload.get("production") if isinstance(payload.get("production"), dict) else {
            "build_script": None,
            "flash_script": None,
            "default_env": {},
            "known_good_command": None,
        },
        "evidence": payload.get("evidence") if isinstance(payload.get("evidence"), list) else [],
        "known_limits": payload.get("known_limits") if isinstance(payload.get("known_limits"), list) else [],
    }
    projects_dir().mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(project, indent=2, sort_keys=False) + "\n", encoding="utf-8")
    project["_path"] = str(path.relative_to(project_root_dir()))
    return project_with_normalized_profiles(project)


def save_project_profile(payload: dict[str, Any]) -> dict[str, Any]:
    project_id = str(payload.get("id") or "").strip()
    if not project_id:
        raise ValueError("id_required")
    path = project_path_for_id(project_id)
    if not path.exists():
        raise ValueError("project_not_found")
    project = dict(payload)
    project.pop("_path", None)
    if project_id != project.get("id"):
        raise ValueError("id_mismatch")
    if not isinstance(project.get("production"), dict):
        profiles = normalize_build_profiles(project)
        project["production"] = profiles.get("production", {})
    path.write_text(json.dumps(project, indent=2, sort_keys=False) + "\n", encoding="utf-8")
    project["_path"] = str(path.relative_to(project_root_dir()))
    return project_with_normalized_profiles(project)


def duplicate_project_profile(projects: list[dict[str, Any]], payload: dict[str, Any]) -> dict[str, Any]:
    source_id = str(payload.get("source_id") or "").strip()
    new_id = str(payload.get("id") or "").strip()
    if not source_id or not new_id:
        raise ValueError("source_id_and_id_required")
    source = public_project(find_project(projects, source_id))
    source["id"] = new_id
    source["name"] = str(payload.get("name") or f"{source.get('name', source_id)} Copy")
    source["status"] = "draft"
    return create_project_profile(source)


def slugify_project_id(value: str) -> str:
    slug = []
    for ch in value.lower():
        if ch.isalnum():
            slug.append(ch)
        elif ch in {"/", "-", ".", " "}:
            slug.append("_")
    collapsed = "_".join(part for part in "".join(slug).split("_") if part)
    return collapsed[:96] or "idf_example"


def graph_nodes_for_idf_example(example: dict[str, Any]) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    api_groups = [str(item) for item in example.get("api_groups", []) if isinstance(item, str)]
    mcu_blocks = [str(item) for item in example.get("mcu_blocks", []) if isinstance(item, str)]
    components = [str(item) for item in example.get("components", []) if isinstance(item, str)]

    block_labels = {
        "freertos": "Application Task",
        "gpio": "GPIO Control",
        "spi_master": "SPI Master Bus",
        "esp_lcd": "LCD Panel Driver",
        "camera": "Camera Source",
        "isp": "Image Signal Processor",
        "ppa": "Pixel Processing Accelerator",
        "jpeg": "JPEG/Image Decoder",
        "parlio": "Parallel IO Port",
        "tinyusb": "USB Device Stack",
        "usb_serial_jtag": "USB Serial/JTAG Console",
        "uart": "UART Link",
        "i2c": "I2C Control Bus",
        "i2s": "I2S Audio/Data Port",
        "rmt": "RMT Waveform Engine",
        "gptimer": "Timer Source",
        "ledc": "PWM/LEDC Output",
        "psram": "External Frame/Work Buffer",
        "bitscrambler": "Bitstream Formatter",
    }
    external_labels = []
    lowered_id = str(example.get("id", "")).lower()
    if any(group in api_groups for group in ("esp_lcd", "spi_master")) and "lcd" in lowered_id:
        external_labels.append("External LCD Panel")
    if "camera" in api_groups:
        external_labels.append("External Camera/Sensor")
    if "tinyusb" in api_groups:
        external_labels.append("USB Host Computer")
    if "i2c" in api_groups:
        external_labels.append("I2C Peripheral")
    if "i2s" in api_groups:
        external_labels.append("Audio Codec / I2S Device")

    nodes: list[dict[str, Any]] = [
        {
            "id": "system_intent",
            "type": "system_intent",
            "label": str(example.get("id", "ESP-IDF Example")),
            "position": {"x": 0, "y": 220},
            "params": {
                "role": "imported_example_intent",
                "path": example.get("path"),
            },
        },
    ]
    edges: list[dict[str, Any]] = []

    for index, group in enumerate(api_groups[:10]):
        node_id = f"block_{index}_{slugify_project_id(group)}"
        params = {
            "api_group": group,
            "components": components,
        }
        if group == "freertos":
            params["overlay"] = {
                "editable": True,
                "block_kind": "rtos_task",
                "task_name": "app_main",
                "priority": 5,
                "stack_size_bytes": 4096,
                "core_affinity": "any",
                "enabled": True,
                "notes": "Imported SDK example task overlay. Source remains read-only until explicit graph generation exists.",
                "source_write_policy": "overlay_only",
            }
        nodes.append({
            "id": node_id,
            "type": "lab_function_block",
            "label": block_labels.get(group, group),
            "position": {"x": 330 + index * 230, "y": 220 if index % 2 == 0 else 380},
            "params": params,
        })
        edges.append({
            "id": f"system_intent_{node_id}",
            "from": "system_intent.out",
            "to": f"{node_id}.in",
            "label": "uses",
        })

    group_node_ids = {
        group: f"block_{index}_{slugify_project_id(group)}"
        for index, group in enumerate(api_groups[:10])
    }
    if "jpeg" in group_node_ids and "spi_master" in group_node_ids:
        edges.append({
            "id": "jpeg_spi_pixels",
            "from": f"{group_node_ids['jpeg']}.out",
            "to": f"{group_node_ids['spi_master']}.in",
            "label": "pixels",
        })
    if "esp_lcd" in group_node_ids and "spi_master" in group_node_ids:
        edges.append({
            "id": "lcd_spi_panel_io",
            "from": f"{group_node_ids['esp_lcd']}.out",
            "to": f"{group_node_ids['spi_master']}.in",
            "label": "panel_io",
        })
    if "gpio" in group_node_ids and "spi_master" in group_node_ids:
        edges.append({
            "id": "gpio_spi_control",
            "from": f"{group_node_ids['gpio']}.out",
            "to": f"{group_node_ids['spi_master']}.in",
            "label": "control_pins",
        })
    if "camera" in group_node_ids and "isp" in group_node_ids:
        edges.append({
            "id": "camera_isp_pixels",
            "from": f"{group_node_ids['camera']}.out",
            "to": f"{group_node_ids['isp']}.in",
            "label": "pixels",
        })
    if "isp" in group_node_ids and "ppa" in group_node_ids:
        edges.append({
            "id": "isp_ppa_pixels",
            "from": f"{group_node_ids['isp']}.out",
            "to": f"{group_node_ids['ppa']}.in",
            "label": "pixels",
        })

    resource_affinity = {
        "freertos": ["freertos"],
        "gpio": ["gpio"],
        "spi_master": ["spi", "gdma", "dma"],
        "esp_lcd": ["lcd", "rgb lcd", "mipi dsi", "i80", "spi", "gdma"],
        "camera": ["cam", "lcd_cam", "gdma", "psram"],
        "isp": ["isp", "gdma", "psram"],
        "ppa": ["ppa", "dma", "psram"],
        "jpeg": ["jpeg", "dma", "psram"],
        "parlio": ["parlio", "gpio", "gdma"],
        "tinyusb": ["usb", "tinyusb"],
        "usb_serial_jtag": ["usb serial", "usb"],
        "uart": ["uart", "gpio"],
        "i2c": ["i2c", "gpio"],
        "i2s": ["i2s", "gpio", "gdma"],
        "rmt": ["rmt", "gpio"],
        "gptimer": ["timer"],
        "ledc": ["ledc", "gpio"],
        "psram": ["psram", "heap"],
        "bitscrambler": ["bitscrambler", "dma"],
    }
    for index, block in enumerate(mcu_blocks[:14]):
        node_id = f"mcu_{index}_{slugify_project_id(block)}"
        block_key = block.lower().replace("/", " ")
        nodes.append({
            "id": node_id,
            "type": "esp32p4_resource",
            "label": block,
            "position": {"x": 330 + (index % 7) * 210, "y": 620 + (index // 7) * 120},
            "params": {
                "resource_claim": "inferred",
                "confidence": "metadata",
            },
        })
        for group, function_id in group_node_ids.items():
            terms = resource_affinity.get(group, [group.replace("_", " ")])
            if any(term in block_key for term in terms):
                edges.append({
                    "id": f"{function_id}_{node_id}",
                    "from": f"{function_id}.out",
                    "to": f"{node_id}.in",
                    "label": "claims",
                })
                break

    for index, label in enumerate(external_labels[:6]):
        node_id = f"external_{index}_{slugify_project_id(label)}"
        nodes.append({
            "id": node_id,
            "type": "external_device",
            "label": label,
            "position": {"x": 330 + index * 260, "y": 40},
            "params": {
                "role": "external_hardware",
            },
        })
        preferred_driver = None
        for group in ("spi_master", "esp_lcd", "tinyusb", "camera", "i2c", "i2s", "uart", "gpio"):
            if group in group_node_ids:
                preferred_driver = group_node_ids[group]
                break
        if preferred_driver:
            edges.append({
                "id": f"{preferred_driver}_{node_id}",
                "from": f"{preferred_driver}.out",
                "to": f"{node_id}.in",
                "label": "drives" if "LCD" in label or "USB" in label else "connects",
            })

    if not api_groups:
        nodes.append({
            "id": "unmapped_function",
            "type": "unmapped_function",
            "label": "Unmapped Function",
            "position": {"x": 330, "y": 220},
            "params": {"reason": "No known functional block detected"},
        })
        edges.append({
            "id": "intent_unmapped",
            "from": "system_intent.out",
            "to": "unmapped_function.in",
            "label": "unknown",
        })
    return nodes, edges


def import_idf_example_project(example_id: str) -> dict[str, Any]:
    inventory = read_inventory_json(inventory_path("sdk_inventory/esp-idf-v5.5-esp32p4.json"))
    examples = inventory.get("examples", [])
    example = next((item for item in examples if item.get("id") == example_id), None)
    if not isinstance(example, dict):
        raise ValueError(f"example_not_found:{example_id}")
    project_id = f"idf_{slugify_project_id(example_id)}"
    path = project_path_for_id(project_id)
    nodes, edges = graph_nodes_for_idf_example(example)
    project = {
        "schema_version": "0.1",
        "id": project_id,
        "name": f"ESP-IDF: {example_id}",
        "status": "imported_sdk_example_read_only",
        "description": "Imported from the local ESP-IDF SDK inventory. Source files remain read-only references until graph-to-firmware generation is implemented.",
        "source": {
            "block": "esp_idf_example",
            "profile": None,
        },
        "processing": [
            {"block": group, "profile": None}
            for group in example.get("api_groups", [])
        ],
        "destination": {
            "block": "unassigned_destination",
            "profile": None,
        },
        "mcu_blocks": example.get("mcu_blocks", []),
        "graph": {
            "nodes": nodes,
            "edges": edges,
        },
        "production": {
            "build_script": None,
            "flash_script": None,
            "default_env": {},
            "known_good_command": None,
        },
        "sdk_example": {
            "inventory": "sdk_inventory/esp-idf-v5.5-esp32p4.json",
            "id": example.get("id"),
            "path": example.get("path"),
            "read_only": True,
            "categories": example.get("categories", []),
            "api_groups": example.get("api_groups", []),
            "components": example.get("components", []),
            "mcu_blocks": example.get("mcu_blocks", []),
            "source_files": example.get("source_files", []),
            "sdkconfig_defaults": example.get("sdkconfig_defaults", []),
            "cmake_files": example.get("cmake_files", []),
        },
        "evidence": [
            "docs/esp_idf_inventory_import_plan.md",
            "docs/sdk_inventory_artifacts.md",
        ],
        "known_limits": [
            "Imported project is read-only with respect to ESP-IDF source files.",
            "Graph is inferred from inventory metadata and may not capture full C semantics.",
            "Build/flash integration for imported SDK examples is not implemented yet.",
        ],
    }
    projects_dir().mkdir(parents=True, exist_ok=True)
    if path.exists():
        existing = json.loads(path.read_text(encoding="utf-8"))
        if isinstance(existing, dict):
            existing_status = str(existing.get("status", ""))
            existing_sdk = existing.get("sdk_example") if isinstance(existing.get("sdk_example"), dict) else {}
            if existing_status.startswith("imported_sdk_example") and existing_sdk.get("id") == example_id:
                path.write_text(json.dumps(project, indent=2, sort_keys=False) + "\n", encoding="utf-8")
            else:
                existing["_path"] = str(path.relative_to(project_root_dir()))
                return existing
    path.write_text(json.dumps(project, indent=2, sort_keys=False) + "\n", encoding="utf-8")
    project["_path"] = str(path.relative_to(project_root_dir()))
    return project


def delete_project_profile(project_id: str) -> dict[str, Any]:
    path = project_path_for_id(project_id)
    if not path.exists():
        raise ValueError("project_not_found")
    path.unlink()
    return {"ok": True, "id": project_id, "path": str(path.relative_to(project_root_dir()))}


def block_registry(profile: dict[str, Any], destination_profile: dict[str, Any]) -> list[dict[str, Any]]:
    return [
        {
            "id": "gbc_lcd_source",
            "name": "Profile Display-Bus Source",
            "kind": "source",
            "status": "validated",
            "profile": "profiles/gbc_lcd.json",
            "firmware_module": "firmware/main/gbc_lcd_source.c",
            "evidence": ["docs/protocol_discoveries.md", "docs/experiment_log.md"],
        },
        {
            "id": "source_capture",
            "name": "LCD_CAM Source Capture",
            "kind": "processing",
            "status": "active",
            "profile": str(profile.get("profile_id", "gbc_lcd")),
            "firmware_module": "firmware/main/lcdcam_raw.c",
            "evidence": ["docs/capture_pipeline.md"],
        },
        {
            "id": "spi_lcd_destination",
            "name": "Profile Display Destination",
            "kind": "destination",
            "status": str(destination_profile.get("status", "draft")),
            "profile": "profiles/spi_lcd_destination.json",
            "firmware_module": "firmware/main/destination_spi_lcd.c",
            "evidence": ["docs/destination_spi_lcd_lab.md"],
        },
        {
            "id": "production_mirror",
            "name": "Source-to-Destination Runtime",
            "kind": "transport",
            "status": "working_baseline",
            "firmware_module": "firmware/main/production_mirror.c",
            "evidence": ["docs/production_modes.md", "CODEX_HANDOFF.md"],
        },
    ]


def destination_gpio_rows(destination_profile: dict[str, Any]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for pin in destination_profile.get("connector", {}).get("pins", []):
        if not isinstance(pin, dict):
            continue
        gpio = pin.get("esp32p4_gpio")
        role = str(pin.get("role", ""))
        if isinstance(gpio, int) and role not in {"power", "ground", "backlight_power"}:
            rows.append({"signal": str(pin.get("name", "?")), "gpio": gpio, "role": role})
    return rows


def validate_project_profile(
    project: dict[str, Any],
    profile: dict[str, Any],
    destination_profile: dict[str, Any],
) -> dict[str, Any]:
    errors: list[str] = []
    warnings: list[str] = []
    source_rows = [
        {"signal": row["signal"], "gpio": row["gpio"]}
        for row in profile_gpio_rows(profile)
        if isinstance(row.get("gpio"), int)
    ]
    destination_rows = destination_gpio_rows(destination_profile)
    source_by_gpio = {row["gpio"]: row["signal"] for row in source_rows}
    seen_destination: dict[int, str] = {}
    for row in destination_rows:
        gpio = int(row["gpio"])
        if gpio in source_by_gpio:
            errors.append(f"destination {row['signal']} GPIO{gpio} conflicts with source {source_by_gpio[gpio]}")
        if gpio in seen_destination:
            errors.append(f"destination {row['signal']} duplicates GPIO{gpio} used by {seen_destination[gpio]}")
        seen_destination[gpio] = str(row["signal"])
    build_profiles = normalize_build_profiles(project)
    for profile_id in ("lab", "telemetry", "production"):
        profile_entry = build_profiles.get(profile_id)
        if not isinstance(profile_entry, dict) or not profile_entry.get("build_script") or not profile_entry.get("flash_script"):
            warnings.append(f"project has no complete {profile_id} build/flash script metadata")
    if not destination_rows:
        warnings.append("destination profile has no active output GPIOs")
    return {
        "ok": not errors,
        "project_id": project.get("id", ""),
        "errors": errors,
        "warnings": warnings,
        "source_gpios": source_rows,
        "destination_gpios": destination_rows,
    }


def run_project_script(project: dict[str, Any], action: str, build_profile: str, port: str | None = None) -> dict[str, Any]:
    profiles = normalize_build_profiles(project)
    selected = profiles.get(build_profile)
    if not isinstance(selected, dict):
        raise ValueError(f"unknown_build_profile:{build_profile}")
    script_key = "build_script" if action == "build" else "flash_script"
    script = selected.get(script_key)
    if not isinstance(script, str) or not script:
        raise ValueError(f"project_missing_{build_profile}_{script_key}")
    script_path = project_root_dir() / script
    if not script_path.exists():
        raise ValueError(f"script_not_found:{script}")
    env = dict(**{k: str(v) for k, v in selected.get("default_env", {}).items()}) if isinstance(selected.get("default_env"), dict) else {}
    command = [str(script_path)]
    if action == "flash":
        if not port:
            raise ValueError("serial_port_required")
        command.append(port)
    completed = subprocess.run(
        command,
        cwd=project_root_dir(),
        env={**dict(os.environ), **env},
        text=True,
        capture_output=True,
        timeout=180,
        check=False,
    )
    return {
        "ok": completed.returncode == 0,
        "project_id": project.get("id", ""),
        "action": action,
        "build_profile": build_profile,
        "command": command,
        "returncode": completed.returncode,
        "stdout": completed.stdout[-12000:],
        "stderr": completed.stderr[-12000:],
    }


def project_build_output_dir(project: dict[str, Any], build_profile: str) -> Path:
    profiles = normalize_build_profiles(project)
    selected = profiles.get(build_profile)
    if not isinstance(selected, dict):
        raise ValueError(f"unknown_build_profile:{build_profile}")
    default_env = selected.get("default_env")
    if isinstance(default_env, dict):
        for env_key in ("BUILD_DIR", "LAB_BUILD_DIR", "TELEMETRY_BUILD_DIR"):
            value = default_env.get(env_key)
            if isinstance(value, str) and value.strip():
                return project_root_dir() / "firmware" / value.strip()
    defaults = {
        "lab": "build_lab",
        "telemetry": "build_telemetry",
        "production": "build_production_mirror",
    }
    return project_root_dir() / "firmware" / defaults.get(build_profile, f"build_{build_profile}")


def load_flash_manifest(project: dict[str, Any], build_profile: str) -> dict[str, Any]:
    build_dir = project_build_output_dir(project, build_profile)
    flasher_args_path = build_dir / "flasher_args.json"
    if not flasher_args_path.exists():
        raise FileNotFoundError(f"flash_manifest_missing:{flasher_args_path}")
    raw = json.loads(flasher_args_path.read_text(encoding="utf-8"))
    flash_settings = raw.get("flash_settings", {})
    extra_args = raw.get("extra_esptool_args", {})
    flash_files = raw.get("flash_files", {})
    if not isinstance(flash_settings, dict) or not isinstance(extra_args, dict) or not isinstance(flash_files, dict):
        raise ValueError("invalid_flasher_args_json")
    images = []
    for address_text, relative_path in sorted(flash_files.items(), key=lambda item: int(str(item[0]), 0)):
        if not isinstance(relative_path, str) or not relative_path:
            continue
        artifact_path = (build_dir / relative_path).resolve()
        if not artifact_path.exists() or not artifact_path.is_file():
            raise FileNotFoundError(f"flash_image_missing:{artifact_path}")
        images.append({
            "address": int(str(address_text), 0),
            "relative_path": relative_path,
            "size": artifact_path.stat().st_size,
            "url": (
                f"/api/projects/artifact?id={project.get('id', '')}"
                f"&profile={build_profile}"
                f"&path={relative_path}"
            ),
        })
    return {
        "ok": True,
        "project_id": project.get("id", ""),
        "project_name": project.get("name", ""),
        "build_profile": build_profile,
        "chip": str(extra_args.get("chip") or "esp32p4"),
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "build_dir": str(build_dir),
        "before": str(extra_args.get("before") or "default_reset"),
        "after": str(extra_args.get("after") or "hard_reset"),
        "flash_mode": str(flash_settings.get("flash_mode") or "dio"),
        "flash_freq": str(flash_settings.get("flash_freq") or "80m"),
        "flash_size": str(flash_settings.get("flash_size") or "keep"),
        "reset_strategy": "usb_jtag_serial",
        "images": images,
    }


def inventory_path(name: str) -> Path:
    return project_root_dir() / name


def read_inventory_json(path: Path) -> dict[str, Any]:
    if not path.exists():
        raise FileNotFoundError(f"inventory_not_found:{path}")
    return json.loads(path.read_text(encoding="utf-8"))


def compact_sdk_example(example: dict[str, Any]) -> dict[str, Any]:
    return {
        "id": example.get("id", ""),
        "name": example.get("name", ""),
        "path": example.get("path", ""),
        "category_path": example.get("category_path", ""),
        "relevance": example.get("relevance", "track"),
        "categories": example.get("categories", []),
        "api_groups": example.get("api_groups", []),
        "components": example.get("components", []),
        "mcu_blocks": example.get("mcu_blocks", []),
        "source_file_count": example.get("source_file_count", 0),
        "sdkconfig_defaults": example.get("sdkconfig_defaults", []),
        "import_status": example.get("import_status", "candidate"),
    }


def filter_sdk_examples(examples: list[dict[str, Any]], query: dict[str, list[str]]) -> list[dict[str, Any]]:
    category = (query.get("category") or [""])[0].strip().lower()
    relevance = (query.get("relevance") or [""])[0].strip().lower()
    api_group = (query.get("api_group") or [""])[0].strip().lower()
    mcu_block = (query.get("mcu_block") or [""])[0].strip().lower()
    search = (query.get("q") or [""])[0].strip().lower()
    filtered = []
    for example in examples:
        haystack = " ".join(
            [
                str(example.get("id", "")),
                str(example.get("path", "")),
                " ".join(str(item) for item in example.get("categories", [])),
                " ".join(str(item) for item in example.get("api_groups", [])),
                " ".join(str(item) for item in example.get("mcu_blocks", [])),
                " ".join(str(item) for item in example.get("components", [])),
            ]
        ).lower()
        if category and category not in [str(item).lower() for item in example.get("categories", [])]:
            continue
        if relevance and relevance != str(example.get("relevance", "")).lower():
            continue
        if api_group and api_group not in [str(item).lower() for item in example.get("api_groups", [])]:
            continue
        if mcu_block and mcu_block not in [str(item).lower() for item in example.get("mcu_blocks", [])]:
            continue
        if search and search not in haystack:
            continue
        filtered.append(example)
    return filtered


def compact_espressif_repo(repo: dict[str, Any]) -> dict[str, Any]:
    return {
        "name": repo.get("name", ""),
        "full_name": repo.get("full_name", ""),
        "html_url": repo.get("html_url", ""),
        "description": repo.get("description", ""),
        "language": repo.get("language", ""),
        "topics": repo.get("topics", []),
        "stars": repo.get("stars", 0),
        "forks": repo.get("forks", 0),
        "archived": repo.get("archived", False),
        "categories": repo.get("categories", []),
        "relevance": repo.get("relevance", "track"),
    }


def make_handler(
    state: LiveLcdcamState,
    interval_ms: int,
    continuous_capture: bool,
    profile: dict[str, Any],
    destination_profile: dict[str, Any],
    destination_profile_path: str,
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
    source_gpio_owners = {
        int(row["gpio"]): str(row["signal"])
        for row in profile_gpio_list
        if isinstance(row.get("gpio"), int)
    }
    allowed_line_markers = {
        row["signal"]
        for row in profile_gpio_list
        if "line_marker_candidate" in row.get("role", "")
    } or {"LP", "SPL"}
    projects = load_project_profiles()
    blocks = block_registry(profile, destination_profile)

    def destination_signal(query: dict[str, list[str]]) -> str:
        signal_name = (query.get("signal") or [""])[0].strip()
        if not signal_name or any(ch not in "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_/-" for ch in signal_name):
            raise ValueError("invalid_signal")
        return signal_name

    def destination_command(command: str) -> dict[str, Any]:
        return state.probe_command(command)

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

        def do_POST(self) -> None:
            parsed = urlparse(self.path)
            path = parsed.path
            if path in {"/api/projects/create", "/api/projects/save", "/api/projects/duplicate", "/api/projects/delete"}:
                action = path.rsplit("/", 1)[-1]
                try:
                    length = int(self.headers.get("Content-Length", "0"))
                    if length <= 0 or length > 128 * 1024:
                        raise ValueError("invalid_content_length")
                    payload = json.loads(self.rfile.read(length).decode("utf-8"))
                    if action == "create":
                        project = create_project_profile(payload)
                    elif action == "save":
                        project = save_project_profile(payload)
                    elif action == "duplicate":
                        project = duplicate_project_profile(load_project_profiles(), payload)
                    else:
                        project_id = str(payload.get("id") or "").strip()
                        if project_id == "gbc_spi_lcd_mirror" and payload.get("confirm") != project_id:
                            raise ValueError("confirm_required_for_gbc_project")
                        result = delete_project_profile(project_id)
                        self.send_body(200, "application/json", json.dumps({
                            **result,
                            "projects": [public_project(item) for item in load_project_profiles()],
                        }).encode("utf-8"))
                        return
                    self.send_body(200, "application/json", json.dumps({
                        "ok": True,
                        "project": public_project(project),
                        "projects": [public_project(item) for item in load_project_profiles()],
                    }).encode("utf-8"))
                except Exception as exc:
                    self.send_body(400, "application/json", json.dumps({"ok": False, "action": action, "error": str(exc)}).encode("utf-8"))
                return
            if path == "/api/projects/import-idf-example":
                try:
                    length = int(self.headers.get("Content-Length", "0"))
                    if length <= 0 or length > 8 * 1024:
                        raise ValueError("invalid_content_length")
                    payload = json.loads(self.rfile.read(length).decode("utf-8"))
                    example_id = str(payload.get("id") or payload.get("example_id") or "").strip()
                    if not example_id:
                        raise ValueError("example_id_required")
                    project = import_idf_example_project(example_id)
                    self.send_body(200, "application/json", json.dumps({
                        "ok": True,
                        "project": public_project(project),
                        "projects": [public_project(item) for item in load_project_profiles()],
                    }).encode("utf-8"))
                except Exception as exc:
                    self.send_body(400, "application/json", json.dumps({"ok": False, "action": "import-idf-example", "error": str(exc)}).encode("utf-8"))
                return
            if path in {"/api/projects/build", "/api/projects/flash"}:
                action = "build" if path.endswith("/build") else "flash"
                try:
                    length = int(self.headers.get("Content-Length", "0"))
                    if length <= 0 or length > 8 * 1024:
                        raise ValueError("invalid_content_length")
                    payload = json.loads(self.rfile.read(length).decode("utf-8"))
                    project_id = str(payload.get("id", "")).strip()
                    build_profile = str(payload.get("profile") or "production").strip() or "production"
                    project = find_project(load_project_profiles(), project_id)
                    validation = validate_project_profile(project, profile, destination_profile)
                    if not validation["ok"]:
                        self.send_body(400, "application/json", json.dumps({
                            "ok": False,
                            "project_id": project_id,
                            "action": action,
                            "build_profile": build_profile,
                            "error": "validation_failed",
                            "validation": validation,
                        }).encode("utf-8"))
                        return
                    if action == "flash":
                        state.release_serial_for_deploy()
                    result = run_project_script(project, action, build_profile, state.port if action == "flash" else None)
                    self.send_body(200 if result.get("ok") else 500, "application/json", json.dumps(result).encode("utf-8"))
                except Exception as exc:
                    self.send_body(500, "application/json", json.dumps({"ok": False, "action": action, "error": str(exc)}).encode("utf-8"))
                return
            if path == "/api/destination-profile":
                try:
                    length = int(self.headers.get("Content-Length", "0"))
                    if length <= 0 or length > 64 * 1024:
                        raise ValueError("invalid_content_length")
                    payload = json.loads(self.rfile.read(length).decode("utf-8"))
                    pin_mapping = payload.get("pin_mapping")
                    if not isinstance(pin_mapping, list):
                        raise ValueError("pin_mapping_required")
                    for pin in pin_mapping:
                        gpio = pin.get("esp32p4_gpio") if isinstance(pin, dict) else None
                        if gpio is not None and gpio in source_gpio_owners:
                            raise ValueError(f"gpio_owned_by_source:{source_gpio_owners[gpio]}")
                    saved = save_destination_profile(destination_profile_path, destination_profile, pin_mapping, payload.get("destination"))
                    self.send_body(200, "application/json", json.dumps({
                        "ok": True,
                        "profile": saved,
                        "path": destination_profile_path,
                    }).encode("utf-8"))
                except Exception as exc:
                    self.send_body(400, "application/json", json.dumps({"ok": False, "error": str(exc)}).encode("utf-8"))
                return
            self.send_body(404, "application/json", json.dumps({"ok": False, "error": "not_found"}).encode("utf-8"))

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
            if path == "/api/blocks":
                self.send_body(200, "application/json", json.dumps({"ok": True, "blocks": blocks}).encode("utf-8"))
                return
            if path == "/api/projects":
                self.send_body(200, "application/json", json.dumps({
                    "ok": True,
                    "projects": [public_project(project) for project in load_project_profiles()],
                }).encode("utf-8"))
                return
            if path == "/api/projects/validate":
                query = parse_qs(parsed.query)
                try:
                    project_id = (query.get("id") or [""])[0].strip()
                    project = find_project(projects, project_id)
                    self.send_body(200, "application/json", json.dumps(
                        validate_project_profile(project, profile, destination_profile)
                    ).encode("utf-8"))
                except Exception as exc:
                    self.send_body(400, "application/json", json.dumps({"ok": False, "error": str(exc)}).encode("utf-8"))
                return
            if path == "/api/sdk/idf":
                try:
                    inventory = read_inventory_json(inventory_path("sdk_inventory/esp-idf-v5.5-esp32p4.json"))
                    self.send_body(200, "application/json", json.dumps({
                        "ok": True,
                        "schema": inventory.get("schema"),
                        "generated_at": inventory.get("generated_at"),
                        "source": inventory.get("source", {}),
                        "summary": inventory.get("summary", {}),
                        "classification": inventory.get("classification", {}),
                    }).encode("utf-8"))
                except Exception as exc:
                    self.send_body(404, "application/json", json.dumps({"ok": False, "error": str(exc)}).encode("utf-8"))
                return
            if path == "/api/sdk/components":
                try:
                    inventory = read_inventory_json(inventory_path("sdk_inventory/esp-idf-v5.5-esp32p4.json"))
                    components = inventory.get("components", [])
                    self.send_body(200, "application/json", json.dumps({
                        "ok": True,
                        "count": len(components),
                        "components": components,
                    }).encode("utf-8"))
                except Exception as exc:
                    self.send_body(404, "application/json", json.dumps({"ok": False, "error": str(exc)}).encode("utf-8"))
                return
            if path == "/api/sdk/examples" or path.startswith("/api/sdk/examples/"):
                try:
                    inventory = read_inventory_json(inventory_path("sdk_inventory/esp-idf-v5.5-esp32p4.json"))
                    examples = inventory.get("examples", [])
                    query = parse_qs(parsed.query)
                    example_id = (query.get("id") or [""])[0].strip()
                    if path.startswith("/api/sdk/examples/"):
                        example_id = unquote(path.removeprefix("/api/sdk/examples/")).strip()
                    if example_id:
                        example = next((item for item in examples if item.get("id") == example_id), None)
                        if example is None:
                            raise FileNotFoundError(f"example_not_found:{example_id}")
                        self.send_body(200, "application/json", json.dumps({
                            "ok": True,
                            "example": example,
                        }).encode("utf-8"))
                        return
                    filtered = filter_sdk_examples(examples, query)
                    limit = min(max(int((query.get("limit") or ["200"])[0]), 1), 1000)
                    self.send_body(200, "application/json", json.dumps({
                        "ok": True,
                        "count": len(filtered),
                        "examples": [compact_sdk_example(item) for item in filtered[:limit]],
                    }).encode("utf-8"))
                except Exception as exc:
                    self.send_body(404, "application/json", json.dumps({"ok": False, "error": str(exc)}).encode("utf-8"))
                return
            if path == "/api/research/espressif/repos":
                try:
                    inventory = read_inventory_json(inventory_path("inventories/espressif_github_repositories.json"))
                    query = parse_qs(parsed.query)
                    repos = inventory.get("repositories", [])
                    relevance = (query.get("relevance") or [""])[0].strip().lower()
                    category = (query.get("category") or [""])[0].strip().lower()
                    search = (query.get("q") or [""])[0].strip().lower()
                    filtered = []
                    for repo in repos:
                        haystack = " ".join([
                            str(repo.get("name", "")),
                            str(repo.get("full_name", "")),
                            str(repo.get("description", "")),
                            " ".join(str(item) for item in repo.get("categories", [])),
                            " ".join(str(item) for item in repo.get("topics", [])),
                        ]).lower()
                        if relevance and relevance != str(repo.get("relevance", "")).lower():
                            continue
                        if category and category not in [str(item).lower() for item in repo.get("categories", [])]:
                            continue
                        if search and search not in haystack:
                            continue
                        filtered.append(repo)
                    limit = min(max(int((query.get("limit") or ["200"])[0]), 1), 1000)
                    self.send_body(200, "application/json", json.dumps({
                        "ok": True,
                        "generated_at": inventory.get("generated_at"),
                        "summary": inventory.get("summary", {}),
                        "count": len(filtered),
                        "repositories": [compact_espressif_repo(item) for item in filtered[:limit]],
                    }).encode("utf-8"))
                except Exception as exc:
                    self.send_body(404, "application/json", json.dumps({"ok": False, "error": str(exc)}).encode("utf-8"))
                return
            if path == "/api/destination-profile":
                self.send_body(200, "application/json", json.dumps(destination_profile).encode("utf-8"))
                return
            if path == "/api/destination/gpio/status":
                try:
                    self.send_body(200, "application/json", json.dumps(destination_command("DEST_GPIO_STATUS")).encode("utf-8"))
                except Exception as exc:
                    self.send_body(500, "application/json", json.dumps({"ok": False, "error": str(exc)}).encode("utf-8"))
                return
            if path == "/api/destination/gpio/validate":
                query = parse_qs(parsed.query)
                try:
                    signal_name = destination_signal(query)
                    gpio = query_int(query, "gpio", -1, 0, 54)
                    if gpio in source_gpio_owners:
                        raise ValueError(f"gpio_owned_by_source:{source_gpio_owners[gpio]}")
                    self.send_body(200, "application/json", json.dumps(
                        destination_command(f"DEST_GPIO_VALIDATE {signal_name} {gpio}")
                    ).encode("utf-8"))
                except Exception as exc:
                    self.send_body(400, "application/json", json.dumps({"ok": False, "error": str(exc)}).encode("utf-8"))
                return
            if path == "/api/destination/gpio/claim":
                query = parse_qs(parsed.query)
                try:
                    signal_name = destination_signal(query)
                    gpio = query_int(query, "gpio", -1, 0, 54)
                    if gpio in source_gpio_owners:
                        raise ValueError(f"gpio_owned_by_source:{source_gpio_owners[gpio]}")
                    self.send_body(200, "application/json", json.dumps(
                        destination_command(f"DEST_GPIO_CLAIM {signal_name} {gpio}")
                    ).encode("utf-8"))
                except Exception as exc:
                    self.send_body(400, "application/json", json.dumps({"ok": False, "error": str(exc)}).encode("utf-8"))
                return
            if path == "/api/destination/gpio/set":
                query = parse_qs(parsed.query)
                try:
                    signal_name = destination_signal(query)
                    level = query_int(query, "level", -1, 0, 1)
                    self.send_body(200, "application/json", json.dumps(
                        destination_command(f"DEST_GPIO_SET {signal_name} {level}")
                    ).encode("utf-8"))
                except Exception as exc:
                    self.send_body(400, "application/json", json.dumps({"ok": False, "error": str(exc)}).encode("utf-8"))
                return
            if path == "/api/destination/gpio/pulse":
                query = parse_qs(parsed.query)
                try:
                    signal_name = destination_signal(query)
                    level = query_int(query, "level", 0, 0, 1)
                    duration_ms = query_int(query, "duration_ms", 100, 1, 5000)
                    self.send_body(200, "application/json", json.dumps(
                        destination_command(f"DEST_GPIO_PULSE {signal_name} {level} {duration_ms}")
                    ).encode("utf-8"))
                except Exception as exc:
                    self.send_body(400, "application/json", json.dumps({"ok": False, "error": str(exc)}).encode("utf-8"))
                return
            if path == "/api/destination/gpio/release":
                query = parse_qs(parsed.query)
                try:
                    signal_name = destination_signal(query)
                    self.send_body(200, "application/json", json.dumps(
                        destination_command(f"DEST_GPIO_RELEASE {signal_name}")
                    ).encode("utf-8"))
                except Exception as exc:
                    self.send_body(400, "application/json", json.dumps({"ok": False, "error": str(exc)}).encode("utf-8"))
                return
            if path == "/api/destination/spi/status":
                try:
                    self.send_body(200, "application/json", json.dumps(destination_command("DEST_SPI_LCD_STATUS")).encode("utf-8"))
                except Exception as exc:
                    self.send_body(500, "application/json", json.dumps({"ok": False, "error": str(exc)}).encode("utf-8"))
                return
            if path == "/api/destination/spi/init":
                try:
                    self.send_body(200, "application/json", json.dumps(destination_command("DEST_SPI_LCD_INIT")).encode("utf-8"))
                except Exception as exc:
                    self.send_body(500, "application/json", json.dumps({"ok": False, "error": str(exc)}).encode("utf-8"))
                return
            if path == "/api/destination/spi/safe-off":
                try:
                    self.send_body(200, "application/json", json.dumps(destination_command("DEST_SPI_LCD_SAFE_OFF")).encode("utf-8"))
                except Exception as exc:
                    self.send_body(500, "application/json", json.dumps({"ok": False, "error": str(exc)}).encode("utf-8"))
                return
            if path == "/api/destination/spi/test-pattern":
                query = parse_qs(parsed.query)
                try:
                    pattern = (query.get("pattern") or ["orientation"])[0].strip()
                    if pattern not in {"orientation", "color_bars"}:
                        raise ValueError("invalid_pattern")
                    self.send_body(200, "application/json", json.dumps(destination_command(f"DEST_SPI_LCD_TEST_PATTERN {pattern}")).encode("utf-8"))
                except Exception as exc:
                    self.send_body(500, "application/json", json.dumps({"ok": False, "error": str(exc)}).encode("utf-8"))
                return
            if path == "/api/destination/spi/test-pattern565":
                try:
                    self.send_body(200, "application/json", json.dumps(destination_command("DEST_SPI_LCD_TEST_PATTERN565 color_bars")).encode("utf-8"))
                except Exception as exc:
                    self.send_body(500, "application/json", json.dumps({"ok": False, "error": str(exc)}).encode("utf-8"))
                return
            if path == "/api/destination/spi/show-gbc-frame":
                query = parse_qs(parsed.query)
                try:
                    timeout_ms = query_int(query, "timeout_ms", 300, 1, 5000)
                    self.send_body(200, "application/json", json.dumps(destination_command(f"DEST_SPI_LCD_SHOW_GBC_FRAME {timeout_ms} 0")).encode("utf-8"))
                except Exception as exc:
                    self.send_body(500, "application/json", json.dumps({"ok": False, "error": str(exc)}).encode("utf-8"))
                return
            if path == "/api/destination/spi/mirror-bench":
                query = parse_qs(parsed.query)
                try:
                    frames = query_int(query, "frames", 10, 1, 120)
                    timeout_ms = query_int(query, "timeout_ms", 300, 1, 5000)
                    self.send_body(200, "application/json", json.dumps(destination_command(f"DEST_SPI_LCD_MIRROR_BENCH {frames} {timeout_ms} 0")).encode("utf-8"))
                except Exception as exc:
                    self.send_body(500, "application/json", json.dumps({"ok": False, "error": str(exc)}).encode("utf-8"))
                return
            if path == "/api/destination/spi/madctl":
                query = parse_qs(parsed.query)
                try:
                    value = (query.get("value") or ["08"])[0].strip()
                    if len(value) > 2 or any(ch not in "0123456789abcdefABCDEF" for ch in value):
                        raise ValueError("invalid_madctl_hex")
                    self.send_body(200, "application/json", json.dumps(destination_command(f"DEST_SPI_LCD_SET_MADCTL {value}")).encode("utf-8"))
                except Exception as exc:
                    self.send_body(400, "application/json", json.dumps({"ok": False, "error": str(exc)}).encode("utf-8"))
                return
            if path == "/api/destination/spi/signal-burst":
                query = parse_qs(parsed.query)
                try:
                    duration_ms = query_int(query, "duration_ms", 5000, 100, 15000)
                    self.send_body(200, "application/json", json.dumps(destination_command(f"DEST_SPI_LCD_SIGNAL_BURST {duration_ms}")).encode("utf-8"))
                except Exception as exc:
                    self.send_body(500, "application/json", json.dumps({"ok": False, "error": str(exc)}).encode("utf-8"))
                return
            if path == "/api/destination/spi/clear":
                query = parse_qs(parsed.query)
                try:
                    color = (query.get("color") or ["0000"])[0].strip()
                    if len(color) > 4 or any(ch not in "0123456789abcdefABCDEF" for ch in color):
                        raise ValueError("invalid_rgb565_hex")
                    self.send_body(200, "application/json", json.dumps(destination_command(f"DEST_SPI_LCD_CLEAR {color}")).encode("utf-8"))
                except Exception as exc:
                    self.send_body(400, "application/json", json.dumps({"ok": False, "error": str(exc)}).encode("utf-8"))
                return
            if path == "/api/destination/spi/clear565be":
                query = parse_qs(parsed.query)
                try:
                    color = (query.get("color") or ["0000"])[0].strip()
                    if len(color) > 4 or any(ch not in "0123456789abcdefABCDEF" for ch in color):
                        raise ValueError("invalid_rgb565_hex")
                    self.send_body(200, "application/json", json.dumps(destination_command(f"DEST_SPI_LCD_CLEAR565BE {color}")).encode("utf-8"))
                except Exception as exc:
                    self.send_body(400, "application/json", json.dumps({"ok": False, "error": str(exc)}).encode("utf-8"))
                return
            if path == "/api/destination/spi/clear666":
                query = parse_qs(parsed.query)
                try:
                    color = (query.get("color") or ["000000"])[0].strip()
                    if len(color) > 6 or any(ch not in "0123456789abcdefABCDEF" for ch in color):
                        raise ValueError("invalid_rgb888_hex")
                    self.send_body(200, "application/json", json.dumps(destination_command(f"DEST_SPI_LCD_CLEAR666 {color}")).encode("utf-8"))
                except Exception as exc:
                    self.send_body(400, "application/json", json.dumps({"ok": False, "error": str(exc)}).encode("utf-8"))
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
            if path == "/api/projects/flash-manifest":
                query = parse_qs(parsed.query)
                try:
                    project_id = (query.get("id") or [""])[0].strip()
                    build_profile = (query.get("profile") or ["production"])[0].strip() or "production"
                    project = find_project(load_project_profiles(), project_id)
                    manifest = load_flash_manifest(project, build_profile)
                    self.send_body(200, "application/json", json.dumps(manifest).encode("utf-8"))
                except Exception as exc:
                    self.send_body(400, "application/json", json.dumps({"ok": False, "error": str(exc)}).encode("utf-8"))
                return
            if path == "/api/projects/artifact":
                query = parse_qs(parsed.query)
                try:
                    project_id = (query.get("id") or [""])[0].strip()
                    build_profile = (query.get("profile") or ["production"])[0].strip() or "production"
                    relative_path = (query.get("path") or [""])[0].strip()
                    if not relative_path:
                        raise ValueError("artifact_path_required")
                    project = find_project(load_project_profiles(), project_id)
                    build_dir = project_build_output_dir(project, build_profile).resolve()
                    artifact_path = (build_dir / relative_path).resolve()
                    if not str(artifact_path).startswith(str(build_dir)):
                        raise ValueError("artifact_path_out_of_bounds")
                    if not artifact_path.exists() or not artifact_path.is_file():
                        raise FileNotFoundError(f"artifact_not_found:{relative_path}")
                    content_type = mimetypes.guess_type(artifact_path.name)[0] or "application/octet-stream"
                    self.send_body(200, content_type, artifact_path.read_bytes())
                except Exception as exc:
                    self.send_body(404, "application/json", json.dumps({"ok": False, "error": str(exc)}).encode("utf-8"))
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
            if path == "/api/serial/release":
                self.send_body(200, "application/json", json.dumps(state.release_serial_for_deploy()).encode("utf-8"))
                return
            if path == "/api/serial/reconnect":
                self.send_body(200, "application/json", json.dumps(state.reconnect_serial_after_deploy()).encode("utf-8"))
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
    if args.port:
        port = args.port
    else:
        try:
            port = autodetect_port()
        except Exception:
            port = "offline"
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
        make_handler(state, args.interval_ms, args.continuous_capture, profile, destination_profile, args.destination_profile),
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
