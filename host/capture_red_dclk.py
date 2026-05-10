#!/usr/bin/env python3
"""Capture red-bus samples on DCLK rising edges after an SPL trigger."""

from __future__ import annotations

import argparse
import csv
import json
import sys
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from gbc_probe import DEFAULT_BAUD, ProbeClient, autodetect_port


def utc_stamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def write_samples_csv(path: Path, samples: list[dict[str, Any]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=["index", "t_us", "red6"])
        writer.writeheader()
        writer.writerows(samples)


def summarize(data: dict[str, Any]) -> dict[str, Any]:
    samples = data.get("samples", [])
    values = [sample["red6"] for sample in samples]
    counts = Counter(values)
    transitions = 0
    for index in range(1, len(values)):
        if values[index] != values[index - 1]:
            transitions += 1

    return {
        "ok": data.get("ok", False),
        "trigger_seen": data.get("trigger_seen", False),
        "timeout": data.get("timeout", False),
        "requested_sample_count": data.get("requested_sample_count"),
        "sample_count": data.get("sample_count", len(samples)),
        "unique_red_values": len(counts),
        "red_value_counts": [
            {"red6": value, "count": count}
            for value, count in counts.most_common()
        ],
        "transitions": transitions,
        "first_samples": samples[:64],
    }


def print_summary(summary: dict[str, Any]) -> None:
    print(
        f"ok={summary['ok']} trigger_seen={summary['trigger_seen']} "
        f"timeout={summary['timeout']} samples={summary['sample_count']}/"
        f"{summary['requested_sample_count']}"
    )
    print(
        f"unique_red_values={summary['unique_red_values']} "
        f"transitions={summary['transitions']} "
        f"counts={summary['red_value_counts'][:16]}"
    )
    print("first samples:")
    for sample in summary["first_samples"]:
        print(f"  {sample['index']:>4} t_us={sample['t_us']:>6} red6=0x{sample['red6']:02x}")


def run(args: argparse.Namespace) -> int:
    port = args.port or autodetect_port()
    output_dir = args.output_dir
    output_dir.mkdir(parents=True, exist_ok=True)

    stamp = utc_stamp()
    raw_path = output_dir / f"{stamp}-red_dclk_{args.samples}samples.json"
    csv_path = output_dir / f"{stamp}-red_dclk_{args.samples}samples.csv"
    summary_path = output_dir / f"{stamp}-red_dclk_{args.samples}samples_summary.json"

    client = ProbeClient(port, args.baud, args.timeout)
    try:
        client.drain_startup()
        response = client.command(f"CAPTURE_RED_DCLK {args.samples} {args.capture_timeout_ms}")
    finally:
        client.close()

    data = response.data
    raw_path.write_text(json.dumps(data, indent=2, sort_keys=True), encoding="utf-8")
    write_samples_csv(csv_path, data.get("samples", []))

    summary = summarize(data)
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True), encoding="utf-8")
    print_summary(summary)
    print(f"raw_json={raw_path}")
    print(f"csv={csv_path}")
    print(f"summary_json={summary_path}")
    return 0 if summary["ok"] and summary["trigger_seen"] and not summary["timeout"] else 1


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="serial port, for example /dev/cu.usbmodem14401")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--samples", type=int, default=512)
    parser.add_argument("--capture-timeout-ms", type=int, default=100)
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("captures/decoded/red_dclk"),
    )
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if args.samples <= 0 or args.samples > 2048:
        print("error: --samples must be between 1 and 2048", file=sys.stderr)
        return 2
    if args.capture_timeout_ms <= 0 or args.capture_timeout_ms > 1000:
        print("error: --capture-timeout-ms must be between 1 and 1000", file=sys.stderr)
        return 2
    try:
        return run(args)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
