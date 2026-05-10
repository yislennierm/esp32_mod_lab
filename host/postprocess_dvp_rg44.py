#!/usr/bin/env python3
"""Post-process DVP RAW8 red/green diagnostic captures with shift hypotheses."""

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


def decode_rg44(value: int, invert: bool, reverse_bits: bool, swap_channels: bool) -> tuple[int, int, int]:
    red4 = value & 0x0F
    green4 = (value >> 4) & 0x0F
    if reverse_bits:
        red4 = reverse4(red4)
        green4 = reverse4(green4)
    if invert:
        red4 = 15 - red4
        green4 = 15 - green4
    if swap_channels:
        red4, green4 = green4, red4
    return red4 * 17, green4 * 17, 0


def transformed_pixels(
    raw: bytes,
    width: int,
    height: int,
    x_shift: int,
    y_shift: int,
    line_shift: int,
    invert: bool,
    reverse_bits: bool,
    swap_channels: bool,
) -> bytes:
    expected = width * height
    if len(raw) != expected:
        raise ValueError(f"expected {expected} bytes for {width}x{height}, got {len(raw)}")

    pixels = bytearray(width * height * 3)
    for y in range(height):
        src_y = (y + y_shift) % height
        per_line_shift = x_shift + src_y * line_shift
        for x in range(width):
            src_x = (x + per_line_shift) % width
            value = raw[src_y * width + src_x]
            red8, green8, blue8 = decode_rg44(value, invert, reverse_bits, swap_channels)
            dst = (y * width + x) * 3
            pixels[dst] = red8
            pixels[dst + 1] = green8
            pixels[dst + 2] = blue8
    return bytes(pixels)


def blit_scaled(
    sheet: bytearray,
    sheet_width: int,
    tile: bytes,
    tile_width: int,
    tile_height: int,
    x0: int,
    y0: int,
    scale: int,
) -> None:
    for y in range(tile_height):
        for x in range(tile_width):
            src = (y * tile_width + x) * 3
            for yy in range(scale):
                for xx in range(scale):
                    dst_x = x0 + x * scale + xx
                    dst_y = y0 + y * scale + yy
                    dst = (dst_y * sheet_width + dst_x) * 3
                    sheet[dst : dst + 3] = tile[src : src + 3]


def parse_int_list(value: str) -> list[int]:
    result = []
    for item in value.split(","):
        item = item.strip()
        if item:
            result.append(int(item, 0))
    if not result:
        raise argparse.ArgumentTypeError("expected at least one integer")
    return result


def run_single(args: argparse.Namespace, raw: bytes) -> Path:
    output = args.output or args.input.with_name(
        f"{args.input.stem}_x{args.x_shift}_y{args.y_shift}_line{args.line_shift}.png"
    )
    pixels = transformed_pixels(
        raw,
        args.width,
        args.height,
        args.x_shift,
        args.y_shift,
        args.line_shift,
        args.invert,
        args.reverse_bits,
        args.swap_channels,
    )
    write_png_rgb(output, args.width, args.height, pixels)
    print(f"output={output}")
    return output


def run_sheet(args: argparse.Namespace, raw: bytes) -> Path:
    variants = []
    for line_shift in args.line_shifts:
        for x_shift in args.x_shifts:
            variants.append((x_shift, args.y_shift, line_shift))

    tile_w = args.width * args.scale
    tile_h = args.height * args.scale
    gap = args.gap
    cols = args.columns
    rows = (len(variants) + cols - 1) // cols
    sheet_width = cols * tile_w + (cols - 1) * gap
    sheet_height = rows * tile_h + (rows - 1) * gap
    sheet = bytearray(sheet_width * sheet_height * 3)

    for index, (x_shift, y_shift, line_shift) in enumerate(variants):
        tile = transformed_pixels(
            raw,
            args.width,
            args.height,
            x_shift,
            y_shift,
            line_shift,
            args.invert,
            args.reverse_bits,
            args.swap_channels,
        )
        col = index % cols
        row = index // cols
        blit_scaled(
            sheet,
            sheet_width,
            tile,
            args.width,
            args.height,
            col * (tile_w + gap),
            row * (tile_h + gap),
            args.scale,
        )

    output = args.output or args.input.with_name(f"{args.input.stem}_postprocess_sheet.png")
    write_png_rgb(output, sheet_width, sheet_height, bytes(sheet))
    print(f"output={output}")
    for index, (x_shift, y_shift, line_shift) in enumerate(variants):
        print(f"  {index}: x_shift={x_shift} y_shift={y_shift} line_shift={line_shift}")
    return output


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("--mode", choices=["single", "sheet"], default="sheet")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--width", type=int, default=160)
    parser.add_argument("--height", type=int, default=144)
    parser.add_argument("--x-shift", type=int, default=0)
    parser.add_argument("--y-shift", type=int, default=0)
    parser.add_argument("--line-shift", type=int, default=0)
    parser.add_argument("--x-shifts", type=parse_int_list, default=parse_int_list("0,8,16,24"))
    parser.add_argument("--line-shifts", type=parse_int_list, default=parse_int_list("-4,-3,-2,-1,0,1,2,3,4"))
    parser.add_argument("--columns", type=int, default=4)
    parser.add_argument("--scale", type=int, default=1)
    parser.add_argument("--gap", type=int, default=6)
    parser.add_argument("--invert", action="store_true")
    parser.add_argument("--reverse-bits", action="store_true")
    parser.add_argument("--swap-channels", action="store_true")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if args.width <= 0 or args.height <= 0 or args.columns <= 0 or args.scale <= 0 or args.gap < 0:
        print("error: invalid dimensions/layout arguments", file=sys.stderr)
        return 2
    try:
        raw = args.input.read_bytes()
        if args.mode == "single":
            run_single(args, raw)
        else:
            run_sheet(args, raw)
        return 0
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
