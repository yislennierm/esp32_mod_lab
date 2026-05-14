#!/usr/bin/env python3
"""Collect PPA SRM benchmark JSON evidence from normal lab firmware."""

from __future__ import annotations

import argparse
import fcntl
import json
import re
import sys
import time
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

try:
    import serial
    import serial.tools.list_ports
except ImportError as exc:  # pragma: no cover - depends on host environment
    raise SystemExit(
        "pyserial is required. Use the ESP-IDF environment or install pyserial."
    ) from exc


DEFAULT_BAUD = 115200
DEFAULT_TIMEOUT_S = 30.0
DEFAULT_FRAMES = 120
PPA_SCHEMA = "esp32_mod_lab.benchmark.ppa_srm.v1"
PPA_COMMANDS = {"PPA_SRM_SCALE2X_BENCH", "CPU_SCALE2X_BENCH"}


class CollectorError(RuntimeError):
    """Raised when benchmark evidence cannot be collected."""


def timestamp_slug() -> str:
    return datetime.now(UTC).strftime("%Y%m%dT%H%M%SZ")


def lock_path_for_port(port: str) -> Path:
    safe_name = re.sub(r"[^A-Za-z0-9_.-]+", "_", port.strip("/"))
    lock_dir = Path("/tmp/gbc_p4_probe_locks")
    lock_dir.mkdir(parents=True, exist_ok=True)
    return lock_dir / f"{safe_name}.lock"


def acquire_port_lock(port: str):
    lock_path = lock_path_for_port(port)
    lock_file = lock_path.open("w")
    try:
        fcntl.flock(lock_file.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError as exc:
        lock_file.close()
        raise CollectorError(
            f"serial port {port} is already in use; stop monitor/UI first"
        ) from exc
    lock_file.write(f"{Path(sys.argv[0]).name} {time.time():.3f}\n")
    lock_file.flush()
    return lock_file


def list_ports() -> list[str]:
    return [port.device for port in serial.tools.list_ports.comports()]


def autodetect_port() -> str:
    ports = list_ports()
    preferred = [
        port for port in ports if "usbmodem" in port or "usbserial" in port
    ]
    if not preferred:
        raise CollectorError("no USB serial port found; pass --port explicitly")
    if len(preferred) > 1:
        raise CollectorError(
            "multiple USB serial ports found; pass --port explicitly: "
            + ", ".join(preferred)
        )
    return preferred[0]


def extract_json_object(line: str) -> dict[str, Any] | None:
    start = line.find("{")
    end = line.rfind("}")
    if start < 0 or end <= start:
        return None
    try:
        data = json.loads(line[start : end + 1])
    except json.JSONDecodeError:
        return None
    if not isinstance(data, dict):
        return None
    return data


def is_benchmark_record(data: dict[str, Any]) -> bool:
    return data.get("schema") == PPA_SCHEMA and data.get("command") in PPA_COMMANDS


def summarize_records(records: list[dict[str, Any]]) -> dict[str, Any]:
    by_command = {str(record.get("command")): record for record in records}
    ppa = by_command.get("PPA_SRM_SCALE2X_BENCH")
    cpu = by_command.get("CPU_SCALE2X_BENCH")
    ppa_fps = float(ppa.get("fps", 0.0) or 0.0) if ppa else 0.0
    cpu_fps = float(cpu.get("fps", 0.0) or 0.0) if cpu else 0.0
    speedup = ppa_fps / cpu_fps if ppa_fps > 0.0 and cpu_fps > 0.0 else None
    return {
        "schema": "esp32_mod_lab.host.ppa_srm_evidence.v1",
        "record_count": len(records),
        "ppa": ppa,
        "cpu": cpu,
        "ppa_fps": ppa_fps,
        "cpu_fps": cpu_fps,
        "ppa_vs_cpu_speedup": speedup,
        "both_records_collected": ppa is not None and cpu is not None,
    }


def write_report(path: Path, metadata: dict[str, Any], summary: dict[str, Any]) -> None:
    ppa = summary.get("ppa")
    cpu = summary.get("cpu")
    speedup = summary.get("ppa_vs_cpu_speedup")
    lines = [
        "# PPA SRM Benchmark Evidence",
        "",
        "## Metadata",
        "",
        f"- Collected at: `{metadata['collected_at']}`",
        f"- Port: `{metadata['port']}`",
        f"- Baud: `{metadata['baud']}`",
        f"- Command: `{metadata['command']}`",
        f"- Timeout: `{metadata['timeout_s']} s`",
        "",
        "## Summary",
        "",
        f"- Records collected: `{summary['record_count']}`",
        f"- Both PPA and CPU records collected: `{summary['both_records_collected']}`",
    ]
    if ppa is not None:
        lines.extend(
            [
                f"- PPA FPS: `{ppa.get('fps')}`",
                f"- PPA avg: `{ppa.get('avg_us')} us`",
                f"- PPA target met: `{ppa.get('target_rate_met')}`",
                f"- PPA error: `{ppa.get('error')}`",
            ]
        )
    if cpu is not None:
        lines.extend(
            [
                f"- CPU FPS: `{cpu.get('fps')}`",
                f"- CPU avg: `{cpu.get('avg_us')} us`",
                f"- CPU target met: `{cpu.get('target_rate_met')}`",
            ]
        )
    if speedup is not None:
        lines.append(f"- PPA/CPU speedup: `{speedup:.3f}x`")
    lines.extend(
        [
            "",
            "## Interpretation",
            "",
            "This artifact measures only synthetic RGB565 2x scaling inside the",
            "normal lab firmware. It does not include GBC source capture, SPI LCD",
            "output, USB frame transport, or browser rendering.",
            "",
        ]
    )
    path.write_text("\n".join(lines), encoding="utf-8")


def collect(args: argparse.Namespace) -> int:
    port = args.port or autodetect_port()
    run_slug = timestamp_slug()
    output_dir = args.output_dir / f"{run_slug}-ppa-srm-bench"
    output_dir.mkdir(parents=True, exist_ok=False)

    lock_file = acquire_port_lock(port)
    raw_log_path = output_dir / "raw_serial.log"
    records_path = output_dir / "records.jsonl"
    summary_path = output_dir / "summary.json"
    report_path = output_dir / "report.md"

    records: list[dict[str, Any]] = []
    all_json: list[dict[str, Any]] = []
    command = f"PPA_SRM_BENCH {args.frames}"
    deadline = time.monotonic() + args.timeout_s

    try:
        with serial.Serial(port, baudrate=args.baud, timeout=0.2) as ser:
            if args.reset_input:
                ser.reset_input_buffer()
            ser.write((command + "\n").encode("utf-8"))
            ser.flush()
            with raw_log_path.open("w", encoding="utf-8") as raw_log:
                while time.monotonic() < deadline:
                    line = ser.readline().decode("utf-8", "replace").rstrip()
                    if not line:
                        continue
                    raw_log.write(line + "\n")
                    raw_log.flush()
                    data = extract_json_object(line)
                    if data is None:
                        continue
                    all_json.append(data)
                    if is_benchmark_record(data):
                        records.append(data)
                        if args.echo:
                            print(json.dumps(data, sort_keys=True))
                    if summarize_records(records)["both_records_collected"]:
                        break
    finally:
        fcntl.flock(lock_file.fileno(), fcntl.LOCK_UN)
        lock_file.close()

    if not records:
        raise CollectorError(
            f"no PPA SRM benchmark records found before timeout; raw log: {raw_log_path}"
        )

    summary = summarize_records(records)
    metadata = {
        "collected_at": run_slug,
        "port": port,
        "baud": args.baud,
        "timeout_s": args.timeout_s,
        "command": command,
        "output_dir": str(output_dir),
        "raw_json_line_count": len(all_json),
    }
    with records_path.open("w", encoding="utf-8") as records_file:
        for record in records:
            records_file.write(json.dumps(record, sort_keys=True) + "\n")
    summary_doc = {
        "metadata": metadata,
        "summary": summary,
        "records": records,
    }
    summary_path.write_text(
        json.dumps(summary_doc, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    write_report(report_path, metadata, summary)

    if not summary["both_records_collected"]:
        print(f"partial PPA SRM evidence; artifact: {output_dir}")
        return 2

    print(
        "PPA SRM evidence: "
        f"ppa={summary['ppa_fps']:.3f} fps, "
        f"cpu={summary['cpu_fps']:.3f} fps, "
        f"speedup={summary['ppa_vs_cpu_speedup']:.3f}x"
    )
    print(f"artifact: {output_dir}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="Serial port. Autodetects if omitted.")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--timeout-s", type=float, default=DEFAULT_TIMEOUT_S)
    parser.add_argument("--frames", type=int, default=DEFAULT_FRAMES)
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("captures/benchmarks/ppa_srm"),
    )
    parser.add_argument("--echo", action="store_true", help="Print records live.")
    parser.add_argument(
        "--reset-input",
        action=argparse.BooleanOptionalAction,
        default=False,
        help="Discard pending serial input before sending the benchmark command.",
    )
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    if args.frames < 1 or args.frames > 1000:
        print("error: --frames must be in range 1..1000", file=sys.stderr)
        return 2
    try:
        return collect(args)
    except (CollectorError, serial.SerialException) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
