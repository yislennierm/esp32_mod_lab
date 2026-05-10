#!/usr/bin/env python3
"""Render red-bus sample CSV files as diagnostic PNG strips."""

from __future__ import annotations

import argparse
import csv
import struct
import sys
import zlib
from pathlib import Path


def png_chunk(kind: bytes, data: bytes) -> bytes:
    return (
        struct.pack(">I", len(data))
        + kind
        + data
        + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    )


def write_png_rgb(path: Path, width: int, height: int, pixels: bytes) -> None:
    if len(pixels) != width * height * 3:
        raise ValueError("pixel buffer size does not match image dimensions")

    rows = []
    stride = width * 3
    for y in range(height):
        rows.append(b"\x00" + pixels[y * stride : (y + 1) * stride])

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    png = (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", ihdr)
        + png_chunk(b"IDAT", zlib.compress(b"".join(rows), level=9))
        + png_chunk(b"IEND", b"")
    )
    path.write_bytes(png)


def read_red_samples(path: Path) -> list[int]:
    values = []
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        if "red6" not in (reader.fieldnames or []):
            raise ValueError(f"{path} does not contain a red6 column")
        for row in reader:
            values.append(int(row["red6"]))
    if not values:
        raise ValueError(f"{path} contains no samples")
    return values


def render_strip(values: list[int], height: int, scale_x: int) -> tuple[int, int, bytes]:
    width = len(values) * scale_x
    pixels = bytearray(width * height * 3)

    for index, red6 in enumerate(values):
        # Expand 6-bit red to 8-bit red. Keep green/blue at zero.
        red8 = max(0, min(255, round((red6 & 0x3F) * 255 / 63)))
        for x_repeat in range(scale_x):
            x = index * scale_x + x_repeat
            for y in range(height):
                offset = (y * width + x) * 3
                pixels[offset] = red8
                pixels[offset + 1] = 0
                pixels[offset + 2] = 0

    return width, height, bytes(pixels)


def run(args: argparse.Namespace) -> int:
    values = read_red_samples(args.input)
    output = args.output or args.input.with_suffix(".png")
    width, height, pixels = render_strip(values, args.height, args.scale_x)
    write_png_rgb(output, width, height, pixels)

    print(f"input={args.input}")
    print(f"output={output}")
    print(f"samples={len(values)}")
    print(f"image={width}x{height}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="red DCLK CSV with a red6 column")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--height", type=int, default=64)
    parser.add_argument("--scale-x", type=int, default=2)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if args.height <= 0:
        print("error: --height must be > 0", file=sys.stderr)
        return 2
    if args.scale_x <= 0:
        print("error: --scale-x must be > 0", file=sys.stderr)
        return 2
    try:
        return run(args)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
