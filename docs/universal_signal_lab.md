# Universal Signal Lab

## 1. Objective

Define the reusable architecture for using the ESP32-P4 as a console picture-signal reverse engineering lab.

This matters because the Game Boy Color LCD bus is the first target, not the whole system. The same firmware, host tooling, artifact model, and AI-assisted workflow should help investigate other console video or display buses later.

## 2. Current Understanding

Current hypothesis: the project should be split into a generic capture platform plus target profiles.

Generic platform responsibilities:

- safe passive observation
- GPIO and peripheral input configuration
- timestamp and edge capture
- DMA/raw byte capture
- binary transport
- reproducible artifact storage
- hypothesis-driven decoding
- browser inspection tools
- staged investigation workbench for pin discovery, timing analysis, role classification, raw capture, and hypothesis testing
- recovery and safe-idle controls

Target profile responsibilities:

- connector pinout
- voltage and electrical notes
- signal names
- known dangerous rails
- candidate role mapping
- capture presets
- decode hypotheses
- confidence/evidence tracking

Target modules are allowed to contain console-specific knowledge, but they should not own reusable instrument machinery. For example, `gbc_lcd` may define that `SPS` is the current frame-marker candidate and that `CLS` is wired to GPIO3 on this bench setup. It should not define the generic meaning of a timing-edge capture, VCD export, serial transport, or browser artifact manifest.

Once a target produces stable visual evidence, the target module should be allowed to grow a source driver. That is different from the initial generic investigation path. The generic path answers "what is this bus doing?" The source driver answers "given what we now know, capture this bus at speed with deterministic telemetry."

For GBC, that means preserving the investigation commands but adding a GBC LCD source driver that can keep LCD_CAM/GDMA active, stream compact binary frames, and report sync/capture health without rerunning all discovery logic every frame.

Product/export modules are a third layer. They should consume platform blocks and target profiles after the reverse-engineered behavior is stable enough. Examples include:

- retimer
- screen replacement bridge
- scaler
- recorder
- protocol analyzer
- AI-assisted review pack generator

The repeatable source -> ESP32-P4 processing -> destination method is defined in `docs/system_method.md`. The current project gap assessment against that method is `docs/system_gap_assessment.md`.

AI-facing workflow responsibilities:

- emit machine-readable target profile references with every capture
- keep raw bytes, decoded images, and timing statistics linked by one manifest
- generate thumbnails/contact sheets for fast visual triage
- record the active hypothesis in plain terms: clock edge, row stride, crop, bit packing, marker roles, and color operations
- mark anomalies explicitly instead of hiding them in filenames
- preserve enough context that a later AI or human review can compare hypotheses without rerunning the hardware capture

Evidence: the GBC work already showed that generic assumptions such as `VSYNC/HSYNC/DE/PCLK` can be wrong even when the picture eventually becomes recognizable. The useful breakthrough came from preserving raw data, testing hypotheses offline, and iterating capture roles without hardcoding one interpretation.

Confidence level: high that the profile split is required; medium for exact file formats and APIs.

## 3. Unknowns

- Final target profile schema.
- Whether target profiles should be JSON, Python modules, C headers, or a combination.
- How much of the ESP32-P4 LCD_CAM/GDMA setup can be made target-agnostic.
- How to represent analog/composite/component signals if the platform later expands beyond digital picture buses.
- Whether AI-assisted analysis should operate on raw frames, timing metadata, decoded screenshots, or all of them.
- How to score confidence for hypotheses across very different consoles.

## 4. Experiment Results

2026-05-09: Lessons from the GBC target were generalized into the reusable signal-lab model.

Key lessons:

- Do not assume standard video semantics. A console picture bus may not map cleanly to `VSYNC`, `HSYNC`, `DE`, and `PCLK`.
- Capture first, interpret later. Raw artifacts must preserve enough data to test alternate row widths, frame periods, sample edges, bit packing, and blanking models.
- Visual success is evidence, not proof. A readable image can still have wrong color bits, unstable sample edges, or hidden line-order mistakes.
- Sample edge must be proven. On the GBC target, one DCLK edge produced cyan sparkle from unstable `G5`; the other edge produced stable text.
- Blanking and porch-like regions should be measured, not assumed. The GBC target currently looks like `160` visible transfers plus one trailing dummy/blank byte, but this remains a hypothesis.
- Transport and recovery are part of the lab tool. Live inspection needs `Stop`, `Safe Idle`, and `Recover`, especially when the target console is power-cycled.
- Target-specific physical behavior matters. Missing original display loads, test-pin straps, and panel-side biasing may affect boot and signal quality.

Reusable artifact metadata should include:

- target profile name and version
- physical wiring map
- electrical confidence level
- capture command and firmware version
- sample clock role and edge
- candidate line/frame marker roles
- raw width/height or byte count
- target-specific decode hypothesis
- checksum/statistics
- rendered PNG or live-view settings
- notes about power state and original display loading

2026-05-09: Added `profiles/` with `profiles/gbc_lcd.json` as the first machine-readable target profile. This file records GBC wiring, dangerous rails, candidate signal roles, the active LCD_CAM RGB565 capture preset, and open questions without making the generic lab architecture GBC-only.

2026-05-09: Added `docs/investigation_workbench.md` to define the browser and host-tool workflow as an investigation instrument. The key correction is that the UI should expose the reverse-engineering process itself: pin inspection, edge/activity scans, clock measurement, timing captures, relationship analysis, raw capture, hypothesis comparison, and AI review packs. The live image view is only one module in that larger tool.

## 5. Next Steps

- Keep `profiles/gbc_lcd.json` synchronized with wiring, capture, and decode discoveries.
- Mark source profiles with a graduation status and current performance path.
- Add a GBC LCD source-driver module once the current live baseline is preserved.
- Draft a formal profile schema after one more target or capture mode proves which fields are truly generic.
- Build the browser UI around the workbench modules in `docs/investigation_workbench.md`, keeping the existing GBC live view as a compatibility-preserved module.
- Rename or wrap host tools so generic capture modules are not permanently named only around GBC.
- Keep GBC-specific scripts working through compatibility aliases while introducing generic names.
- Define a common raw artifact manifest format.
- Add a hypothesis report format that can compare multiple profile-specific decodes.
- Add AI-facing summaries for each capture: thumbnails, timing stats, signal role assumptions, and anomaly notes.
- Add an electrical checklist for each target profile: voltage domains, safe lines, original load/termination, straps, and power sequencing.
- Preserve `docs/project_maintenance.md` as the cleanup gate so useful GBC evidence is summarized before scripts or artifacts are moved.
