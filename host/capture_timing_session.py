#!/usr/bin/env python3
"""Run repeatable timing-edge capture sessions with optional manual triggers."""

from __future__ import annotations

import argparse
import csv
import json
import statistics
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from analyze_timing_edges import summarize_capture, write_events_csv
from analyze_timing_relationships import analyze as analyze_relationships
from analyze_timing_relationships import write_markdown as write_relationship_markdown
from gbc_probe import DEFAULT_BAUD, ProbeClient, autodetect_port


def utc_stamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def slugify(value: str) -> str:
    allowed = []
    for char in value.strip().lower():
        if char.isalnum():
            allowed.append(char)
        elif char in {"-", "_", " "}:
            allowed.append("_")
    slug = "".join(allowed).strip("_")
    return slug or "timing_session"


def wait_for_manual_trigger(label: str, run_index: int, repeat: int, pre_delay_ms: int) -> None:
    print("")
    print(f"READY [{run_index + 1}/{repeat}] label={label}")
    print("Set the GBC to the requested state now.")
    input("Press Enter to arm the capture...")
    if pre_delay_ms > 0:
        print(f"Waiting {pre_delay_ms} ms before capture...")
        time.sleep(pre_delay_ms / 1000.0)
    print("CAPTURING NOW")


def save_capture_artifacts(
    session_dir: Path,
    base_name: str,
    capture_data: dict[str, Any],
    relationship_summary: dict[str, Any],
) -> dict[str, str]:
    raw_path = session_dir / f"{base_name}.json"
    csv_path = session_dir / f"{base_name}.csv"
    edge_summary_path = session_dir / f"{base_name}_edge_summary.json"
    relationship_json_path = session_dir / f"{base_name}_relationships.json"
    relationship_md_path = session_dir / f"{base_name}_relationships.md"

    raw_path.write_text(json.dumps(capture_data, indent=2, sort_keys=True), encoding="utf-8")
    write_events_csv(csv_path, capture_data.get("events", []))

    edge_summary = summarize_capture(capture_data)
    edge_summary_path.write_text(json.dumps(edge_summary, indent=2, sort_keys=True), encoding="utf-8")

    relationship_json_path.write_text(
        json.dumps(relationship_summary, indent=2, sort_keys=True),
        encoding="utf-8",
    )
    write_relationship_markdown(relationship_md_path, raw_path, relationship_summary)

    return {
        "raw_json": str(raw_path),
        "csv": str(csv_path),
        "edge_summary_json": str(edge_summary_path),
        "relationship_json": str(relationship_json_path),
        "relationship_md": str(relationship_md_path),
    }


def summarize_session(runs: list[dict[str, Any]]) -> dict[str, Any]:
    frame_durations = [
        run["relationship_summary"]["frame_duration_mean_us"]
        for run in runs
        if run["relationship_summary"].get("frame_duration_mean_us") is not None
    ]
    lp_counts = [
        run["relationship_summary"]["lp_count_mean_per_frame"]
        for run in runs
        if run["relationship_summary"].get("lp_count_mean_per_frame") is not None
    ]
    spl_counts = [
        run["relationship_summary"]["spl_count_mean_per_frame"]
        for run in runs
        if run["relationship_summary"].get("spl_count_mean_per_frame") is not None
    ]
    overflow_counts = [run["capture"].get("overflow_count", 0) for run in runs]

    return {
        "run_count": len(runs),
        "overflow_total": sum(overflow_counts),
        "frame_duration_mean_us": statistics.fmean(frame_durations) if frame_durations else None,
        "frame_duration_min_us": min(frame_durations) if frame_durations else None,
        "frame_duration_max_us": max(frame_durations) if frame_durations else None,
        "lp_count_mean_per_frame": statistics.fmean(lp_counts) if lp_counts else None,
        "lp_count_values": lp_counts,
        "spl_count_mean_per_frame": statistics.fmean(spl_counts) if spl_counts else None,
        "spl_count_values": spl_counts,
    }


def write_combined_markdown(path: Path, session: dict[str, Any]) -> None:
    lines = [
        "# Timing Session Report",
        "",
        f"Label: `{session['label']}`",
        f"Started UTC: `{session['started_utc']}`",
        f"Duration per capture: `{session['duration_ms']} ms`",
        f"Manual trigger: `{session['manual_trigger']}`",
        f"Pre-delay: `{session['pre_delay_ms']} ms`",
        "",
        "## Combined Summary",
        "",
    ]
    combined = session["combined_summary"]
    for key, value in combined.items():
        lines.append(f"- {key}: {value}")

    lines.extend(
        [
            "",
            "## Runs",
            "",
            "| Run | Capture OK | Overflow | Complete Frames | Mean Frame us | LP/frame | SPL/frame | Raw JSON | Relationship Report |",
            "|---:|---|---:|---:|---:|---:|---:|---|---|",
        ]
    )
    for run in session["runs"]:
        rel = run["relationship_summary"]
        artifacts = run["artifacts"]
        lines.append(
            f"| {run['index']} | {run['capture'].get('ok')} | {run['capture'].get('overflow_count')} | "
            f"{rel.get('complete_frame_count')} | {rel.get('frame_duration_mean_us')} | "
            f"{rel.get('lp_count_mean_per_frame')} | {rel.get('spl_count_mean_per_frame')} | "
            f"`{artifacts['raw_json']}` | `{artifacts['relationship_md']}` |"
        )

    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def run(args: argparse.Namespace) -> int:
    port = args.port or autodetect_port()
    label = slugify(args.label)
    started_utc = utc_stamp()
    session_dir = args.output_dir / f"{started_utc}-{label}"
    session_dir.mkdir(parents=True, exist_ok=True)

    print(f"session_dir={session_dir}")
    print(f"port={port}")

    runs = []
    client = ProbeClient(port, args.baud, args.timeout)
    try:
        client.drain_startup()
        for index in range(args.repeat):
            if args.manual_trigger:
                wait_for_manual_trigger(args.label, index, args.repeat, args.pre_delay_ms)
            elif args.pre_delay_ms > 0:
                time.sleep(args.pre_delay_ms / 1000.0)

            response = client.command(f"CAPTURE_TIMING_EDGES {args.duration_ms}")
            capture_data = response.data
            events = capture_data.get("events", [])
            relationship_summary = analyze_relationships(events)

            base_name = f"run_{index + 1:02d}_timing_edges_{args.duration_ms}ms"
            artifacts = save_capture_artifacts(
                session_dir,
                base_name,
                capture_data,
                relationship_summary,
            )

            run_record = {
                "index": index + 1,
                "captured_utc": utc_stamp(),
                "capture": {
                    "ok": capture_data.get("ok"),
                    "event_count": capture_data.get("event_count"),
                    "overflow_count": capture_data.get("overflow_count"),
                    "duration_ms": capture_data.get("duration_ms"),
                },
                "relationship_summary": relationship_summary,
                "artifacts": artifacts,
            }
            runs.append(run_record)
            print(
                f"run={index + 1} ok={capture_data.get('ok')} "
                f"events={capture_data.get('event_count')} "
                f"overflow={capture_data.get('overflow_count')} "
                f"frames={relationship_summary.get('complete_frame_count')} "
                f"lp/frame={relationship_summary.get('lp_count_mean_per_frame')} "
                f"spl/frame={relationship_summary.get('spl_count_mean_per_frame')}"
            )
    finally:
        client.close()

    session = {
        "label": args.label,
        "started_utc": started_utc,
        "port": port,
        "duration_ms": args.duration_ms,
        "repeat": args.repeat,
        "manual_trigger": args.manual_trigger,
        "pre_delay_ms": args.pre_delay_ms,
        "runs": runs,
        "combined_summary": summarize_session(runs),
    }

    session_json = session_dir / "session_summary.json"
    session_md = session_dir / "session_report.md"
    session_json.write_text(json.dumps(session, indent=2, sort_keys=True), encoding="utf-8")
    write_combined_markdown(session_md, session)

    print(f"session_summary_json={session_json}")
    print(f"session_report_md={session_md}")
    return 0 if session["combined_summary"]["overflow_total"] == 0 else 1


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="serial port, for example /dev/cu.usbmodem14401")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--timeout", type=float, default=15.0)
    parser.add_argument("--label", default="timing_session")
    parser.add_argument("--duration-ms", type=int, default=100)
    parser.add_argument("--repeat", type=int, default=3)
    parser.add_argument("--manual-trigger", action="store_true")
    parser.add_argument("--pre-delay-ms", type=int, default=0)
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("captures/experiments/timing_sessions"),
    )
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if args.repeat <= 0:
        print("error: --repeat must be > 0", file=sys.stderr)
        return 2
    if args.duration_ms <= 0 or args.duration_ms > 250:
        print("error: --duration-ms must be between 1 and 250", file=sys.stderr)
        return 2
    if args.pre_delay_ms < 0:
        print("error: --pre-delay-ms must be >= 0", file=sys.stderr)
        return 2
    try:
        return run(args)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
