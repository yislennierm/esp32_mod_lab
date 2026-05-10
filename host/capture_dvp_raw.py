#!/usr/bin/env python3
"""Capture one experimental DVP RAW8 frame and render a diagnostic PNG."""

from __future__ import annotations

import argparse
import json
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from gbc_probe import DEFAULT_BAUD, ProbeClient, autodetect_port
from render_dvp_raw import render_lower6_red, render_rg44


def utc_stamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def raw8_hex_to_bytes(data_hex: str, expected_len: int) -> bytes:
    data = bytes.fromhex(data_hex)
    if len(data) != expected_len:
        raise ValueError(f"expected {expected_len} bytes, got {len(data)}")
    return data


def write_summary(path: Path, response: dict[str, Any], raw_path: Path, png_path: Path) -> None:
    summary = dict(response)
    summary.pop("data_hex", None)
    summary["raw_bin"] = str(raw_path)
    summary["png"] = str(png_path)
    path.write_text(json.dumps(summary, indent=2, sort_keys=True), encoding="utf-8")


def run(args: argparse.Namespace) -> int:
    port = args.port or autodetect_port()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    stamp = utc_stamp()
    mode = "dvp_raw_len" if args.byte_count_eof else "dvp_raw"
    command_name = "DVP_CAPTURE_RAW_LEN" if args.byte_count_eof else "DVP_CAPTURE_RAW"
    prefix = args.output_dir / f"{stamp}-{mode}_{args.de.lower()}_{args.width}x{args.height}"
    json_path = prefix.with_suffix(".json")
    raw_path = prefix.with_suffix(".bin")
    png_path = prefix.with_suffix(".png")
    summary_path = args.output_dir / f"{prefix.name}_summary.json"

    client = ProbeClient(port, args.baud, args.timeout)
    try:
        client.drain_startup()
        response = client.command(
            f"{command_name} {args.de} {args.capture_timeout_ms} "
            f"{int(args.vsync_invert)} {int(args.de_invert)} {int(args.pclk_invert)} "
            f"{args.width} {args.height}"
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
    raw = raw8_hex_to_bytes(data["data_hex"], width * height)
    raw_path.write_bytes(raw)
    if data.get("packing") == "rg44_upper_bits":
        render_rg44(raw, width, height, png_path, invert=False)
    else:
        render_lower6_red(raw, width, height, png_path, invert=False)
    write_summary(summary_path, data, raw_path, png_path)

    print(
        f"ok=true de={data['de']} size={width}x{height} "
        f"byte_count_eof={data.get('byte_count_eof', False)} "
        f"checksum={data['checksum']} raw8_transitions={data.get('raw8_transitions', '-')}"
    )
    print(f"raw_json={json_path}")
    print(f"raw_bin={raw_path}")
    print(f"png={png_path}")
    print(f"summary_json={summary_path}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="serial port, for example /dev/cu.usbmodem14401")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--timeout", type=float, default=20.0)
    parser.add_argument("--de", choices=["SPL", "LP"], default="SPL")
    parser.add_argument("--vsync-invert", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--de-invert", action=argparse.BooleanOptionalAction, default=False)
    parser.add_argument("--pclk-invert", action=argparse.BooleanOptionalAction, default=False)
    parser.add_argument("--byte-count-eof", action="store_true")
    parser.add_argument("--capture-timeout-ms", type=int, default=1000)
    parser.add_argument("--width", type=int, default=160)
    parser.add_argument("--height", type=int, default=144)
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("captures/decoded/dvp_raw"),
    )
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if args.capture_timeout_ms <= 0 or args.capture_timeout_ms > 5000:
        print("error: --capture-timeout-ms must be between 1 and 5000", file=sys.stderr)
        return 2
    if args.width <= 0 or args.width > 320:
        print("error: --width must be between 1 and 320", file=sys.stderr)
        return 2
    if args.height <= 0 or args.height > 240:
        print("error: --height must be between 1 and 240", file=sys.stderr)
        return 2
    try:
        return run(args)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
