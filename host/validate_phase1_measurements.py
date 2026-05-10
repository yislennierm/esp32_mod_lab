#!/usr/bin/env python3
"""Validate Phase 1 voltage measurements before firmware pin map changes."""

from __future__ import annotations

import argparse
import csv
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any


SAFE_HIGH_MAX_V = 3.3
SAFE_LOW_MIN_V = -0.3
SAFE_LOW_MAX_V = 0.8
SAFE_HIGH_MIN_V = 2.0
VALID_DECISIONS = {"unknown", "safe", "needs_level_shift", "dangerous"}
DANGEROUS_RAILS = {"V0-V9", "VCOM", "VEE", "VSHA", "VSHD"}
REQUIRED_COLUMNS = {
    "signal",
    "connector_pin",
    "measured_low_v",
    "measured_high_v",
    "proposed_gpio",
    "decision",
}


@dataclass(frozen=True)
class Measurement:
    signal: str
    connector_pin: str
    measured_low_v: float | None
    measured_high_v: float | None
    proposed_gpio: int | None
    decision: str
    notes: str


def parse_float(value: str) -> float | None:
    value = value.strip()
    if not value:
        return None
    return float(value)


def parse_gpio(value: str) -> int | None:
    value = value.strip()
    if not value:
        return None
    return int(value)


def load_measurements(path: Path) -> tuple[list[Measurement], list[str]]:
    errors: list[str] = []
    with path.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle)
        fieldnames = set(reader.fieldnames or [])
        missing = sorted(REQUIRED_COLUMNS - fieldnames)
        if missing:
            errors.append("missing required columns: " + ", ".join(missing))

        measurements: list[Measurement] = []
        for line_number, row in enumerate(reader, start=2):
            signal = (row.get("signal") or "").strip()
            decision = (row.get("decision") or "").strip()
            if not signal:
                errors.append(f"line {line_number}: missing signal")
                continue
            if decision not in VALID_DECISIONS:
                errors.append(f"{signal}: invalid decision {decision!r}")

            try:
                low_v = parse_float(row.get("measured_low_v") or "")
                high_v = parse_float(row.get("measured_high_v") or "")
                gpio = parse_gpio(row.get("proposed_gpio") or "")
            except ValueError as exc:
                errors.append(f"{signal}: invalid numeric value: {exc}")
                low_v = None
                high_v = None
                gpio = None

            measurements.append(
                Measurement(
                    signal=signal,
                    connector_pin=(row.get("connector_pin") or "").strip(),
                    measured_low_v=low_v,
                    measured_high_v=high_v,
                    proposed_gpio=gpio,
                    decision=decision or "unknown",
                    notes=(row.get("notes") or "").strip(),
                )
            )

    return measurements, errors


def validate(measurements: list[Measurement], initial_errors: list[str]) -> dict[str, Any]:
    errors = list(initial_errors)
    warnings: list[str] = []
    safe_pinmap: list[dict[str, Any]] = []
    proposed_gpios: dict[int, str] = {}

    for measurement in measurements:
        if measurement.signal in DANGEROUS_RAILS:
            if measurement.decision != "dangerous":
                errors.append(f"{measurement.signal}: dangerous rail must remain marked dangerous")
            if measurement.proposed_gpio is not None:
                errors.append(f"{measurement.signal}: dangerous rail must not have proposed_gpio")
            continue

        if measurement.decision == "unknown":
            warnings.append(f"{measurement.signal}: remains unknown")
            continue

        if measurement.decision == "dangerous":
            if measurement.proposed_gpio is not None:
                errors.append(f"{measurement.signal}: dangerous signal must not have proposed_gpio")
            continue

        if measurement.measured_low_v is None or measurement.measured_high_v is None:
            errors.append(f"{measurement.signal}: safe/level-shift decisions require measured voltages")
            continue

        if measurement.measured_high_v > SAFE_HIGH_MAX_V:
            if measurement.decision == "safe":
                errors.append(
                    f"{measurement.signal}: high voltage {measurement.measured_high_v}V exceeds {SAFE_HIGH_MAX_V}V"
                )
        if not (SAFE_LOW_MIN_V <= measurement.measured_low_v <= SAFE_LOW_MAX_V):
            errors.append(
                f"{measurement.signal}: low voltage {measurement.measured_low_v}V outside expected digital low range"
            )
        if measurement.measured_high_v < SAFE_HIGH_MIN_V:
            warnings.append(
                f"{measurement.signal}: high voltage {measurement.measured_high_v}V may not meet GPIO high threshold"
            )

        if measurement.decision == "needs_level_shift":
            if measurement.proposed_gpio is not None:
                errors.append(f"{measurement.signal}: needs_level_shift must not have proposed_gpio yet")
            continue

        if measurement.decision == "safe":
            if measurement.proposed_gpio is None:
                errors.append(f"{measurement.signal}: safe signal requires proposed_gpio")
                continue
            if measurement.proposed_gpio in proposed_gpios:
                errors.append(
                    f"{measurement.signal}: GPIO {measurement.proposed_gpio} already used by {proposed_gpios[measurement.proposed_gpio]}"
                )
                continue
            proposed_gpios[measurement.proposed_gpio] = measurement.signal
            safe_pinmap.append(
                {
                    "signal": measurement.signal,
                    "connector_pin": measurement.connector_pin,
                    "gpio": measurement.proposed_gpio,
                    "measured_low_v": measurement.measured_low_v,
                    "measured_high_v": measurement.measured_high_v,
                }
            )

    return {
        "ok": not errors,
        "errors": errors,
        "warnings": warnings,
        "safe_pinmap": safe_pinmap,
        "safe_pin_count": len(safe_pinmap),
    }


def write_report(path: Path, source_csv: Path, result: dict[str, Any]) -> None:
    lines = [
        "# Phase 1 Measurement Validation Report",
        "",
        f"Source CSV: `{source_csv}`",
        "",
        f"Validation result: `{'PASS' if result['ok'] else 'BLOCKED'}`",
        "",
        "## Errors",
        "",
    ]
    if result["errors"]:
        lines.extend(f"- {error}" for error in result["errors"])
    else:
        lines.append("- None")
    lines.extend(["", "## Warnings", ""])
    if result["warnings"]:
        lines.extend(f"- {warning}" for warning in result["warnings"])
    else:
        lines.append("- None")
    lines.extend(["", "## Safe Pinmap Proposal", ""])
    if result["safe_pinmap"]:
        lines.append("| Signal | Connector Pin | GPIO | Low V | High V |")
        lines.append("|---|---|---:|---:|---:|")
        for pin in result["safe_pinmap"]:
            lines.append(
                f"| {pin['signal']} | {pin['connector_pin']} | {pin['gpio']} | "
                f"{pin['measured_low_v']} | {pin['measured_high_v']} |"
            )
    else:
        lines.append("No safe GPIO pinmap entries are proposed.")
    lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8")


def run(args: argparse.Namespace) -> int:
    measurements, errors = load_measurements(args.csv_path)
    result = validate(measurements, errors)
    if args.report:
        write_report(args.report, args.csv_path, result)
    print(json.dumps(result, sort_keys=True))
    return 0 if result["ok"] else 1


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv_path", type=Path)
    parser.add_argument("--report", type=Path, help="optional Markdown report path")
    return parser


def main() -> int:
    return run(build_parser().parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
