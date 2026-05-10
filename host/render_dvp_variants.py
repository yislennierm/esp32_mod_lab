#!/usr/bin/env python3
"""Render a contact sheet of DVP RAW8 red/green interpretation variants."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from render_red_diagnostic import write_png_rgb


def reverse4(value: int) -> int:
    out = 0
    for bit in range(4):
        if value & (1 << bit):
            out |= 1 << (3 - bit)
    return out


def transform_rg44(value: int, invert: bool, reverse_bits: bool) -> tuple[int, int]:
    red4 = value & 0x0F
    green4 = (value >> 4) & 0x0F
    if reverse_bits:
        red4 = reverse4(red4)
        green4 = reverse4(green4)
    if invert:
        red4 = 15 - red4
        green4 = 15 - green4
    return red4 * 17, green4 * 17


def draw_variant(
    dest: bytearray,
    sheet_width: int,
    x0: int,
    y0: int,
    raw: bytes,
    width: int,
    height: int,
    scale: int,
    x_shift: int,
    y_shift: int,
    invert: bool,
    reverse_bits: bool,
) -> None:
    for y in range(height):
        src_y = (y + y_shift) % height
        for x in range(width):
            src_x = (x + x_shift) % width
            value = raw[src_y * width + src_x]
            red8, green8 = transform_rg44(value, invert, reverse_bits)
            for yy in range(scale):
                for xx in range(scale):
                    dx = x0 + x * scale + xx
                    dy = y0 + y * scale + yy
                    offset = (dy * sheet_width + dx) * 3
                    dest[offset] = red8
                    dest[offset + 1] = green8
                    dest[offset + 2] = 0


def run(args: argparse.Namespace) -> int:
    raw = args.input.read_bytes()
    expected = args.width * args.height
    if len(raw) != expected:
        raise ValueError(f"expected {expected} bytes, got {len(raw)}")

    variants = [
        (0, 0, False, False),
        (0, 0, True, False),
        (0, 0, False, True),
        (0, 0, True, True),
        (8, 0, False, False),
        (16, 0, False, False),
        (0, 1, False, False),
        (0, 2, False, False),
    ]

    tile_w = args.width * args.scale
    tile_h = args.height * args.scale
    gap = 8
    cols = 4
    rows = 2
    sheet_width = cols * tile_w + (cols - 1) * gap
    sheet_height = rows * tile_h + (rows - 1) * gap
    pixels = bytearray(sheet_width * sheet_height * 3)

    for index, (x_shift, y_shift, invert, reverse_bits) in enumerate(variants):
        col = index % cols
        row = index // cols
        draw_variant(
            pixels,
            sheet_width,
            col * (tile_w + gap),
            row * (tile_h + gap),
            raw,
            args.width,
            args.height,
            args.scale,
            x_shift,
            y_shift,
            invert,
            reverse_bits,
        )

    output = args.output or args.input.with_name(args.input.stem + "_variants.png")
    write_png_rgb(output, sheet_width, sheet_height, bytes(pixels))

    print(f"input={args.input}")
    print(f"output={output}")
    print("variants:")
    for index, (x_shift, y_shift, invert, reverse_bits) in enumerate(variants):
        print(f"  {index}: x_shift={x_shift} y_shift={y_shift} invert={invert} reverse_bits={reverse_bits}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--width", type=int, default=160)
    parser.add_argument("--height", type=int, default=144)
    parser.add_argument("--scale", type=int, default=2)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if args.width <= 0 or args.height <= 0 or args.scale <= 0:
        print("error: dimensions and scale must be positive", file=sys.stderr)
        return 2
    try:
        return run(args)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
