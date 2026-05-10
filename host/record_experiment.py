#!/usr/bin/env python3
"""Create reproducible experiment records for the GBC LCD probe project."""

from __future__ import annotations

import argparse
import csv
import json
import platform
import subprocess
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from gbc_probe import DEFAULT_BAUD, DEFAULT_TIMEOUT_S, ProbeClient, autodetect_port
from gbc_probe import PHASE1_BLOCKED_COMMANDS


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_EXPERIMENT_ROOT = REPO_ROOT / "captures" / "experiments"
PHASE1_SIGNALS = [
    "DCLK",
    "LP",
    "SPL",
    "SPS",
    "CLS",
    "MOD",
    "R0",
    "R1",
    "R2",
    "R3",
    "R4",
    "R5",
    "G0",
    "G1",
    "G2",
    "G3",
    "G4",
    "G5",
    "B0",
    "B1",
    "B2",
    "B3",
    "B4",
    "B5",
]
DANGEROUS_RAILS = ["V0-V9", "VCOM", "VEE", "VSHA", "VSHD"]


def iso_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat()


def safe_slug(value: str) -> str:
    chars = []
    for char in value.lower():
        if char.isalnum():
            chars.append(char)
        elif char in {"-", "_", " "}:
            chars.append("-")
    slug = "".join(chars).strip("-")
    while "--" in slug:
        slug = slug.replace("--", "-")
    return slug or "experiment"


def git_commit() -> str | None:
    try:
        result = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=REPO_ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None
    return result.stdout.strip()


def write_json(path: Path, value: Any) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def write_notes(path: Path, title: str, started_at: str) -> None:
    path.write_text(
        "\n".join(
            [
                f"# {title}",
                "",
                "## Objective",
                "",
                "Record the purpose of this experiment.",
                "",
                "## Setup",
                "",
                f"- Started: {started_at}",
                "- Equipment:",
                "- Probe settings:",
                "- Board state:",
                "",
                "## Procedure",
                "",
                "1. ",
                "",
                "## Observations",
                "",
                "- ",
                "",
                "## Result",
                "",
                "- ",
                "",
                "## Next Steps",
                "",
                "- ",
                "",
            ]
        ),
        encoding="utf-8",
    )


def write_voltage_template(path: Path) -> None:
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(
            [
                "signal",
                "connector_pin",
                "measured_low_v",
                "measured_high_v",
                "proposed_gpio",
                "probe_impedance",
                "equipment",
                "decision",
                "notes",
            ]
        )
        for signal in PHASE1_SIGNALS:
            writer.writerow([signal, "", "", "", "", "", "", "unknown", ""])
        for signal in DANGEROUS_RAILS:
            writer.writerow([signal, "", "", "", "", "", "", "dangerous", "do not connect to ESP32-P4 GPIO"])


def run_smoke(port: str, baud: int, timeout_s: float) -> dict[str, Any]:
    client = ProbeClient(port, baud, timeout_s)
    try:
        startup_lines = client.drain_startup()
        responses = [
            client.command("PING"),
            client.command("GET_VERSION"),
            client.command("GET_PINMAP"),
            client.command("EXPORT_STATS"),
        ]
        blocked_responses = [
            client.command(command) for command in PHASE1_BLOCKED_COMMANDS
        ]
    finally:
        client.close()

    errors: list[str] = []
    if responses[0].data.get("response") != "PONG":
        errors.append("PING did not return PONG")
    if responses[1].data.get("phase") != "phase1-electrical-safety":
        errors.append("GET_VERSION returned unexpected phase")
    pinmap = responses[2].data
    if pinmap.get("capture_pin_count") != 0:
        errors.append("GET_PINMAP must report capture_pin_count 0 during Phase 1")
    if pinmap.get("pins") != []:
        errors.append("GET_PINMAP must report no configured pins during Phase 1")
    stats = responses[3].data.get("stats", {})
    if stats.get("capture_pin_count") != 0:
        errors.append("capture_pin_count must remain 0 during Phase 1")
    for response in blocked_responses:
        if response.data.get("ok") is not False:
            errors.append(f"{response.command} unexpectedly succeeded")
        if response.data.get("error") != "no_capture_pins_configured":
            errors.append(f"{response.command} returned unexpected error")

    return {
        "ok": not errors,
        "port": port,
        "startup_lines": startup_lines,
        "responses": [response.data for response in responses],
        "raw_responses": [response.raw for response in responses],
        "blocked_responses": [response.data for response in blocked_responses],
        "raw_blocked_responses": [response.raw for response in blocked_responses],
        "errors": errors,
    }


def create_experiment(args: argparse.Namespace) -> int:
    started_at = iso_now()
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    title = args.title
    experiment_dir = args.output / f"{timestamp}-{safe_slug(title)}"
    experiment_dir.mkdir(parents=True, exist_ok=False)

    port = args.port or autodetect_port()
    metadata = {
        "title": title,
        "started_at": started_at,
        "phase": args.phase,
        "port": port,
        "baud": args.baud,
        "timeout_s": args.timeout,
        "host_platform": platform.platform(),
        "python_version": platform.python_version(),
        "git_commit": git_commit(),
    }
    write_json(experiment_dir / "metadata.json", metadata)
    write_notes(experiment_dir / "notes.md", title, started_at)
    write_voltage_template(experiment_dir / "phase1_voltage_template.csv")

    if args.smoke:
        smoke_result = run_smoke(port, args.baud, args.timeout)
        write_json(experiment_dir / "smoke_test.json", smoke_result)
        if not smoke_result["ok"]:
            print(json.dumps({"ok": False, "experiment_dir": str(experiment_dir), "errors": smoke_result["errors"]}, sort_keys=True))
            return 1

    print(json.dumps({"ok": True, "experiment_dir": str(experiment_dir)}, sort_keys=True))
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("title", help="short experiment title")
    parser.add_argument("--phase", default="phase1-electrical-safety")
    parser.add_argument("--port", help="serial port, for example /dev/cu.usbmodem14201")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT_S)
    parser.add_argument("--output", type=Path, default=DEFAULT_EXPERIMENT_ROOT)
    parser.add_argument("--no-smoke", action="store_false", dest="smoke", help="skip firmware smoke test")
    parser.set_defaults(smoke=True)
    return parser


def main() -> int:
    parser = build_parser()
    return create_experiment(parser.parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
