#!/usr/bin/env python3
"""Capture CPU-polled RGB666 line bursts and render a true-color hypothesis PNG."""

from __future__ import annotations

import argparse
import json
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from gbc_probe import DEFAULT_BAUD, ProbeClient, autodetect_port
from render_red_diagnostic import write_png_rgb


def expand6(value: int) -> int:
    return max(0, min(255, round((value & 0x3F) * 255 / 63)))


def render_rgb666(raw: bytes, width: int, height: int, output: Path) -> None:
    expected = width * height * 3
    if len(raw) < expected:
        raise ValueError(f"short RGB666 data: {len(raw)} bytes, expected {expected}")
    pixels = bytearray(width * height * 3)
    for index in range(width * height):
        src = index * 3
        dst = index * 3
        pixels[dst] = expand6(raw[src])
        pixels[dst + 1] = expand6(raw[src + 1])
        pixels[dst + 2] = expand6(raw[src + 2])
    write_png_rgb(output, width, height, bytes(pixels))


def strip_data_hex(data: dict[str, Any]) -> tuple[bytes, dict[str, Any]]:
    data_hex = data.get("data_hex")
    if not isinstance(data_hex, str):
        raise ValueError(data.get("error") or "response did not include data_hex")
    metadata = dict(data)
    metadata.pop("data_hex", None)
    return bytes.fromhex(data_hex), metadata


def run(args: argparse.Namespace) -> int:
    port = args.port or autodetect_port()
    output_dir = args.output_dir
    output_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    prefix = output_dir / f"{stamp}-rgb666_line_bursts_{args.width}x{args.height}"

    command = (
        f"CAPTURE_RGB666_LINE_BURSTS {args.width} {args.height} {args.capture_timeout_ms} "
        f"{args.edge} {args.marker} {args.skip_markers} {args.dclk_delay_edges} "
        f"{args.marker_stride} {args.marker_phase} {int(args.stop_on_next_frame)}"
    )

    client = ProbeClient(port, args.baud, args.timeout)
    try:
        client.drain_startup()
        response = client.command(command)
    finally:
        client.close()

    raw, metadata = strip_data_hex(response.data)
    raw_path = prefix.with_suffix(".rgb666")
    json_path = prefix.with_suffix(".json")
    png_path = prefix.with_suffix(".png")

    raw_path.write_bytes(raw)
    json_path.write_text(json.dumps(metadata, indent=2, sort_keys=True), encoding="utf-8")
    render_rgb666(raw, args.width, args.height, png_path)

    print(json.dumps({
        "ok": metadata.get("ok", False),
        "command": command,
        "raw": str(raw_path),
        "metadata": str(json_path),
        "png": str(png_path),
        "captured_lines": metadata.get("captured_lines"),
        "checksum": metadata.get("checksum"),
        "transition_count": metadata.get("transition_count"),
    }, sort_keys=True))
    return 0 if metadata.get("ok") else 1


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="serial port, for example /dev/cu.usbmodem14401")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--width", type=int, default=160)
    parser.add_argument("--height", type=int, default=144)
    parser.add_argument("--capture-timeout-ms", type=int, default=2500)
    parser.add_argument("--edge", choices=["rising", "falling"], default="rising")
    parser.add_argument("--marker", choices=["SPL", "LP"], default="SPL")
    parser.add_argument("--skip-markers", type=int, default=0)
    parser.add_argument("--dclk-delay-edges", type=int, default=0)
    parser.add_argument("--marker-stride", type=int, default=1)
    parser.add_argument("--marker-phase", type=int, default=0)
    parser.add_argument("--stop-on-next-frame", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--output-dir", type=Path, default=Path("captures/decoded/rgb666_line_bursts"))
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

