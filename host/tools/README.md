# Stable Host Tools

## 1. Objective

Define the future home for stable command-line tools that operate on the ESP32-P4 signal lab.

This matters because users and automation need reliable commands, while experimental scripts should remain available without being mistaken for stable interfaces.

## 2. Current Understanding

Current stable-tool candidates from `host/`:

| Script | Proposed role | Notes |
|---|---|---|
| `gbc_probe.py` | serial probe CLI | Keep name for now; later add generic alias. |
| `record_experiment.py` | experiment folder recorder | Should be target-profile aware. |
| `validate_phase1_measurements.py` | electrical checklist validator | Useful across targets. |
| `analyze_timing_edges.py` | timing capture CLI | Generic timing tool candidate. |
| `analyze_timing_relationships.py` | timing report generator | Generic report tool candidate. |
| `export_pulseview.py` | VCD export | Generic artifact exporter. |
| `capture_lcdcam_raw.py` | raw LCD_CAM capture | Generic capture backend candidate. |
| `decode_lcdcam_fast.py` | fast frame decoder | Keep GBC presets target-scoped later. |
| `live_lcdcam_stream_viewer.py` | browser workbench server | Move under `host/workbench/` after wrappers exist. |
| `host/tools/validate_profile.py` | profile validator | New generic tool; dependency-free baseline checks. |

Confidence level: medium-high for the first seven; medium for decode/viewer split until the browser is reorganized.

## 3. Unknowns

- Final command names.
- Whether stable tools should be thin wrappers around `host/lab/`.
- How to expose target presets without hardcoding GBC names in generic commands.

## 4. Experiment Results

2026-05-10: Directory added as an index only. No scripts were moved, so current commands and approved flash/viewer workflows remain valid.

## 5. Next Steps

- Add compatibility wrapper scripts here after `host/lab/` APIs exist.
- Document each stable command with examples and output artifacts.
- Preserve old top-level `host/*.py` entry points until the browser and capture workflows are verified after the move.

Validate the active profile:

```sh
python host/tools/validate_profile.py profiles/gbc_lcd.json
```
