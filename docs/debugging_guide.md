# Debugging Guide

## 1. Objective

Define debugging practices for firmware, host tools, timing captures, and frame reconstruction.

This matters because observability is required from the beginning of the project.

## 2. Current Understanding

Current hypothesis: every critical module should expose diagnostics for timing, overflow, dropped samples, synchronization loss, and command responses. The initial diagnostics snapshot includes command counters, capture pin count, dropped sample count, overflow count, and synchronization loss count. Host smoke tests should verify these after each firmware flash.

Evidence: these requirements are listed in `PROJECT_CHARTER.md`.

Confidence level: high for desired behavior, low for concrete implementation until modules exist.

## 3. Unknowns

- Firmware logging backend.
- USB command error format.
- Diagnostic counter definitions.
- Host-side validation checks.
- Minimum useful capture metadata.

## 4. Experiment Results

2026-05-06: Added basic firmware diagnostics exposed through the `EXPORT_STATS` command.

2026-05-06: Flashing can reach both detected serial ports, but esptool receives no ROM loader response. Use manual download mode before retrying.

2026-05-06: Flashing succeeded after the board re-enumerated as `/dev/cu.usbmodem14201`.

2026-05-06: USB-Serial/JTAG command verification succeeded after making USB-Serial/JTAG the primary console.

2026-05-06: Added `host/gbc_probe.py smoke` as the repeatable command-path smoke test.

2026-05-06: Host smoke test passed on `/dev/cu.usbmodem14201`.

2026-05-06: Experiment recorder smoke test passed and saved raw JSON responses under `captures/experiments/20260505T223950Z-phase1-baseline-smoke/smoke_test.json`.

2026-05-06: Smoke tests now verify that required capture commands are present but blocked during Phase 1 with `no_capture_pins_configured`.

2026-05-06: Expanded smoke test passed after flashing the command-scaffolding firmware.

2026-05-06: Added `READ_GPIO 33` as an allowlisted input-only test command for board interaction checks.

2026-05-06: Verified `READ_GPIO 33` succeeds and non-allowlisted `READ_GPIO 32` is rejected.

2026-05-07: Added `MEASURE_DCLK <gpio> <duration_ms>` for PCNT-backed rising-edge frequency checks. Use this instead of `COUNT_GPIO_EDGES` for MHz-class clock candidates.

2026-05-07: Added `CAPTURE_TIMING_EDGES <duration_ms>` and host analyzer. A 100 ms run produced 5456 events with zero overflow.

2026-05-07: Added `host/capture_timing_session.py` for repeated timing captures and manual-trigger coordination. A two-run steady-state dry run completed with zero overflow and stable LP/SPL frame counts.

2026-05-07: Manual boot session with 250 ms pre-delay missed the active LCD bus on two off-to-on attempts, while an already-on control capture worked. Use longer pre-delay values for boot captures.

2026-05-08: Added `host/postprocess_dvp_rg44.py` for visual post-processing of saved red/green RAW8 `.bin` captures. Example: `python3 host/postprocess_dvp_rg44.py captures/decoded/dvp_raw/<capture>.bin --mode sheet --x-shifts 0,8,16,24 --line-shifts -4,-3,-2,-1,0,1,2,3,4`.

2026-05-08: Added `host/debug_dvp_capture_viewer.py`, an offline browser viewer for saved red/green RAW8 `.bin` captures. It provides sliders for X shift, Y shift, line skew, fine line skew, invert, bit order, and red/green channel swap. This viewer does not access the ESP32-P4 or start DVP capture, so it is preferred while the live capture power-loading issue is unresolved.

2026-05-09: Added a browser Power-Cycle Monitor to `host/live_lcdcam_stream_viewer.py`. This mode stops live LCD_CAM capture, issues `SAFE_IDLE`, then repeatedly samples timing/control GPIO edge counts for `DCLK`, `SPS`, `SPL`, `LP`, `PS`, and `CLS`.

Use this when the target fails to power back on cleanly while connected. Start with the target off, click `Start Monitor`, then switch the target on. The result is a coarse state timeline classified as `off`, `clock_only`, `frame_no_line`, `line_no_frame`, `locked`, or `unstable`. It saves `raw.json`, `samples.csv`, and `summary.json` under `captures/experiments/<timestamp>-power_cycle_monitor/`.

Known limitation: each GPIO is counted sequentially, so this is not a phase-accurate logic analyzer. It is intended to separate power/source-start behavior from LCD_CAM capture load.

## 5. Next Steps

- Keep `host/gbc_probe.py smoke` passing after every firmware flash.
- For DCLK-like signals, prefer `MEASURE_DCLK <gpio> <duration_ms>` over GPIO interrupt edge counting.
- For relative sync timing, prefer `host/analyze_timing_edges.py`; check `overflow_count` before trusting the summary.
- For coordinated power-cycle captures, use `host/capture_timing_session.py --manual-trigger` and follow the terminal prompts.
- If a manual boot capture returns zero events but an already-on capture works, increase `--pre-delay-ms`.
- For flashing failures with `No serial data received`, hold `BOOT`, tap `RESET`, release `BOOT`, rescan `/dev/cu.*`, and retry the newly enumerated port.
- Define command response and error conventions.
- Add diagnostics from the first firmware modules instead of retrofitting later.
- Make host tools preserve raw inputs when decoding fails.
- Use `host/postprocess_dvp_rg44.py` to test X/Y shifts and per-line skew before changing firmware capture timing.
- Use `host/debug_dvp_capture_viewer.py captures/decoded/dvp_raw/<capture>.bin --listen-port 8767` for interactive slider-based inspection of saved captures.
- Use the browser Power-Cycle Monitor before deeper electrical changes when the target fails to restart. Compare a failed start and a successful start by their `samples.csv` timelines.
