#!/usr/bin/env python3
"""Capture one private LCD_CAM/GDMA RAW8 diagnostic buffer."""

from __future__ import annotations

import argparse
import json
import sys
from datetime import datetime, timezone
from pathlib import Path

from gbc_probe import DEFAULT_BAUD, ProbeClient, autodetect_port
from render_dvp_raw import render_rg44, render_rgb332, render_rgb565, render_rgb664


def utc_stamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def run(args: argparse.Namespace) -> int:
    port = args.port or autodetect_port()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    stamp = utc_stamp()
    prefix = args.output_dir / f"{stamp}-lcdcam_raw_{args.de.lower()}_{args.width}x{args.height}"
    json_path = prefix.with_suffix(".json")
    raw_path = prefix.with_suffix(".bin")
    png_path = prefix.with_suffix(".png")
    summary_path = args.output_dir / f"{prefix.name}_summary.json"

    client = ProbeClient(port, args.baud, args.timeout)
    try:
        client.drain_startup()
        response = client.command(
            f"LCDCAM_RAW_CAPTURE {args.de} {args.capture_timeout_ms} "
            f"{int(args.vsync_invert)} {int(args.de_invert)} {int(args.pclk_invert)} "
            f"{int(args.byte_count_eof)} {args.width} {args.height} {args.start_mode} "
            f"{int(args.vh_de_mode)} {args.data_mode}"
        )
    finally:
        client.close()

    data = response.data
    json_path.write_text(json.dumps(data, indent=2, sort_keys=True), encoding="utf-8")

    data_hex = data.get("data_hex", "")
    raw = bytes.fromhex(data_hex) if data_hex else b""
    if raw:
        raw_path.write_bytes(raw)
        width = int(data["width"])
        height = int(data["height"])
        if data.get("packing") == "rgb664_r0_r5_g0_g5_b2_b5" and len(raw) == width * height * 2:
            render_rgb664(raw, width, height, png_path, invert=False)
        elif data.get("packing") == "rgb565_upper_bits_standard" and len(raw) == width * height * 2:
            render_rgb565(raw, width, height, png_path, invert=False)
        elif len(raw) == width * height:
            if data.get("packing") == "rgb332_upper_bits":
                render_rgb332(raw, width, height, png_path, invert=False)
            else:
                render_rg44(raw, width, height, png_path, invert=False)

    summary = dict(data)
    summary.pop("data_hex", None)
    if raw:
        summary["raw_bin"] = str(raw_path)
        if png_path.exists():
            summary["png"] = str(png_path)
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True), encoding="utf-8")

    print(
        f"ok={str(bool(data.get('ok'))).lower()} de={data.get('de')} "
        f"size={data.get('width')}x{data.get('height')} byte_count_eof={data.get('byte_count_eof')} "
        f"start_mode={data.get('start_mode')} start_trigger_seen={data.get('start_trigger_seen')} "
        f"vh_de_mode={data.get('vh_de_mode')} hsync={data.get('hsync')} "
        f"received_size={data.get('received_size')} descriptors={data.get('completed_descriptors')}/"
        f"{data.get('descriptor_count')} checksum={data.get('checksum')} "
        f"transitions={data.get('raw8_transitions')} failure_stage={data.get('failure_stage')}"
    )
    print(f"raw_json={json_path}")
    if raw:
        print(f"raw_bin={raw_path}")
    if png_path.exists():
        print(f"png={png_path}")
    print(f"summary_json={summary_path}")
    return 0 if data.get("ok") or raw else 1


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="serial port, for example /dev/cu.usbmodem14401")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--de", choices=["SPL", "LP", "HIGH"], default="SPL")
    parser.add_argument("--data-mode", choices=["RG44", "RGB332", "RGB664", "RGB565"], default="RG44")
    parser.add_argument("--vsync-invert", action=argparse.BooleanOptionalAction, default=False)
    parser.add_argument("--de-invert", action=argparse.BooleanOptionalAction, default=False)
    parser.add_argument("--pclk-invert", action=argparse.BooleanOptionalAction, default=False)
    parser.add_argument("--byte-count-eof", action=argparse.BooleanOptionalAction, default=False)
    parser.add_argument(
        "--start-mode",
        type=int,
        choices=[0, 1, 2],
        default=0,
        help="0=immediate, 1=after SPS rising, 2=after SPS rising then SPL falling",
    )
    parser.add_argument(
        "--vh-de-mode",
        action=argparse.BooleanOptionalAction,
        default=False,
        help="Enable LCD_CAM DE+HSYNC+VSYNC mode. The alternate line marker is routed as HSYNC.",
    )
    parser.add_argument("--capture-timeout-ms", type=int, default=2500)
    parser.add_argument("--width", type=int, default=160)
    parser.add_argument("--height", type=int, default=144)
    parser.add_argument("--output-dir", type=Path, default=Path("captures/decoded/lcdcam_raw"))
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
