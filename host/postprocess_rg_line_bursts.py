#!/usr/bin/env python3
"""Post-process RG44 line-burst captures with decimation and tile hypotheses."""

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


def render_decimated(
    raw: bytes,
    width: int,
    height: int,
    x_step: int,
    y_step: int,
    x_phase: int,
    y_phase: int,
    out_width: int,
    out_height: int,
    invert: bool,
    reverse_bits: bool,
    swap_channels: bool,
) -> bytes:
    pixels = bytearray(out_width * out_height * 3)
    for y in range(out_height):
        src_y = y_phase + y * y_step
        if src_y >= height:
            src_y %= height
        for x in range(out_width):
            src_x = x_phase + x * x_step
            if src_x >= width:
                src_x %= width
            value = raw[src_y * width + src_x]
            rgb = decode_rg44(value, invert, reverse_bits, swap_channels)
            dst = (y * out_width + x) * 3
            pixels[dst : dst + 3] = bytes(rgb)
    return bytes(pixels)


def render_tile_scaled(
    raw: bytes,
    width: int,
    height: int,
    tile_x: int,
    tile_y: int,
    tile_w: int,
    tile_h: int,
    out_width: int,
    out_height: int,
    invert: bool,
    reverse_bits: bool,
    swap_channels: bool,
) -> bytes:
    pixels = bytearray(out_width * out_height * 3)
    for y in range(out_height):
        src_y = tile_y + min(tile_h - 1, (y * tile_h) // out_height)
        for x in range(out_width):
            src_x = tile_x + min(tile_w - 1, (x * tile_w) // out_width)
            if 0 <= src_x < width and 0 <= src_y < height:
                value = raw[src_y * width + src_x]
                rgb = decode_rg44(value, invert, reverse_bits, swap_channels)
            else:
                rgb = (0, 0, 40)
            dst = (y * out_width + x) * 3
            pixels[dst : dst + 3] = bytes(rgb)
    return bytes(pixels)


def blit(sheet: bytearray, sheet_width: int, tile: bytes, tile_width: int, tile_height: int, x0: int, y0: int) -> None:
    for y in range(tile_height):
        src = y * tile_width * 3
        dst = ((y0 + y) * sheet_width + x0) * 3
        sheet[dst : dst + tile_width * 3] = tile[src : src + tile_width * 3]


def parse_int_list(value: str) -> list[int]:
    result = []
    for item in value.split(","):
        item = item.strip()
        if item:
            result.append(int(item, 0))
    if not result:
        raise argparse.ArgumentTypeError("expected at least one integer")
    return result


def write_decimation_sheet(args: argparse.Namespace, raw: bytes) -> Path:
    variants = [(x_step, y_step) for y_step in args.y_steps for x_step in args.x_steps]
    tile_w = args.output_width
    tile_h = args.output_height
    cols = args.columns
    rows = (len(variants) + cols - 1) // cols
    sheet_w = cols * tile_w + (cols - 1) * args.gap
    sheet_h = rows * tile_h + (rows - 1) * args.gap
    sheet = bytearray(sheet_w * sheet_h * 3)

    for index, (x_step, y_step) in enumerate(variants):
        tile = render_decimated(
            raw,
            args.width,
            args.height,
            x_step,
            y_step,
            args.x_phase,
            args.y_phase,
            tile_w,
            tile_h,
            args.invert,
            args.reverse_bits,
            args.swap_channels,
        )
        col = index % cols
        row = index // cols
        blit(sheet, sheet_w, tile, tile_w, tile_h, col * (tile_w + args.gap), row * (tile_h + args.gap))

    output = args.output or args.input.with_name(args.input.stem + "_decimation_sheet.png")
    write_png_rgb(output, sheet_w, sheet_h, bytes(sheet))
    print(f"output={output}")
    for index, (x_step, y_step) in enumerate(variants):
        print(f"  {index}: x_step={x_step} y_step={y_step}")
    return output


def write_tile_sheet(args: argparse.Namespace, raw: bytes) -> Path:
    tile_w = args.width // args.tile_cols
    tile_h = args.height // args.tile_rows
    variants = []
    for row in range(args.tile_rows):
        for col in range(args.tile_cols):
            variants.append((col * tile_w, row * tile_h, tile_w, tile_h, col, row))

    out_w = args.output_width
    out_h = args.output_height
    cols = args.tile_cols
    rows = args.tile_rows
    sheet_w = cols * out_w + (cols - 1) * args.gap
    sheet_h = rows * out_h + (rows - 1) * args.gap
    sheet = bytearray(sheet_w * sheet_h * 3)

    for tile_x, tile_y, current_tile_w, current_tile_h, col, row in variants:
        tile = render_tile_scaled(
            raw,
            args.width,
            args.height,
            tile_x,
            tile_y,
            current_tile_w,
            current_tile_h,
            out_w,
            out_h,
            args.invert,
            args.reverse_bits,
            args.swap_channels,
        )
        blit(sheet, sheet_w, tile, out_w, out_h, col * (out_w + args.gap), row * (out_h + args.gap))

    output = args.output or args.input.with_name(f"{args.input.stem}_tile_{args.tile_cols}x{args.tile_rows}_sheet.png")
    write_png_rgb(output, sheet_w, sheet_h, bytes(sheet))
    print(f"output={output}")
    print(f"tile_size={tile_w}x{tile_h}")
    return output


def write_phase_sheet(args: argparse.Namespace, raw: bytes) -> Path:
    variants = [(x_phase, y_phase) for y_phase in range(args.phase_y_count) for x_phase in range(args.phase_x_count)]
    tile_w = args.output_width
    tile_h = args.output_height
    cols = args.phase_x_count
    rows = args.phase_y_count
    sheet_w = cols * tile_w + (cols - 1) * args.gap
    sheet_h = rows * tile_h + (rows - 1) * args.gap
    sheet = bytearray(sheet_w * sheet_h * 3)

    for x_phase, y_phase in variants:
        tile = render_decimated(
            raw,
            args.width,
            args.height,
            args.phase_x_step,
            args.phase_y_step,
            x_phase,
            y_phase,
            tile_w,
            tile_h,
            args.invert,
            args.reverse_bits,
            args.swap_channels,
        )
        blit(sheet, sheet_w, tile, tile_w, tile_h, x_phase * (tile_w + args.gap), y_phase * (tile_h + args.gap))

    output = args.output or args.input.with_name(
        f"{args.input.stem}_phase_x{args.phase_x_step}_y{args.phase_y_step}_sheet.png"
    )
    write_png_rgb(output, sheet_w, sheet_h, bytes(sheet))
    print(f"output={output}")
    print(f"x_step={args.phase_x_step} y_step={args.phase_y_step}")
    for index, (x_phase, y_phase) in enumerate(variants):
        print(f"  {index}: x_phase={x_phase} y_phase={y_phase}")
    return output


def write_untile(args: argparse.Namespace, raw: bytes) -> Path:
    tile_w = args.width // args.tile_cols
    tile_h = args.height // args.tile_rows
    out_w = tile_w * args.tile_cols
    out_h = tile_h * args.tile_rows
    pixels = bytearray(out_w * out_h * 3)

    for phase_y in range(args.tile_rows):
        for phase_x in range(args.tile_cols):
            src_tile_x = phase_x * tile_w
            src_tile_y = phase_y * tile_h
            dst_phase_x = (phase_x + args.phase_x_offset) % args.tile_cols
            dst_phase_y = (phase_y + args.phase_y_offset) % args.tile_rows
            if args.reverse_phase_x:
                dst_phase_x = args.tile_cols - 1 - dst_phase_x
            if args.reverse_phase_y:
                dst_phase_y = args.tile_rows - 1 - dst_phase_y

            for local_y in range(tile_h):
                for local_x in range(tile_w):
                    src = (src_tile_y + local_y) * args.width + src_tile_x + local_x
                    dst_x = local_x * args.tile_cols + dst_phase_x
                    dst_y = local_y * args.tile_rows + dst_phase_y
                    dst = (dst_y * out_w + dst_x) * 3
                    rgb = decode_rg44(raw[src], args.invert, args.reverse_bits, args.swap_channels)
                    pixels[dst : dst + 3] = bytes(rgb)

    output = args.output or args.input.with_name(
        f"{args.input.stem}_untile_{args.tile_cols}x{args.tile_rows}.png"
    )
    write_png_rgb(output, out_w, out_h, bytes(pixels))
    print(f"output={output}")
    print(f"image={out_w}x{out_h}")
    print(f"tile_size={tile_w}x{tile_h}")
    print(
        f"phase_offset={args.phase_x_offset},{args.phase_y_offset} "
        f"reverse_phase_x={args.reverse_phase_x} reverse_phase_y={args.reverse_phase_y}"
    )
    return output


def run(args: argparse.Namespace) -> int:
    raw = args.input.read_bytes()
    expected = args.width * args.height
    if len(raw) != expected:
        raise ValueError(f"expected {expected} bytes, got {len(raw)}")

    if args.mode == "tiles":
        write_tile_sheet(args, raw)
    elif args.mode == "phases":
        write_phase_sheet(args, raw)
    elif args.mode == "untile":
        write_untile(args, raw)
    else:
        write_decimation_sheet(args, raw)
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("--mode", choices=["decimate", "tiles", "phases", "untile"], default="decimate")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--width", type=int, default=160)
    parser.add_argument("--height", type=int, default=144)
    parser.add_argument("--output-width", type=int, default=160)
    parser.add_argument("--output-height", type=int, default=144)
    parser.add_argument("--x-steps", type=parse_int_list, default=parse_int_list("1,2,3,4,5,6,7,8"))
    parser.add_argument("--y-steps", type=parse_int_list, default=parse_int_list("1,2,3,4,5,6"))
    parser.add_argument("--x-phase", type=int, default=0)
    parser.add_argument("--y-phase", type=int, default=0)
    parser.add_argument("--tile-cols", type=int, default=5)
    parser.add_argument("--tile-rows", type=int, default=5)
    parser.add_argument("--phase-x-step", type=int, default=5)
    parser.add_argument("--phase-y-step", type=int, default=5)
    parser.add_argument("--phase-x-count", type=int, default=5)
    parser.add_argument("--phase-y-count", type=int, default=5)
    parser.add_argument("--phase-x-offset", type=int, default=0)
    parser.add_argument("--phase-y-offset", type=int, default=0)
    parser.add_argument("--reverse-phase-x", action="store_true")
    parser.add_argument("--reverse-phase-y", action="store_true")
    parser.add_argument("--columns", type=int, default=4)
    parser.add_argument("--gap", type=int, default=6)
    parser.add_argument("--invert", action="store_true")
    parser.add_argument("--reverse-bits", action="store_true")
    parser.add_argument("--swap-channels", action="store_true")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if args.width <= 0 or args.height <= 0 or args.output_width <= 0 or args.output_height <= 0:
        print("error: dimensions must be positive", file=sys.stderr)
        return 2
    if args.columns <= 0 or args.gap < 0 or args.tile_cols <= 0 or args.tile_rows <= 0:
        print("error: layout arguments must be positive", file=sys.stderr)
        return 2
    if args.phase_x_step <= 0 or args.phase_y_step <= 0 or args.phase_x_count <= 0 or args.phase_y_count <= 0:
        print("error: phase arguments must be positive", file=sys.stderr)
        return 2
    try:
        return run(args)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
