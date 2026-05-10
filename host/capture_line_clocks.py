#!/usr/bin/env python3
"""Capture DCLK counts between LP/SPL marker edges."""

from __future__ import annotations

import argparse
import csv
import collections
import json
import statistics
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from gbc_probe import DEFAULT_BAUD, ProbeClient, autodetect_port


def utc_stamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def write_samples_csv(path: Path, samples: list[dict[str, Any]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=["index", "t_us", "dclk_delta", "dclk_total"])
        writer.writeheader()
        for sample in samples:
            writer.writerow(
                {
                    "index": sample.get("index"),
                    "t_us": sample.get("t_us"),
                    "dclk_delta": sample.get("dclk_delta"),
                    "dclk_total": sample.get("dclk_total"),
                }
            )


def summarize_response(data: dict[str, Any]) -> dict[str, Any]:
    samples = data.get("samples", [])
    deltas = [int(sample["dclk_delta"]) for sample in samples if "dclk_delta" in sample]
    nonzero_deltas = [delta for delta in deltas if delta > 0]
    histogram = collections.Counter(deltas)
    intervals = []
    previous_t = None
    for sample in samples:
        current_t = int(sample.get("t_us", 0))
        if previous_t is not None:
            intervals.append(current_t - previous_t)
        previous_t = current_t

    return {
        "ok": data.get("ok"),
        "marker": data.get("marker"),
        "marker_edge": data.get("marker_edge"),
        "sample_count": data.get("sample_count"),
        "frame_sync_seen": data.get("frame_sync_seen"),
        "timeout": data.get("timeout"),
        "dclk_delta_min": min(deltas) if deltas else None,
        "dclk_delta_max": max(deltas) if deltas else None,
        "dclk_delta_mean": statistics.fmean(deltas) if deltas else None,
        "dclk_delta_nonzero_mean": statistics.fmean(nonzero_deltas) if nonzero_deltas else None,
        "dclk_delta_values": sorted(set(deltas)),
        "dclk_delta_histogram": {str(key): histogram[key] for key in sorted(histogram)},
        "interval_us_min": min(intervals) if intervals else None,
        "interval_us_max": max(intervals) if intervals else None,
        "interval_us_mean": statistics.fmean(intervals) if intervals else None,
    }


def write_markdown(path: Path, summary: dict[str, Any], artifacts: dict[str, Path]) -> None:
    lines = [
        "# Line Clock Capture",
        "",
        f"Raw JSON: `{artifacts['json']}`",
        f"Samples CSV: `{artifacts['csv']}`",
        "",
        "## Summary",
        "",
    ]
    for key, value in summary.items():
        lines.append(f"- {key}: {value}")
    lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8")


def run(args: argparse.Namespace) -> int:
    port = args.port or autodetect_port()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    stamp = utc_stamp()
    prefix = args.output_dir / f"{stamp}-line_clocks_{args.marker.lower()}_{args.edge}"
    json_path = prefix.with_suffix(".json")
    csv_path = prefix.with_suffix(".csv")
    summary_path = args.output_dir / f"{prefix.name}_summary.json"
    md_path = args.output_dir / f"{prefix.name}_summary.md"

    client = ProbeClient(port, args.baud, args.timeout)
    try:
        client.drain_startup()
        response = client.command(
            f"CAPTURE_LINE_CLOCKS {args.marker} {args.edge} {args.lines} {args.capture_timeout_ms}"
        )
    finally:
        client.close()

    data = response.data
    json_path.write_text(json.dumps(data, indent=2, sort_keys=True), encoding="utf-8")
    write_samples_csv(csv_path, data.get("samples", []))

    summary = summarize_response(data)
    summary["raw_json"] = str(json_path)
    summary["samples_csv"] = str(csv_path)
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True), encoding="utf-8")
    write_markdown(md_path, summary, {"json": json_path, "csv": csv_path})

    print(
        f"ok={data.get('ok')} marker={data.get('marker')} edge={data.get('marker_edge')} "
        f"samples={data.get('sample_count')} frame_sync_seen={data.get('frame_sync_seen')} "
        f"delta_mean={summary['dclk_delta_mean']} values={summary['dclk_delta_values']}"
    )
    print(f"raw_json={json_path}")
    print(f"samples_csv={csv_path}")
    print(f"summary_json={summary_path}")
    print(f"summary_md={md_path}")
    return 0 if data.get("ok") else 1


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="serial port, for example /dev/cu.usbmodem14401")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--marker", choices=["LP", "SPL"], default="LP")
    parser.add_argument("--edge", choices=["falling", "rising"], default="falling")
    parser.add_argument("--lines", type=int, default=180)
    parser.add_argument("--capture-timeout-ms", type=int, default=1000)
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("captures/decoded/line_clocks"),
    )
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if args.lines <= 0 or args.lines > 256:
        print("error: --lines must be between 1 and 256", file=sys.stderr)
        return 2
    if args.capture_timeout_ms <= 0 or args.capture_timeout_ms > 5000:
        print("error: --capture-timeout-ms must be between 1 and 5000", file=sys.stderr)
        return 2
    try:
        return run(args)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
