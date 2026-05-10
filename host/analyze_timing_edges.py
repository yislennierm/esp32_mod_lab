#!/usr/bin/env python3
"""Capture and summarize Phase 1 timing edges from the ESP32-P4 probe."""

from __future__ import annotations

import argparse
import csv
import json
import statistics
import sys
from collections import Counter, defaultdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from gbc_probe import DEFAULT_BAUD, ProbeClient, autodetect_port


def write_events_csv(path: Path, events: list[dict[str, Any]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=["t_us", "signal", "gpio", "level", "red6"],
            extrasaction="ignore",
        )
        writer.writeheader()
        writer.writerows(events)


def intervals(values: list[int]) -> list[int]:
    return [values[i] - values[i - 1] for i in range(1, len(values))]


def summarize_capture(data: dict[str, Any]) -> dict[str, Any]:
    events = data.get("events", [])
    duration_ms = data.get("duration_ms", 0)
    duration_s = duration_ms / 1000.0 if duration_ms else 0.0

    by_signal: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for event in events:
        by_signal[event["signal"]].append(event)

    signal_summary = {}
    for signal, signal_events in sorted(by_signal.items()):
        level_counts = Counter(event["level"] for event in signal_events)
        event_count = len(signal_events)
        signal_intervals = intervals([event["t_us"] for event in signal_events])
        signal_summary[signal] = {
            "event_count": event_count,
            "event_rate_hz": event_count / duration_s if duration_s else 0.0,
            "level_0_edges": level_counts.get(0, 0),
            "level_1_edges": level_counts.get(1, 0),
            "mean_interval_us": statistics.fmean(signal_intervals) if signal_intervals else None,
            "min_interval_us": min(signal_intervals) if signal_intervals else None,
            "max_interval_us": max(signal_intervals) if signal_intervals else None,
        }

    sps_rising_times = [
        event["t_us"]
        for event in by_signal.get("SPS", [])
        if event.get("level") == 1
    ]
    sps_rising_intervals = intervals(sps_rising_times)

    first_events = events[:32]
    red_values = [event["red6"] for event in events if "red6" in event]
    red_counter = Counter(red_values)
    red_summary = {
        "sample_count": len(red_values),
        "unique_values": len(red_counter),
        "top_values": [
            {"red6": value, "count": count}
            for value, count in red_counter.most_common(16)
        ],
    }

    red_by_signal = {}
    for signal, signal_events in sorted(by_signal.items()):
        signal_red_values = [event["red6"] for event in signal_events if "red6" in event]
        signal_red_counter = Counter(signal_red_values)
        red_by_signal[signal] = {
            "sample_count": len(signal_red_values),
            "unique_values": len(signal_red_counter),
            "top_values": [
                {"red6": value, "count": count}
                for value, count in signal_red_counter.most_common(8)
            ],
        }

    return {
        "ok": data.get("ok", False),
        "duration_ms": duration_ms,
        "event_count": data.get("event_count", len(events)),
        "overflow_count": data.get("overflow_count", 0),
        "signal_summary": signal_summary,
        "sps_rising_intervals_us": sps_rising_intervals,
        "sps_rising_interval_mean_us": statistics.fmean(sps_rising_intervals)
        if sps_rising_intervals
        else None,
        "red_summary": red_summary,
        "red_by_signal": red_by_signal,
        "first_events": first_events,
    }


def print_summary(summary: dict[str, Any]) -> None:
    print(
        f"capture ok={summary['ok']} duration_ms={summary['duration_ms']} "
        f"events={summary['event_count']} overflow={summary['overflow_count']}"
    )
    for signal, row in summary["signal_summary"].items():
        print(
            f"{signal:>3}: events={row['event_count']:>5} "
            f"rate={row['event_rate_hz']:>9.1f} Hz "
            f"level0={row['level_0_edges']:>5} level1={row['level_1_edges']:>5} "
            f"interval_us mean={row['mean_interval_us']} "
            f"min={row['min_interval_us']} max={row['max_interval_us']}"
        )

    if summary["sps_rising_intervals_us"]:
        print(
            "SPS rising intervals us: "
            + ", ".join(str(value) for value in summary["sps_rising_intervals_us"])
        )
        print(f"SPS mean rising interval us: {summary['sps_rising_interval_mean_us']:.1f}")

    red_summary = summary.get("red_summary", {})
    if red_summary.get("sample_count"):
        print(
            f"red6: samples={red_summary['sample_count']} "
            f"unique={red_summary['unique_values']} "
            f"top={red_summary['top_values'][:8]}"
        )

    print("first events:")
    for event in summary["first_events"]:
        red_text = f" red6=0x{event['red6']:02x}" if "red6" in event else ""
        print(
            f"  {event['t_us']:>8} us {event['signal']:>3} "
            f"GPIO{event['gpio']} level={event['level']}{red_text}"
        )


def run(args: argparse.Namespace) -> int:
    port = args.port or autodetect_port()
    output_dir = args.output_dir
    output_dir.mkdir(parents=True, exist_ok=True)

    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    raw_path = output_dir / f"{stamp}-timing_edges_{args.duration_ms}ms.json"
    csv_path = output_dir / f"{stamp}-timing_edges_{args.duration_ms}ms.csv"
    summary_path = output_dir / f"{stamp}-timing_edges_{args.duration_ms}ms_summary.json"

    client = ProbeClient(port, args.baud, args.timeout)
    try:
        client.drain_startup()
        response = client.command(f"CAPTURE_TIMING_EDGES {args.duration_ms}")
    finally:
        client.close()

    data = response.data
    raw_path.write_text(json.dumps(data, indent=2, sort_keys=True), encoding="utf-8")
    write_events_csv(csv_path, data.get("events", []))

    summary = summarize_capture(data)
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True), encoding="utf-8")
    print_summary(summary)
    print(f"raw_json={raw_path}")
    print(f"csv={csv_path}")
    print(f"summary_json={summary_path}")
    return 0 if data.get("ok") and data.get("overflow_count", 1) == 0 else 1


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="serial port, for example /dev/cu.usbmodem14401")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--duration-ms", type=int, default=100)
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("captures/decoded/timing_edges"),
    )
    return parser


def main() -> int:
    args = build_parser().parse_args()
    try:
        return run(args)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
