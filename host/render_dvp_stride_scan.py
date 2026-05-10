#!/usr/bin/env python3
"""Render DVP RAW8 captures with alternate stream stride and offset hypotheses."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from render_dvp_raw import render_rg44
from render_red_diagnostic import write_png_rgb


def reverse4(value: int) -> int:
    out = 0
    for bit in range(4):
        if value & (1 << bit):
            out |= 1 << (3 - bit)
    return out


def rg44(value: int, invert: bool, reverse_bits: bool) -> tuple[int, int, int]:
    red4 = value & 0x0F
    green4 = (value >> 4) & 0x0F
    if reverse_bits:
        red4 = reverse4(red4)
        green4 = reverse4(green4)
    if invert:
        red4 = 15 - red4
        green4 = 15 - green4
    return red4 * 17, green4 * 17, 0


def draw_window(
    raw: bytes,
    out_width: int,
    out_height: int,
    stride: int,
    offset: int,
    invert: bool,
    reverse_bits: bool,
) -> bytes:
    pixels = bytearray(out_width * out_height * 3)
    for y in range(out_height):
        row_base = offset + y * stride
        for x in range(out_width):
            src = row_base + x
            if src < 0 or src >= len(raw):
                red8, green8, blue8 = 0, 0, 40
            else:
                red8, green8, blue8 = rg44(raw[src], invert, reverse_bits)
            dst = (y * out_width + x) * 3
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


def write_contact_sheet(args: argparse.Namespace, raw: bytes) -> Path:
    variants: list[tuple[int, int]] = []
    for stride in args.strides:
        for offset in args.offsets:
            variants.append((stride, offset))

    tile_w = args.width * args.scale
    tile_h = args.height * args.scale
    gap = args.gap
    cols = args.columns
    rows = (len(variants) + cols - 1) // cols
    sheet_width = cols * tile_w + (cols - 1) * gap
    sheet_height = rows * tile_h + (rows - 1) * gap
    sheet = bytearray(sheet_width * sheet_height * 3)

    for index, (stride, offset) in enumerate(variants):
        tile = draw_window(raw, args.width, args.height, stride, offset, args.invert, args.reverse_bits)
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

    output = args.output or args.input.with_name(args.input.stem + "_stride_scan.png")
    write_png_rgb(output, sheet_width, sheet_height, bytes(sheet))
    print(f"input={args.input}")
    print(f"output={output}")
    for index, (stride, offset) in enumerate(variants):
        print(f"  {index}: stride={stride} offset={offset}")
    return output


def write_single(args: argparse.Namespace, raw: bytes) -> Path:
    output = args.output or args.input.with_name(
        f"{args.input.stem}_stride{args.single_stride}_offset{args.single_offset}.png"
    )
    tile = draw_window(
        raw,
        args.width,
        args.height,
        args.single_stride,
        args.single_offset,
        args.invert,
        args.reverse_bits,
    )
    write_png_rgb(output, args.width, args.height, tile)
    print(f"input={args.input}")
    print(f"output={output}")
    print(f"stride={args.single_stride}")
    print(f"offset={args.single_offset}")
    return output


def parse_int_list(value: str) -> list[int]:
    items = []
    for part in value.split(","):
        part = part.strip()
        if not part:
            continue
        items.append(int(part, 0))
    if not items:
        raise argparse.ArgumentTypeError("list must contain at least one integer")
    return items


def run(args: argparse.Namespace) -> int:
    raw = args.input.read_bytes()
    if args.mode == "single":
        write_single(args, raw)
    else:
        write_contact_sheet(args, raw)
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("--mode", choices=["sheet", "single"], default="sheet")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--width", type=int, default=160)
    parser.add_argument("--height", type=int, default=144)
    parser.add_argument("--strides", type=parse_int_list, default=parse_int_list("128,144,152,160,168,176,192,224"))
    parser.add_argument("--offsets", type=parse_int_list, default=parse_int_list("0,8,16,32"))
    parser.add_argument("--single-stride", type=int, default=160)
    parser.add_argument("--single-offset", type=int, default=0)
    parser.add_argument("--columns", type=int, default=4)
    parser.add_argument("--scale", type=int, default=1)
    parser.add_argument("--gap", type=int, default=6)
    parser.add_argument("--invert", action="store_true")
    parser.add_argument("--reverse-bits", action="store_true")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if args.width <= 0 or args.height <= 0:
        print("error: dimensions must be positive", file=sys.stderr)
        return 2
    if args.scale <= 0 or args.columns <= 0 or args.gap < 0:
        print("error: scale/columns/gap values are invalid", file=sys.stderr)
        return 2
    try:
        return run(args)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
