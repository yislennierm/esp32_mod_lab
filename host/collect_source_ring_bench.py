#!/usr/bin/env python3
"""Collect source-ring benchmark JSON evidence from isolated ESP32-P4 firmware."""

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
SOURCE_RING_SCHEMA = "esp32_mod_lab.benchmark.source_ring.v1"
SOURCE_RING_COMMANDS = {
    "SOURCE_RING_BENCH_AUTO",
    "SOURCE_RING_CTLR_BENCH_AUTO",
    "SOURCE_RING_LOWLEVEL_BENCH_AUTO",
    "SOURCE_RING_LOWLEVEL_BENCH",
}


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
    return (
        data.get("schema") == SOURCE_RING_SCHEMA
        and data.get("command") in SOURCE_RING_COMMANDS
    )


def summarize_records(records: list[dict[str, Any]]) -> dict[str, Any]:
    native_records = [
        record
        for record in records
        if record.get("command") == "SOURCE_RING_LOWLEVEL_BENCH_AUTO"
        and int(record.get("capture_width", 0) or 0) == 160
        and int(record.get("capture_height", 0) or 0) == 144
    ]
    best_native = max(
        native_records,
        key=lambda record: float(record.get("completed_fps", 0.0) or 0.0),
        default=None,
    )
    best_any = max(
        records,
        key=lambda record: float(record.get("completed_fps", 0.0) or 0.0),
        default=None,
    )
    return {
        "schema": "esp32_mod_lab.host.source_ring_evidence.v1",
        "record_count": len(records),
        "native_visible_record_count": len(native_records),
        "best_native_visible": best_native,
        "best_any": best_any,
    }


def write_report(path: Path, metadata: dict[str, Any], summary: dict[str, Any]) -> None:
    best_native = summary.get("best_native_visible")
    best_any = summary.get("best_any")
    lines = [
        "# Source Ring Benchmark Evidence",
        "",
        "## Metadata",
        "",
        f"- Collected at: `{metadata['collected_at']}`",
        f"- Port: `{metadata['port']}`",
        f"- Baud: `{metadata['baud']}`",
        f"- Timeout: `{metadata['timeout_s']} s`",
        "",
        "## Summary",
        "",
        f"- Records collected: `{summary['record_count']}`",
        f"- Native visible `160x144` low-level records: `{summary['native_visible_record_count']}`",
    ]
    if best_native is not None:
        lines.extend(
            [
                f"- Best native visible FPS: `{best_native.get('completed_fps')}`",
                f"- Native visible target met: `{best_native.get('target_rate_met')}`",
                f"- Native visible drops: `{best_native.get('dropped_frames')}`",
                f"- Native visible DMA errors: `{best_native.get('dma_errors')}`",
                f"- Native visible avg capture: `{best_native.get('avg_capture_us')} us`",
            ]
        )
    if best_any is not None and best_any is not best_native:
        lines.append(f"- Best overall FPS: `{best_any.get('completed_fps')}`")
    lines.extend(
        [
            "",
            "## Interpretation",
            "",
            "This artifact proves the isolated firmware reported source-ingress",
            "benchmark results to the computer over serial. It is a counters-only",
            "evidence capture and does not depend on the browser live view.",
            "",
        ]
    )
    path.write_text("\n".join(lines), encoding="utf-8")


def collect(args: argparse.Namespace) -> int:
    port = args.port or autodetect_port()
    run_slug = timestamp_slug()
    output_dir = args.output_dir / f"{run_slug}-source-ring-bench"
    output_dir.mkdir(parents=True, exist_ok=False)

    lock_file = acquire_port_lock(port)
    raw_log_path = output_dir / "raw_serial.log"
    records_path = output_dir / "records.jsonl"
    summary_path = output_dir / "summary.json"
    report_path = output_dir / "report.md"

    records: list[dict[str, Any]] = []
    all_json: list[dict[str, Any]] = []
    deadline = time.monotonic() + args.timeout_s
    target_native_records = args.native_records

    try:
        with serial.Serial(port, baudrate=args.baud, timeout=0.2) as ser:
            if args.reset_input:
                ser.reset_input_buffer()
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
                    summary = summarize_records(records)
                    if (
                        summary["native_visible_record_count"]
                        >= target_native_records
                    ):
                        break
    finally:
        fcntl.flock(lock_file.fileno(), fcntl.LOCK_UN)
        lock_file.close()

    if not records:
        raise CollectorError(
            f"no source-ring benchmark records found before timeout; "
            f"raw log: {raw_log_path}"
        )

    summary = summarize_records(records)
    metadata = {
        "collected_at": run_slug,
        "port": port,
        "baud": args.baud,
        "timeout_s": args.timeout_s,
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

    best_native = summary.get("best_native_visible")
    if best_native is None:
        print(
            f"collected {len(records)} records, but no native 160x144 record; "
            f"artifact: {output_dir}"
        )
        return 2

    print(
        "native source-ring evidence: "
        f"{best_native.get('completed_fps')} fps, "
        f"target_met={best_native.get('target_rate_met')}, "
        f"drops={best_native.get('dropped_frames')}, "
        f"dma_errors={best_native.get('dma_errors')}"
    )
    print(f"artifact: {output_dir}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="Serial port. Autodetects if omitted.")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--timeout-s", type=float, default=DEFAULT_TIMEOUT_S)
    parser.add_argument(
        "--native-records",
        type=int,
        default=1,
        help="Stop after this many native visible 160x144 low-level records.",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("captures/benchmarks/source_ring"),
    )
    parser.add_argument("--echo", action="store_true", help="Print records live.")
    parser.add_argument(
        "--reset-input",
        action=argparse.BooleanOptionalAction,
        default=False,
        help="Discard pending serial input before collecting.",
    )
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        return collect(args)
    except (CollectorError, serial.SerialException) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
