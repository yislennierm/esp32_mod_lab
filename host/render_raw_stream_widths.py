#!/usr/bin/env python3
"""Render one RAW8 RG44 stream using multiple candidate row widths."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from render_dvp_raw import render_rg44


def parse_widths(text: str) -> list[int]:
    widths: list[int] = []
    for item in text.split(","):
        item = item.strip()
        if not item:
            continue
        widths.append(int(item, 0))
    return widths


def run(args: argparse.Namespace) -> int:
    raw = args.input.read_bytes()
    if args.limit_bytes is not None:
        raw = raw[: args.limit_bytes]

    output_dir = args.output_dir or args.input.parent
    output_dir.mkdir(parents=True, exist_ok=True)

    for width in args.widths:
        if width <= 0:
            raise ValueError("candidate widths must be positive")
        height = len(raw) // width
        if args.max_height is not None:
            height = min(height, args.max_height)
        usable_len = width * height
        if usable_len == 0:
            continue
        output = output_dir / f"{args.input.stem}_stream_w{width}_h{height}.png"
        render_rg44(raw[:usable_len], width, height, output, invert=args.invert)
        print(f"width={width} height={height} bytes={usable_len} output={output}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="RAW8 RG44 .bin stream")
    parser.add_argument(
        "--widths",
        type=parse_widths,
        default=parse_widths("128,144,152,154,160,192,256,320"),
        help="comma-separated candidate row widths",
    )
    parser.add_argument("--limit-bytes", type=int)
    parser.add_argument("--max-height", type=int)
    parser.add_argument("--invert", action="store_true")
    parser.add_argument("--output-dir", type=Path)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if args.limit_bytes is not None and args.limit_bytes <= 0:
        print("error: --limit-bytes must be positive", file=sys.stderr)
        return 2
    if args.max_height is not None and args.max_height <= 0:
        print("error: --max-height must be positive", file=sys.stderr)
        return 2
    try:
        return run(args)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
