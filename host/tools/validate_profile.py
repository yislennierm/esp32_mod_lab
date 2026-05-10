#!/usr/bin/env python3
"""Validate an ESP32-P4 signal-lab target profile.

This is intentionally lightweight and dependency-free. It checks the project
rules that matter before a full JSON Schema validator exists.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


REQUIRED_TOP_LEVEL = (
    "schema_version",
    "profile_id",
    "display_name",
    "status",
    "target_type",
    "safety",
    "connector",
    "signals",
)


def iter_signal_objects(signals: dict[str, Any]) -> list[dict[str, Any]]:
    found: list[dict[str, Any]] = []
    timing = signals.get("timing_or_control", [])
    if isinstance(timing, list):
        found.extend(item for item in timing if isinstance(item, dict))
    analog = signals.get("analog_or_power", [])
    if isinstance(analog, list):
        found.extend(item for item in analog if isinstance(item, dict))
    pixel_bus = signals.get("pixel_bus", {})
    if isinstance(pixel_bus, dict):
        for channel in ("red", "green", "blue"):
            bits = pixel_bus.get(channel, {})
            if isinstance(bits, dict):
                found.extend(item for item in bits.values() if isinstance(item, dict))
    return found


def gpio_from_signal(signal: dict[str, Any]) -> int | None:
    for key in ("current_esp32p4_gpio", "esp32p4_gpio"):
        value = signal.get(key)
        if isinstance(value, int):
            return value
    return None


def signal_name(signal: dict[str, Any]) -> str:
    if isinstance(signal.get("name"), str):
        return signal["name"]
    for key in ("R0", "R1", "R2", "R3", "R4", "R5", "G0", "G1", "G2", "G3", "G4", "G5", "B0", "B1", "B2", "B3", "B4", "B5"):
        if key in signal:
            return key
    return "<unnamed>"


def validate(profile: dict[str, Any]) -> tuple[list[str], list[str]]:
    errors: list[str] = []
    warnings: list[str] = []

    for key in REQUIRED_TOP_LEVEL:
        if key not in profile:
            errors.append(f"missing top-level field: {key}")

    safety = profile.get("safety", {})
    signals = profile.get("signals", {})
    if not isinstance(safety, dict):
        errors.append("safety must be an object")
        safety = {}
    if not isinstance(signals, dict):
        errors.append("signals must be an object")
        signals = {}

    if safety.get("esp32p4_gpio_5v_tolerant") is not False:
        warnings.append("esp32p4_gpio_5v_tolerant should be false unless proven otherwise")

    if safety.get("default_gpio_mode") != "input_only":
        warnings.append("default_gpio_mode is not input_only")

    do_not_connect = set(safety.get("do_not_connect_to_gpio", []))
    dangerous = set(safety.get("dangerous_rails", []))
    protected_names = do_not_connect | dangerous

    gpio_to_signals: dict[int, list[str]] = {}
    for signal in iter_signal_objects(signals):
        name = signal_name(signal)
        gpio = gpio_from_signal(signal)
        if name in protected_names and gpio is not None:
            errors.append(f"protected signal {name} has GPIO assignment {gpio}")
        if gpio is not None:
            gpio_to_signals.setdefault(gpio, []).append(name)

    for gpio, names in sorted(gpio_to_signals.items()):
        if len(names) > 1:
            errors.append(f"GPIO {gpio} assigned to multiple signals: {', '.join(names)}")

    concerns = safety.get("known_concerns", [])
    if not isinstance(concerns, list) or not concerns:
        warnings.append("safety.known_concerns is empty")

    if profile.get("profile_id") == "gbc_lcd":
        cls_gpio = None
        for signal in iter_signal_objects(signals):
            if signal.get("name") == "CLS":
                cls_gpio = gpio_from_signal(signal)
        if cls_gpio == 32:
            errors.append("gbc_lcd profile still maps CLS to historical problematic GPIO32")
        elif cls_gpio != 3:
            warnings.append(f"gbc_lcd CLS is mapped to GPIO {cls_gpio}; current working baseline is GPIO3")

    return errors, warnings


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", type=Path, help="target profile JSON file")
    parser.add_argument("--json", action="store_true", help="emit machine-readable result")
    args = parser.parse_args()

    with args.profile.open("r", encoding="utf-8") as f:
        profile = json.load(f)

    errors, warnings = validate(profile)
    result = {
        "profile": str(args.profile),
        "ok": not errors,
        "error_count": len(errors),
        "warning_count": len(warnings),
        "errors": errors,
        "warnings": warnings,
    }

    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        print(f"profile: {args.profile}")
        print(f"ok: {str(result['ok']).lower()}")
        for warning in warnings:
            print(f"warning: {warning}")
        for error in errors:
            print(f"error: {error}")

    return 0 if not errors else 1


if __name__ == "__main__":
    raise SystemExit(main())
