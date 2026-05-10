#!/usr/bin/env python3
"""Capture one experimental ISP-DVP frame and render a diagnostic PNG."""

from __future__ import annotations

import argparse
import json
import sys
from datetime import datetime, timezone
from pathlib import Path

from gbc_probe import DEFAULT_BAUD, ProbeClient, autodetect_port
from render_dvp_raw import render_rg44
from render_red_diagnostic import write_png_rgb


def utc_stamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def render_rgb565(raw: bytes, width: int, height: int, output: Path) -> None:
    expected_len = width * height * 2
    if len(raw) != expected_len:
        raise ValueError(f"expected {expected_len} bytes, got {len(raw)}")

    pixels = bytearray(width * height * 3)
    for index in range(width * height):
        value = raw[index * 2] | (raw[index * 2 + 1] << 8)
        red = (value >> 11) & 0x1F
        green = (value >> 5) & 0x3F
        blue = value & 0x1F
        offset = index * 3
        pixels[offset] = round(red * 255 / 31)
        pixels[offset + 1] = round(green * 255 / 63)
        pixels[offset + 2] = round(blue * 255 / 31)

    write_png_rgb(output, width, height, bytes(pixels))


def run(args: argparse.Namespace) -> int:
    port = args.port or autodetect_port()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    stamp = utc_stamp()
    command_name = "ISP_DVP_CAPTURE_RGB565" if args.format == "rgb565" else "ISP_DVP_CAPTURE_RAW"
    prefix = args.output_dir / (
        f"{stamp}-isp_dvp_{args.format}_h{args.hsync.lower()}_de{args.de.lower()}_{args.width}x{args.height}"
    )
    json_path = prefix.with_suffix(".json")
    raw_path = prefix.with_suffix(".bin")
    png_path = prefix.with_suffix(".png")
    summary_path = args.output_dir / f"{prefix.name}_summary.json"

    client = ProbeClient(port, args.baud, args.timeout)
    try:
        client.drain_startup()
        response = client.command(
            f"{command_name} {args.hsync} {args.de} {args.capture_timeout_ms} "
            f"{int(args.hsync_invert)} {int(args.vsync_invert)} "
            f"{int(args.de_invert)} {int(args.pclk_invert)} "
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
    raw = bytes.fromhex(data["data_hex"])
    expected_len = width * height * (2 if args.format == "rgb565" else 1)
    if len(raw) != expected_len:
        raise ValueError(f"expected {expected_len} bytes, got {len(raw)}")
    raw_path.write_bytes(raw)
    if args.format == "rgb565":
        render_rgb565(raw, width, height, png_path)
    else:
        render_rg44(raw, width, height, png_path, invert=False)

    summary = dict(data)
    summary.pop("data_hex", None)
    summary["raw_bin"] = str(raw_path)
    summary["png"] = str(png_path)
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True), encoding="utf-8")

    print(
        f"ok=true hsync={data.get('hsync')} de={data.get('de')} size={width}x{height} "
        f"format={args.format} checksum={data.get('checksum')} raw8_transitions={data.get('raw8_transitions')}"
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
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--hsync", choices=["SPL", "LP", "NC"], default="LP")
    parser.add_argument("--de", choices=["SPL", "LP", "NC"], default="SPL")
    parser.add_argument("--format", choices=["raw8", "rgb565"], default="raw8")
    parser.add_argument("--hsync-invert", action=argparse.BooleanOptionalAction, default=False)
    parser.add_argument("--vsync-invert", action=argparse.BooleanOptionalAction, default=False)
    parser.add_argument("--de-invert", action=argparse.BooleanOptionalAction, default=False)
    parser.add_argument("--pclk-invert", action=argparse.BooleanOptionalAction, default=False)
    parser.add_argument("--capture-timeout-ms", type=int, default=2500)
    parser.add_argument("--width", type=int, default=160)
    parser.add_argument("--height", type=int, default=144)
    parser.add_argument("--output-dir", type=Path, default=Path("captures/decoded/isp_dvp_raw"))
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
