# Investigation Workbench

## 1. Objective

Define the browser and host-tool workflow for using the ESP32-P4 as an investigation instrument for unknown console picture signals.

This matters because the project should help repeat the whole reverse-engineering journey on future targets: discover safe pins, identify active signals, measure timing, classify candidate roles, test capture hypotheses, and preserve evidence. The live image viewer is only one late-stage panel in that workflow.

The workbench should follow the broader source -> ESP32-P4 processing -> destination method documented in `docs/system_method.md`.

## 2. Current Understanding

Current hypothesis: the browser UI should become a front end for staged investigation tools, backed by the existing Python host commands and ESP32-P4 firmware primitives.

The first GBC target taught this investigation sequence:

1. Establish communication with the ESP32-P4.
2. Keep all candidate pins input-only.
3. Read static GPIO levels.
4. Count slow edges to find active control signals.
5. Use PCNT-style measurement for MHz-class clock candidates.
6. Timestamp slower timing/control edges over bounded captures.
7. Analyze relationships between candidate frame, line, and control signals.
8. Correlate timing events with a small data subset.
9. Capture raw pixel/data bursts without assuming standard video semantics.
10. Render many hypotheses offline before promoting one to live capture.
11. Verify sample edge and color-bit stability.
12. Save enough raw artifacts and metadata that the conclusion can be challenged later.

Evidence: the GBC work used `READ_GPIO`, `COUNT_GPIO_EDGES`, `MEASURE_DCLK`, `CAPTURE_TIMING_EDGES`, timing analyzers, line-clock captures, raw LCD_CAM captures, offline renderers, browser sliders, and repeated sample-edge comparisons before reaching the current RGB565 live view.

Confidence level: high that this staged workbench model matches the project; medium for exact UI layout and automation sequence.

## 3. Unknowns

- Which firmware commands should remain generic and which should be profile-specific adapters.
- How to safely allow arbitrary GPIO investigation from the browser without exposing dangerous commands.
- Whether pin discovery should be driven by an explicit profile allowlist, a temporary session allowlist, or both.
- How much automatic signal classification is reliable across different consoles.
- How to represent confidence when a signal visually works but its protocol role is not proven.
- How to support analog, composite, or sync-embedded video later if the current ESP32-P4 path starts with digital buses.

## 4. Experiment Results

2026-05-09: The first tabbed browser UI was added around the working GBC live view. This proved profile loading and safe browser-to-host control paths, but it is still viewer-centered.

2026-05-09: The user clarified the real target: a research workbench that exposes the investigation process itself. The UI should surface the commands and analysis that found the current GBC timing model, not just display the final decoded image.

2026-05-09: Implemented the first browser-backed workbench features inside `host/live_lcdcam_stream_viewer.py` while preserving the existing GBC live view.

Implemented modules:

- `Safety`: renders dangerous rails, do-not-connect signals, known concerns, and allowed investigation GPIOs from the active profile.
- `Pins`: reads all profile-connected GPIO levels, scans profile GPIO edge counts, measures a selected clock candidate, and renders a rolling level timeline.
- `Timing`: captures timing edges, computes timing summaries and relationship analysis, writes artifacts, and captures line-clock snapshots from profile line-marker candidates.
- `Logs`: keeps safe probe commands and action output visible in the browser.

Implemented HTTP endpoints:

- `/api/workbench/gpios`
- `/api/workbench/read-gpios`
- `/api/workbench/count-edges-all?duration_ms=<ms>`
- `/api/workbench/measure-clock?gpio=<gpio>&duration_ms=<ms>`
- `/api/workbench/capture-timing?duration_ms=<1..250>`
- `/api/workbench/line-clocks?marker=<LP|SPL>&edge=<falling|rising>&line_count=<n>&timeout_ms=<ms>`

Safety behavior:

- GPIO operations are restricted to GPIOs present in the active target profile.
- Line-clock marker choices are restricted to profile line-marker candidates.
- The browser still uses allowlisted firmware commands instead of exposing a free-form command console.
- Timing and line-clock captures save artifacts under `captures/experiments/<timestamp>-<profile>-<kind>/`.

Verification:

- `MEASURE_DCLK GPIO22 100 ms` returned about `1.388 MHz`.
- `CAPTURE_TIMING_EDGES 20` returned `1069` events with `overflow_count=0` and wrote raw JSON, CSV, summary JSON, and relationship JSON artifacts.
- `CAPTURE_LINE_CLOCKS SPL falling 8 1000` returned 8 samples and wrote raw/summary artifacts.

2026-05-09: Added the first pin-level timeline view to the `Pins` tab. It repeatedly samples `/api/workbench/read-gpios`, draws one row per profile GPIO, and colors cells by level: high, low, or read error. This is a logic-analyzer-style display for slow human-visible state changes, power sequencing, and coarse signal activity; it is not a true sampled logic analyzer and is not a substitute for edge counting, PCNT clock measurement, or timing capture.

2026-05-09: Refined the browser layout model. The left side is now the viewer/result surface: timeline, tables, timing reports, live image, profile data, and logs. The right side is a contextual control surface: dashboard controls, pin/timeline parameters, timing parameters, live decode parameters, or log commands depending on the active tab.

2026-05-09: Added PulseView-oriented VCD export for timing captures.

2026-05-10: Reorganized the browser top-level navigation around the source -> ESP32-P4 processing -> destination method while preserving the working GBC live path.

Current top-level tabs:

- `Project`
- `Source`
- `Processing`
- `Destination`
- `Live`
- `Artifacts`
- `Profile`
- `Logs`

The previous `Safety`, `Pins`, `Timing`, and `Signals` surfaces now live under `Source`. `Live` keeps the current frame monitor. `Processing` and `Destination` are visible placeholders for the future product pipeline. `Artifacts` lists recent capture folders from `captures/experiments/`.

Added endpoint:

- `/api/artifacts/recent`

Implemented:

- `host/export_pulseview.py` converts timing-edge raw JSON captures into VCD.
- Workbench timing captures now write `timing_edges.vcd` next to `raw.json`, `events.csv`, `summary.json`, and `relationships.json`.
- Exported VCD uses a `1 us` timescale and includes timing/control signals plus the optional `red6` sampled data bus when present.

Current limitation: this is event-based timing/control export from `CAPTURE_TIMING_EDGES`. It is useful for inspecting SPS/LP/SPL/CLS/PS relationships in PulseView-like tools, but it is not a full-rate DCLK sampled logic-analyzer recording.

Required workbench modules:

### Dashboard

Purpose: establish that the ESP32-P4 instrument is alive and controllable.

Controls and outputs:

- connect or reconnect serial transport
- `PING`
- `GET_VERSION`
- `EXPORT_STATS`
- firmware/build/profile version
- current target profile
- safe-idle state
- last error and recovery action

### Safety And Pin Setup

Purpose: prevent accidental electrical or firmware mistakes before measurement.

Controls and outputs:

- active target profile dangerous rails
- do-not-connect list
- currently allowed investigation GPIOs
- GPIO direction and pull state
- input-only verification
- `SAFE_IDLE`
- original-load and strap checklist
- per-pin notes for measured voltage and confidence

### Pin Inspector

Purpose: progressively map unknown console pins to ESP32-P4 GPIOs.

Controls and outputs:

- `READ_GPIO <gpio>`
- static level table
- optional repeated sampling over time
- manual label assignment: unknown, clock candidate, line candidate, frame candidate, data candidate, power/control, unsafe
- profile update suggestions, not automatic rewrites

### Activity Scanner

Purpose: find which connected pins are alive without assuming their meaning.

Controls and outputs:

- `COUNT_GPIO_EDGES <gpio> <duration_ms>` for low and medium-rate candidates
- rising/falling/total edge counts
- static level after measurement
- activity ranking table
- warning when edge rate is too high for ISR counting
- one-click promotion to clock measurement for fast candidates

### Clock Measurement

Purpose: measure MHz-class clock candidates with peripheral-backed counters.

Controls and outputs:

- `MEASURE_DCLK <gpio> <duration_ms>` or future generic `MEASURE_CLOCK <gpio> <duration_ms>`
- measured frequency
- stability over repeated windows
- comparison against expected display, line, or frame rates
- notes when measured frequency suggests gated pixel transfer rather than full internal dot clock

### Timing Capture

Purpose: timestamp relationships between candidate sync/control signals.

Controls and outputs:

- `CAPTURE_TIMING_EDGES <duration_ms>`
- raw event count and overflow flag
- per-signal edge rates
- frame-to-frame interval table
- candidate line count per frame
- first line/control edge after frame marker
- downloadable JSON, CSV, and summary report

### Relationship Analyzer

Purpose: classify candidate roles without relying on names such as HSYNC or VSYNC.

Controls and outputs:

- frame marker candidates ranked by stable low-frequency cadence
- line marker candidates ranked by count per frame and jitter
- data-enable candidates ranked by relation to pixel bursts
- warnings for duplicate/narrow ISR observations
- confidence score with evidence links
- ability to compare roles from the same raw timing capture

### Data Correlation

Purpose: test whether timing markers line up with pixel/data changes.

Controls and outputs:

- timing events with sampled data subset
- per-marker data snapshot statistics
- line-burst captures keyed from candidate markers
- phase/delay scans after marker edges
- data-bit activity table

### Raw Capture Lab

Purpose: capture bytes or samples before final interpretation.

Controls and outputs:

- LCD_CAM/GDMA raw capture presets from the active profile
- immediate, after-frame-marker, and marker-combination start modes
- sample edge selection
- data packing mode
- raw width/height or byte count
- checksum, transition count, dropped/overflow indicators
- artifact save with manifest

### Hypothesis Lab

Purpose: apply multiple interpretations to the same raw data.

Controls and outputs:

- row stride
- visible crop
- frame offset
- line skew
- bit masks
- channel order
- per-channel invert and bit reverse
- sample-edge annotations
- contact sheet generation
- confidence notes
- promote selected settings into the target profile

### AI Review Pack

Purpose: create a compact, reproducible evidence bundle for AI-assisted analysis.

Outputs:

- target profile ID and version
- wiring map
- command history
- raw captures
- timing reports
- rendered thumbnails/contact sheets
- active hypotheses
- known anomalies
- open questions
- human observation notes

## 5. Next Steps

- Keep the current GBC live viewer working, but treat it as the `Live View` module inside a larger workbench.
- Add browser tabs for `Safety`, `Pin Inspector`, `Activity Scanner`, `Clock Measurement`, `Timing Capture`, `Relationship Analyzer`, `Raw Capture`, `Hypotheses`, and `AI Review`.
- Split the current first-pass `Pins` and `Timing` tabs into richer dedicated modules as complexity grows.
- Add profile-backed allowlists so the browser can safely run investigation commands only on known connected GPIOs.
- Rename host tools toward generic names while keeping GBC compatibility wrappers.
- Add a common experiment manifest format before adding more live controls.
- Expose existing analyzers through HTTP endpoints instead of duplicating logic in JavaScript.
- Make every workbench action able to save an artifact under `captures/experiments/<timestamp>/`.
- Add a "promote to profile" workflow that drafts profile changes but does not silently rewrite target assumptions.
- Investigate native sigrok session export (`.sr`) after the event-based VCD workflow is stable.
