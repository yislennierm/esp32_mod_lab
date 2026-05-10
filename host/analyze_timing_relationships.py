#!/usr/bin/env python3
"""Analyze frame/line relationships from a timing-edge capture JSON file."""

from __future__ import annotations

import argparse
import json
import statistics
import sys
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


SIGNALS = ("SPL", "PS", "LP", "CLS", "SPS")


def newest_capture(default_dir: Path) -> Path:
    captures = sorted(default_dir.glob("*-timing_edges_*ms.json"))
    if not captures:
        raise FileNotFoundError(f"no timing-edge captures found in {default_dir}")
    return captures[-1]


def load_events(path: Path) -> list[dict[str, Any]]:
    data = json.loads(path.read_text(encoding="utf-8"))
    events = data.get("events")
    if not isinstance(events, list):
        raise ValueError(f"{path} does not contain an events list")
    return events


def event_times(events: list[dict[str, Any]], signal: str, level: int | None = None) -> list[int]:
    result = []
    for event in events:
        if event.get("signal") != signal:
            continue
        if level is not None and event.get("level") != level:
            continue
        result.append(int(event["t_us"]))
    return result


def in_window(events: list[dict[str, Any]], start_us: int, end_us: int) -> list[dict[str, Any]]:
    return [event for event in events if start_us <= int(event["t_us"]) < end_us]


def first_time_after(events: list[dict[str, Any]], signal: str, start_us: int) -> int | None:
    for event in events:
        if event.get("signal") == signal and int(event["t_us"]) >= start_us:
            return int(event["t_us"])
    return None


def last_time_before(events: list[dict[str, Any]], signal: str, end_us: int) -> int | None:
    last = None
    for event in events:
        if int(event["t_us"]) >= end_us:
            break
        if event.get("signal") == signal:
            last = int(event["t_us"])
    return last


def phase_offsets(events: list[dict[str, Any]], reference_signal: str, target_signal: str, max_offset_us: int = 80) -> list[int]:
    reference_times = event_times(events, reference_signal)
    target_times = event_times(events, target_signal)
    offsets = []
    target_index = 0
    for reference_time in reference_times:
        while target_index < len(target_times) and target_times[target_index] < reference_time:
            target_index += 1
        if target_index >= len(target_times):
            break
        offset = target_times[target_index] - reference_time
        if 0 <= offset <= max_offset_us:
            offsets.append(offset)
    return offsets


def summarize_offsets(offsets: list[int]) -> dict[str, Any]:
    if not offsets:
        return {"count": 0, "mean_us": None, "min_us": None, "max_us": None}
    return {
        "count": len(offsets),
        "mean_us": statistics.fmean(offsets),
        "min_us": min(offsets),
        "max_us": max(offsets),
    }


def analyze(events: list[dict[str, Any]]) -> dict[str, Any]:
    events = sorted(events, key=lambda event: int(event["t_us"]))
    sps_rising = event_times(events, "SPS", level=1)
    frames = []

    for index, (start_us, end_us) in enumerate(zip(sps_rising, sps_rising[1:])):
        frame_events = in_window(events, start_us, end_us)
        counts = Counter(event["signal"] for event in frame_events)
        level_counts = {
            signal: Counter(
                event["level"] for event in frame_events if event["signal"] == signal
            )
            for signal in SIGNALS
        }

        lp_times = event_times(frame_events, "LP")
        spl_times = event_times(frame_events, "SPL")
        cls_times = event_times(frame_events, "CLS")
        ps_times = event_times(frame_events, "PS")

        frames.append(
            {
                "index": index,
                "start_us": start_us,
                "end_us": end_us,
                "duration_us": end_us - start_us,
                "counts": {signal: counts.get(signal, 0) for signal in SIGNALS},
                "level_counts": {
                    signal: {
                        "level_0": level_counts[signal].get(0, 0),
                        "level_1": level_counts[signal].get(1, 0),
                    }
                    for signal in SIGNALS
                },
                "first_lp_after_sps_us": (lp_times[0] - start_us) if lp_times else None,
                "last_lp_before_next_sps_us": (end_us - lp_times[-1]) if lp_times else None,
                "first_spl_after_sps_us": (spl_times[0] - start_us) if spl_times else None,
                "last_spl_before_next_sps_us": (end_us - spl_times[-1]) if spl_times else None,
                "first_cls_after_sps_us": (cls_times[0] - start_us) if cls_times else None,
                "first_ps_after_sps_us": (ps_times[0] - start_us) if ps_times else None,
            }
        )

    relationship_offsets = {
        "LP_to_SPL_next_us": summarize_offsets(phase_offsets(events, "LP", "SPL")),
        "LP_to_CLS_next_us": summarize_offsets(phase_offsets(events, "LP", "CLS")),
        "LP_to_PS_next_us": summarize_offsets(phase_offsets(events, "LP", "PS")),
        "CLS_to_LP_next_us": summarize_offsets(phase_offsets(events, "CLS", "LP")),
        "PS_to_LP_next_us": summarize_offsets(phase_offsets(events, "PS", "LP")),
    }

    lp_counts = [frame["counts"]["LP"] for frame in frames]
    spl_counts = [frame["counts"]["SPL"] for frame in frames]
    return {
        "event_count": len(events),
        "sps_rising_count": len(sps_rising),
        "complete_frame_count": len(frames),
        "frame_duration_mean_us": statistics.fmean(frame["duration_us"] for frame in frames)
        if frames
        else None,
        "lp_count_mean_per_frame": statistics.fmean(lp_counts) if lp_counts else None,
        "spl_count_mean_per_frame": statistics.fmean(spl_counts) if spl_counts else None,
        "frames": frames,
        "relationship_offsets": relationship_offsets,
    }


def write_markdown(path: Path, source: Path, summary: dict[str, Any]) -> None:
    lines = [
        "# Timing Relationship Report",
        "",
        f"Source: `{source}`",
        "",
        "## Summary",
        "",
        f"- Events: {summary['event_count']}",
        f"- SPS rising edges: {summary['sps_rising_count']}",
        f"- Complete SPS-to-SPS frames: {summary['complete_frame_count']}",
        f"- Mean frame duration: {summary['frame_duration_mean_us']} us",
        f"- Mean LP count per frame: {summary['lp_count_mean_per_frame']}",
        f"- Mean SPL count per frame: {summary['spl_count_mean_per_frame']}",
        "",
        "## Frames",
        "",
        "| Frame | Duration us | LP | SPL | CLS | PS | SPS | First LP us | Last LP Gap us | First SPL us | Last SPL Gap us |",
        "|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|",
    ]

    for frame in summary["frames"]:
        counts = frame["counts"]
        lines.append(
            f"| {frame['index']} | {frame['duration_us']} | {counts['LP']} | {counts['SPL']} | "
            f"{counts['CLS']} | {counts['PS']} | {counts['SPS']} | "
            f"{frame['first_lp_after_sps_us']} | {frame['last_lp_before_next_sps_us']} | "
            f"{frame['first_spl_after_sps_us']} | {frame['last_spl_before_next_sps_us']} |"
        )

    lines.extend(
        [
            "",
            "## Relative Ordering",
            "",
            "| Relationship | Count | Mean us | Min us | Max us |",
            "|---|---:|---:|---:|---:|",
        ]
    )
    for name, row in summary["relationship_offsets"].items():
        lines.append(
            f"| {name} | {row['count']} | {row['mean_us']} | {row['min_us']} | {row['max_us']} |"
        )

    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def run(args: argparse.Namespace) -> int:
    source = args.input or newest_capture(args.input_dir)
    events = load_events(source)
    summary = analyze(events)

    output_dir = args.output_dir
    output_dir.mkdir(parents=True, exist_ok=True)
    stem = source.stem.replace("timing_edges", "timing_relationships")
    json_path = output_dir / f"{stem}.json"
    md_path = output_dir / f"{stem}.md"

    json_path.write_text(json.dumps(summary, indent=2, sort_keys=True), encoding="utf-8")
    write_markdown(md_path, source, summary)

    print(f"source={source}")
    print(f"events={summary['event_count']}")
    print(f"complete_frames={summary['complete_frame_count']}")
    print(f"mean_frame_duration_us={summary['frame_duration_mean_us']}")
    print(f"mean_lp_count_per_frame={summary['lp_count_mean_per_frame']}")
    print(f"mean_spl_count_per_frame={summary['spl_count_mean_per_frame']}")
    print(f"summary_json={json_path}")
    print(f"report_md={md_path}")
    return 0 if summary["complete_frame_count"] > 0 else 1


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", nargs="?", type=Path, help="timing-edge raw JSON file")
    parser.add_argument(
        "--input-dir",
        type=Path,
        default=Path("captures/decoded/timing_edges"),
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("captures/decoded/timing_relationships"),
    )
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
