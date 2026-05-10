#!/usr/bin/env python3
"""Decode fast LCD_CAM RAW8 streams using named reconstruction presets."""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path

from render_dvp_raw import render_rg44, render_rgb332


@dataclass(frozen=True)
class DecodePreset:
    name: str
    stream_width: int
    frame_period_rows: int
    visible_width: int
    visible_height: int
    x_offset: int
    y_offset: int
    invert: bool
    render_mode: str
    description: str


PRESETS: dict[str, DecodePreset] = {
    "gbc_rg44_fast_v1": DecodePreset(
        name="gbc_rg44_fast_v1",
        stream_width=161,
        frame_period_rows=145,
        visible_width=160,
        visible_height=144,
        x_offset=0,
        y_offset=0,
        invert=False,
        render_mode="rg44",
        description=(
            "Experimental GBC LCD fast-capture preset: DE forced high, DCLK inverted edge, "
            "RG44 upper-bit diagnostic data, 161 bytes per transfer line, 145 transfer lines per frame, "
            "crop 160x144 visible pixels."
        ),
    ),
    "gbc_rgb332_fast_v1": DecodePreset(
        name="gbc_rgb332_fast_v1",
        stream_width=161,
        frame_period_rows=145,
        visible_width=160,
        visible_height=144,
        x_offset=0,
        y_offset=0,
        invert=False,
        render_mode="rgb332",
        description=(
            "Experimental GBC LCD fast-capture color preset: DE forced high, DCLK inverted edge, "
            "RGB332 upper-bit diagnostic data, 161 bytes per transfer line, 145 transfer lines per frame, "
            "crop 160x144 visible pixels."
        ),
    ),
}


def utc_stamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def extract_frame(raw: bytes, preset: DecodePreset, frame_index: int, x_offset: int, y_offset: int) -> bytes:
    output = bytearray(preset.visible_width * preset.visible_height)
    start_row = y_offset + (frame_index * preset.frame_period_rows)
    for row in range(preset.visible_height):
        src = ((start_row + row) * preset.stream_width) + x_offset
        if src < 0 or src + preset.visible_width > len(raw):
            raise ValueError(
                f"frame {frame_index} exceeds raw buffer at row {row}: "
                f"need bytes {src}..{src + preset.visible_width}, have {len(raw)}"
            )
        output[row * preset.visible_width : (row + 1) * preset.visible_width] = raw[src : src + preset.visible_width]
    return bytes(output)


def available_frame_count(raw_len: int, preset: DecodePreset, y_offset: int) -> int:
    rows = raw_len // preset.stream_width
    if rows <= y_offset:
        return 0
    return max(0, ((rows - y_offset - preset.visible_height) // preset.frame_period_rows) + 1)


def run(args: argparse.Namespace) -> int:
    preset = PRESETS[args.preset]
    raw = args.input.read_bytes()
    x_offset = args.x_offset if args.x_offset is not None else preset.x_offset
    y_offset = args.y_offset if args.y_offset is not None else preset.y_offset
    invert = args.invert if args.invert is not None else preset.invert

    if x_offset < 0 or x_offset + preset.visible_width > preset.stream_width:
        raise ValueError("x offset must keep the visible crop within the stream width")
    if y_offset < 0:
        raise ValueError("y offset must be non-negative")

    frame_count = available_frame_count(len(raw), preset, y_offset)
    if frame_count == 0:
        raise ValueError("raw buffer does not contain one complete frame for this preset")
    if args.frame is not None:
        frame_indices = [args.frame]
    else:
        frame_indices = list(range(frame_count))

    output_dir = args.output_dir or args.input.with_name(args.input.stem + f"_{preset.name}")
    output_dir.mkdir(parents=True, exist_ok=True)

    artifacts = []
    for frame_index in frame_indices:
        if frame_index < 0 or frame_index >= frame_count:
            raise ValueError(f"frame index {frame_index} outside available range 0..{frame_count - 1}")
        frame = extract_frame(raw, preset, frame_index, x_offset, y_offset)
        suffix = "_inverted" if invert else ""
        png_path = output_dir / f"{args.input.stem}_{preset.name}_frame{frame_index}_x{x_offset}_y{y_offset}{suffix}.png"
        bin_path = output_dir / f"{args.input.stem}_{preset.name}_frame{frame_index}_x{x_offset}_y{y_offset}.bin"
        bin_path.write_bytes(frame)
        if preset.render_mode == "rgb332":
            render_rgb332(frame, preset.visible_width, preset.visible_height, png_path, invert=invert)
        else:
            render_rg44(frame, preset.visible_width, preset.visible_height, png_path, invert=invert)
        artifacts.append({
            "frame_index": frame_index,
            "raw_bin": str(bin_path),
            "png": str(png_path),
        })
        print(f"frame={frame_index} png={png_path}")

    report = {
        "created_utc": utc_stamp(),
        "input": str(args.input),
        "input_bytes": len(raw),
        "preset": asdict(preset),
        "effective": {
            "x_offset": x_offset,
            "y_offset": y_offset,
            "invert": invert,
            "available_frame_count": frame_count,
        },
        "artifacts": artifacts,
    }
    report_path = output_dir / f"{args.input.stem}_{preset.name}_decode_report.json"
    report_path.write_text(json.dumps(report, indent=2, sort_keys=True), encoding="utf-8")
    print(f"report={report_path}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="fast LCD_CAM RAW8 .bin stream")
    parser.add_argument("--preset", choices=sorted(PRESETS), default="gbc_rg44_fast_v1")
    parser.add_argument("--frame", type=int, help="decode only one frame index; default decodes all complete frames")
    parser.add_argument("--x-offset", type=int)
    parser.add_argument("--y-offset", type=int)
    parser.add_argument("--invert", action=argparse.BooleanOptionalAction, default=None)
    parser.add_argument("--output-dir", type=Path)
    return parser


def main() -> int:
    try:
        return run(build_parser().parse_args())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
