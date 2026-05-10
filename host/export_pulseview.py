#!/usr/bin/env python3
"""Export timing-edge captures to VCD for PulseView/sigrok-style inspection."""

from __future__ import annotations

import argparse
import json
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


VCD_IDS = [
    "!",
    "\"",
    "#",
    "$",
    "%",
    "&",
    "'",
    "(",
    ")",
    "*",
    "+",
    ",",
    "-",
    ".",
    "/",
]


def load_capture(path: Path) -> dict[str, Any]:
    data = json.loads(path.read_text(encoding="utf-8"))
    events = data.get("events")
    if not isinstance(events, list):
        raise ValueError(f"{path} does not contain an events list")
    return data


def ordered_signals(events: list[dict[str, Any]]) -> list[str]:
    preferred = ["SPS", "LP", "SPL", "CLS", "PS"]
    seen = {str(event.get("signal")) for event in events if event.get("signal")}
    return [signal for signal in preferred if signal in seen] + sorted(seen - set(preferred))


def write_vcd(data: dict[str, Any], output: Path, include_red6: bool = True) -> None:
    events = sorted(data.get("events", []), key=lambda event: int(event.get("t_us", 0)))
    signals = ordered_signals(events)
    if len(signals) + (1 if include_red6 else 0) > len(VCD_IDS):
        raise ValueError("too many signals for built-in VCD identifier table")

    ids = {signal: VCD_IDS[index] for index, signal in enumerate(signals)}
    red6_id = VCD_IDS[len(signals)] if include_red6 else None
    now = datetime.now(timezone.utc).isoformat()

    lines = [
        f"$date {now} $end",
        "$version gbc-p4-probe export_pulseview.py $end",
        "$comment Event-based VCD export from ESP32-P4 timing capture. Timescale is 1 us. $end",
        "$timescale 1 us $end",
        "$scope module logic $end",
    ]
    for signal in signals:
        lines.append(f"$var wire 1 {ids[signal]} {signal} $end")
    if include_red6 and red6_id is not None:
        lines.append(f"$var wire 6 {red6_id} red6 $end")
    lines.extend(["$upscope $end", "$enddefinitions $end", "$dumpvars"])
    for signal in signals:
        lines.append(f"x{ids[signal]}")
    if include_red6 and red6_id is not None:
        lines.append(f"bxxxxxx {red6_id}")
    lines.append("$end")

    last_time: int | None = None
    last_values: dict[str, int] = {}
    last_red6: int | None = None
    for event in events:
        t_us = int(event.get("t_us", 0))
        if t_us != last_time:
            lines.append(f"#{t_us}")
            last_time = t_us
        signal = str(event.get("signal"))
        if signal in ids and "level" in event:
            level = int(event["level"])
            if last_values.get(signal) != level:
                lines.append(f"{level}{ids[signal]}")
                last_values[signal] = level
        if include_red6 and red6_id is not None and "red6" in event:
            red6 = int(event["red6"]) & 0x3F
            if last_red6 != red6:
                lines.append(f"b{red6:06b} {red6_id}")
                last_red6 = red6

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")


def default_output(input_path: Path) -> Path:
    return input_path.with_suffix(".vcd")


def run(args: argparse.Namespace) -> int:
    data = load_capture(args.input)
    output = args.output or default_output(args.input)
    write_vcd(data, output, include_red6=not args.no_red6)
    print(f"vcd={output}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="timing-edge raw JSON capture")
    parser.add_argument("-o", "--output", type=Path, help="output .vcd path")
    parser.add_argument("--no-red6", action="store_true", help="omit red6 data bus from VCD")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    try:
        return run(args)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())

