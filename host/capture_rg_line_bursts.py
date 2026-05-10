#!/usr/bin/env python3
"""Capture experimental SPL/DCLK red-green line bursts and render a PNG."""

from __future__ import annotations

import argparse
import json
import statistics
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from gbc_probe import DEFAULT_BAUD, ProbeClient, autodetect_port
from render_dvp_raw import render_rg44


def utc_stamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def raw_hex_to_bytes(data_hex: str, expected_len: int) -> bytes:
    data = bytes.fromhex(data_hex)
    if len(data) != expected_len:
        raise ValueError(f"expected {expected_len} bytes, got {len(data)}")
    return data


def summarize(data: dict[str, Any], raw_path: Path, png_path: Path) -> dict[str, Any]:
    line_counts = [int(value) for value in data.get("line_sample_counts", [])]
    return {
        "ok": data.get("ok"),
        "mode": data.get("mode"),
        "width": data.get("width"),
        "height": data.get("height"),
        "packing": data.get("packing"),
        "line_start": data.get("line_start"),
        "skip_markers": data.get("skip_markers"),
        "dclk_delay_edges": data.get("dclk_delay_edges"),
        "marker_stride": data.get("marker_stride"),
        "marker_phase": data.get("marker_phase"),
        "stop_on_next_frame": data.get("stop_on_next_frame"),
        "next_frame_seen": data.get("next_frame_seen"),
        "observed_markers": data.get("observed_markers"),
        "sample_clock_edge": data.get("sample_clock_edge"),
        "frame_sync_seen": data.get("frame_sync_seen"),
        "timeout": data.get("timeout"),
        "captured_lines": data.get("captured_lines"),
        "checksum": data.get("checksum"),
        "transition_count": data.get("transition_count"),
        "min_value": data.get("min_value"),
        "max_value": data.get("max_value"),
        "line_count_min": min(line_counts) if line_counts else None,
        "line_count_max": max(line_counts) if line_counts else None,
        "line_count_mean": statistics.fmean(line_counts) if line_counts else None,
        "complete_line_count": sum(1 for value in line_counts if value == data.get("width")),
        "raw_bin": str(raw_path),
        "png": str(png_path),
    }


def run(args: argparse.Namespace) -> int:
    port = args.port or autodetect_port()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    stamp = utc_stamp()
    prefix = args.output_dir / f"{stamp}-rg_line_bursts_{args.width}x{args.height}"
    json_path = prefix.with_suffix(".json")
    raw_path = prefix.with_suffix(".bin")
    png_path = prefix.with_suffix(".png")
    inverted_png_path = args.output_dir / f"{prefix.name}_inverted.png"
    summary_path = args.output_dir / f"{prefix.name}_summary.json"

    client = ProbeClient(port, args.baud, args.timeout)
    try:
        client.drain_startup()
        response = client.command(
            f"CAPTURE_RG_LINE_BURSTS {args.width} {args.height} {args.capture_timeout_ms} "
            f"{args.edge} {args.marker} {args.skip_markers} "
            f"{args.dclk_delay_edges} {args.marker_stride} {args.marker_phase} "
            f"{1 if args.single_frame else 0}"
        )
    finally:
        client.close()

    data = response.data
    json_path.write_text(json.dumps(data, indent=2, sort_keys=True), encoding="utf-8")

    if not data.get("ok"):
        print(json.dumps(data, sort_keys=True))
        print(f"raw_json={json_path}")
        return 1

    width = int(data["width"])
    height = int(data["height"])
    raw = raw_hex_to_bytes(data["data_hex"], width * height)
    raw_path.write_bytes(raw)
    render_rg44(raw, width, height, png_path, invert=False)
    render_rg44(raw, width, height, inverted_png_path, invert=True)

    summary = summarize(data, raw_path, png_path)
    summary["raw_json"] = str(json_path)
    summary["inverted_png"] = str(inverted_png_path)
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True), encoding="utf-8")

    print(
        f"ok=true size={width}x{height} marker={data.get('line_start')} "
        f"skip_markers={data.get('skip_markers')} dclk_delay={data.get('dclk_delay_edges')} "
        f"stride={data.get('marker_stride')} phase={data.get('marker_phase')} "
        f"single_frame={data.get('stop_on_next_frame')} next_frame={data.get('next_frame_seen')} "
        f"markers={data.get('observed_markers')} "
        f"edge={data.get('sample_clock_edge')} "
        f"frame_sync_seen={data.get('frame_sync_seen')} "
        f"timeout={data.get('timeout')} captured_lines={data.get('captured_lines')} "
        f"complete_lines={summary['complete_line_count']} checksum={data.get('checksum')} "
        f"transitions={data.get('transition_count')}"
    )
    print(f"raw_json={json_path}")
    print(f"raw_bin={raw_path}")
    print(f"png={png_path}")
    print(f"inverted_png={inverted_png_path}")
    print(f"summary_json={summary_path}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="serial port, for example /dev/cu.usbmodem14401")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--timeout", type=float, default=20.0)
    parser.add_argument("--width", type=int, default=160)
    parser.add_argument("--height", type=int, default=144)
    parser.add_argument("--capture-timeout-ms", type=int, default=1500)
    parser.add_argument("--edge", choices=["rising", "falling"], default="rising")
    parser.add_argument("--marker", choices=["SPL", "LP"], default="SPL")
    parser.add_argument("--skip-markers", type=int, default=0)
    parser.add_argument("--dclk-delay-edges", type=int, default=0)
    parser.add_argument("--marker-stride", type=int, default=1)
    parser.add_argument("--marker-phase", type=int, default=0)
    parser.add_argument(
        "--single-frame",
        action="store_true",
        help="stop at the next SPS rising edge instead of spanning frames to fill --height",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("captures/decoded/rg_line_bursts"),
    )
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if args.width <= 0 or args.width > 160:
        print("error: --width must be between 1 and 160", file=sys.stderr)
        return 2
    if args.height <= 0 or args.height > 144:
        print("error: --height must be between 1 and 144", file=sys.stderr)
        return 2
    if args.capture_timeout_ms <= 0 or args.capture_timeout_ms > 5000:
        print("error: --capture-timeout-ms must be between 1 and 5000", file=sys.stderr)
        return 2
    if args.skip_markers < 0 or args.skip_markers > 32:
        print("error: --skip-markers must be between 0 and 32", file=sys.stderr)
        return 2
    if args.dclk_delay_edges < 0 or args.dclk_delay_edges > 64:
        print("error: --dclk-delay-edges must be between 0 and 64", file=sys.stderr)
        return 2
    if args.marker_stride <= 0 or args.marker_stride > 16:
        print("error: --marker-stride must be between 1 and 16", file=sys.stderr)
        return 2
    if args.marker_phase < 0 or args.marker_phase >= args.marker_stride:
        print("error: --marker-phase must be between 0 and marker_stride - 1", file=sys.stderr)
        return 2
    try:
        return run(args)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
