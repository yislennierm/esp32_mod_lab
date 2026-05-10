#!/usr/bin/env python3
"""Render experimental DVP RAW8 captures as diagnostic PNGs."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from render_red_diagnostic import write_png_rgb


def render_lower6_red(raw: bytes, width: int, height: int, output: Path, invert: bool) -> None:
    if len(raw) != width * height:
        raise ValueError(f"expected {width * height} bytes, got {len(raw)}")

    pixels = bytearray(width * height * 3)
    for index, value in enumerate(raw):
        red6 = value & 0x3F
        if invert:
            red6 = 63 - red6
        red8 = round(red6 * 255 / 63)
        offset = index * 3
        pixels[offset] = red8
        pixels[offset + 1] = 0
        pixels[offset + 2] = 0

    write_png_rgb(output, width, height, bytes(pixels))


def render_rg44(raw: bytes, width: int, height: int, output: Path, invert: bool) -> None:
    if len(raw) != width * height:
        raise ValueError(f"expected {width * height} bytes, got {len(raw)}")

    pixels = bytearray(width * height * 3)
    for index, value in enumerate(raw):
        red4 = value & 0x0F
        green4 = (value >> 4) & 0x0F
        if invert:
            red4 = 15 - red4
            green4 = 15 - green4
        offset = index * 3
        pixels[offset] = red4 * 17
        pixels[offset + 1] = green4 * 17
        pixels[offset + 2] = 0

    write_png_rgb(output, width, height, bytes(pixels))


def render_rgb332(raw: bytes, width: int, height: int, output: Path, invert: bool) -> None:
    if len(raw) != width * height:
        raise ValueError(f"expected {width * height} bytes, got {len(raw)}")

    pixels = bytearray(width * height * 3)
    for index, value in enumerate(raw):
        red3 = (value >> 5) & 0x07
        green3 = (value >> 2) & 0x07
        blue2 = value & 0x03
        red8 = round(red3 * 255 / 7)
        green8 = round(green3 * 255 / 7)
        blue8 = round(blue2 * 255 / 3)
        if invert:
            red8 = 255 - red8
            green8 = 255 - green8
            blue8 = 255 - blue8
        offset = index * 3
        pixels[offset] = red8
        pixels[offset + 1] = green8
        pixels[offset + 2] = blue8

    write_png_rgb(output, width, height, bytes(pixels))


def render_rgb664(raw: bytes, width: int, height: int, output: Path, invert: bool) -> None:
    expected = width * height * 2
    if len(raw) != expected:
        raise ValueError(f"expected {expected} bytes, got {len(raw)}")

    pixels = bytearray(width * height * 3)
    for index in range(width * height):
        word = raw[index * 2] | (raw[index * 2 + 1] << 8)
        red6 = word & 0x3F
        green6 = (word >> 6) & 0x3F
        blue4 = (word >> 12) & 0x0F
        red8 = round(red6 * 255 / 63)
        green8 = round(green6 * 255 / 63)
        blue8 = blue4 * 17
        if invert:
            red8 = 255 - red8
            green8 = 255 - green8
            blue8 = 255 - blue8
        offset = index * 3
        pixels[offset] = red8
        pixels[offset + 1] = green8
        pixels[offset + 2] = blue8

    write_png_rgb(output, width, height, bytes(pixels))


def render_rgb565(raw: bytes, width: int, height: int, output: Path, invert: bool) -> None:
    expected = width * height * 2
    if len(raw) != expected:
        raise ValueError(f"expected {expected} bytes, got {len(raw)}")

    pixels = bytearray(width * height * 3)
    for index in range(width * height):
        word = raw[index * 2] | (raw[index * 2 + 1] << 8)
        blue5 = word & 0x1F
        green6 = (word >> 5) & 0x3F
        red5 = (word >> 11) & 0x1F
        red8 = round(red5 * 255 / 31)
        green8 = round(green6 * 255 / 63)
        blue8 = round(blue5 * 255 / 31)
        if invert:
            red8 = 255 - red8
            green8 = 255 - green8
            blue8 = 255 - blue8
        offset = index * 3
        pixels[offset] = red8
        pixels[offset + 1] = green8
        pixels[offset + 2] = blue8

    write_png_rgb(output, width, height, bytes(pixels))


def run(args: argparse.Namespace) -> int:
    raw = args.input.read_bytes()
    output = args.output
    if output is None:
        suffix = "_inverted.png" if args.invert else "_normal.png"
        output = args.input.with_name(args.input.stem + suffix)

    if args.mode == "rg44":
        render_rg44(raw, args.width, args.height, output, args.invert)
    elif args.mode == "rgb332":
        render_rgb332(raw, args.width, args.height, output, args.invert)
    elif args.mode == "rgb664":
        render_rgb664(raw, args.width, args.height, output, args.invert)
    elif args.mode == "rgb565":
        render_rgb565(raw, args.width, args.height, output, args.invert)
    else:
        render_lower6_red(raw, args.width, args.height, output, args.invert)
    print(f"input={args.input}")
    print(f"output={output}")
    print(f"image={args.width}x{args.height}")
    print(f"mode={args.mode}")
    print(f"invert={args.invert}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="DVP raw .bin file")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--width", type=int, default=160)
    parser.add_argument("--height", type=int, default=144)
    parser.add_argument("--mode", choices=["rg44", "rgb332", "rgb664", "rgb565", "lower6-red"], default="rg44")
    parser.add_argument("--invert", action="store_true")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if args.width <= 0 or args.height <= 0:
        print("error: dimensions must be positive", file=sys.stderr)
        return 2
    try:
        return run(args)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
