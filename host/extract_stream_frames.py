#!/usr/bin/env python3
"""Extract candidate 160x144 frames from a RAW8 RG44 stream."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from render_dvp_raw import render_rg44


def parse_offsets(text: str) -> list[int]:
    values: list[int] = []
    for item in text.split(","):
        item = item.strip()
        if item:
            values.append(int(item, 0))
    return values


def extract_window(raw: bytes, stream_width: int, frame_width: int, frame_height: int, x: int, y: int) -> bytes:
    output = bytearray(frame_width * frame_height)
    for row in range(frame_height):
        src = ((y + row) * stream_width) + x
        if src < 0 or src + frame_width > len(raw):
            break
        output[row * frame_width : (row + 1) * frame_width] = raw[src : src + frame_width]
    return bytes(output)


def run(args: argparse.Namespace) -> int:
    raw = args.input.read_bytes()
    output_dir = args.output_dir or args.input.parent
    output_dir.mkdir(parents=True, exist_ok=True)

    max_rows = len(raw) // args.stream_width
    for y in args.y_offsets:
        for x in args.x_offsets:
            if y < 0 or x < 0 or x + args.frame_width > args.stream_width:
                continue
            if y + args.frame_height > max_rows:
                continue
            frame = extract_window(raw, args.stream_width, args.frame_width, args.frame_height, x, y)
            output = output_dir / (
                f"{args.input.stem}_sw{args.stream_width}_x{x}_y{y}_"
                f"{args.frame_width}x{args.frame_height}.png"
            )
            render_rg44(frame, args.frame_width, args.frame_height, output, invert=args.invert)
            print(f"x={x} y={y} output={output}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="RAW8 RG44 stream")
    parser.add_argument("--stream-width", type=int, default=160)
    parser.add_argument("--frame-width", type=int, default=160)
    parser.add_argument("--frame-height", type=int, default=144)
    parser.add_argument("--x-offsets", type=parse_offsets, default=[0])
    parser.add_argument("--y-offsets", type=parse_offsets, default=parse_offsets("0,24,48,72,96,120,144,168,192,216,240,264"))
    parser.add_argument("--invert", action="store_true")
    parser.add_argument("--output-dir", type=Path)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if args.stream_width <= 0 or args.frame_width <= 0 or args.frame_height <= 0:
        print("error: dimensions must be positive", file=sys.stderr)
        return 2
    try:
        return run(args)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
