#!/usr/bin/env python3
"""Render RAW8 RG44 streams with per-row horizontal phase correction."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from render_dvp_raw import render_rg44


def deskew_rows(raw: bytes, width: int, height: int, shift_per_row: float, start_offset: int) -> bytes:
    output = bytearray(width * height)
    for y in range(height):
        shift = round(y * shift_per_row)
        row_start = start_offset + y * width
        for x in range(width):
            src_x = x + shift
            if 0 <= src_x < width:
                src = row_start + src_x
                if 0 <= src < len(raw):
                    output[y * width + x] = raw[src]
    return bytes(output)


def parse_shifts(text: str) -> list[float]:
    shifts: list[float] = []
    for item in text.split(","):
        item = item.strip()
        if item:
            shifts.append(float(item))
    return shifts


def run(args: argparse.Namespace) -> int:
    raw = args.input.read_bytes()
    output_dir = args.output_dir or args.input.parent
    output_dir.mkdir(parents=True, exist_ok=True)

    for width in args.widths:
        max_height = (len(raw) - args.start_offset) // width
        height = min(args.height, max_height) if args.height is not None else max_height
        if width <= 0 or height <= 0:
            continue
        for shift in args.shifts:
            corrected = deskew_rows(raw, width, height, shift, args.start_offset)
            shift_name = f"{shift:+.2f}".replace("+", "p").replace("-", "m").replace(".", "p")
            output = output_dir / f"{args.input.stem}_w{width}_h{height}_skew{shift_name}.png"
            render_rg44(corrected, width, height, output, invert=args.invert)
            print(f"width={width} height={height} shift_per_row={shift} output={output}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="RAW8 RG44 stream")
    parser.add_argument("--widths", type=lambda text: [int(v) for v in text.split(",")], default=[320])
    parser.add_argument("--height", type=int)
    parser.add_argument("--start-offset", type=int, default=0)
    parser.add_argument("--shifts", type=parse_shifts, default=parse_shifts("-1.0,-0.75,-0.5,-0.25,0,0.25,0.5,0.75,1.0"))
    parser.add_argument("--invert", action="store_true")
    parser.add_argument("--output-dir", type=Path)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if args.start_offset < 0:
        print("error: --start-offset must be >= 0", file=sys.stderr)
        return 2
    if args.height is not None and args.height <= 0:
        print("error: --height must be positive", file=sys.stderr)
        return 2
    try:
        return run(args)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
