# Project Maintenance And Modularization Plan

## 1. Objective

Define how to clean, organize, and evolve the project without losing the technical journey.

This matters because the project has moved from a GBC-specific capture experiment into a reusable ESP32-P4 console picture-signal lab. Cleanup must preserve evidence, lessons learned, and working GBC behavior while making room for reusable capture blocks that can later become retimers, screen mods, scalers, or other product modules.

## 2. Current Understanding

Current hypothesis: the codebase should be organized around three layers:

- A reusable ESP32-P4 instrument platform.
- Target modules such as the Game Boy Color LCD bus.
- Product/export modules built from proven capture and decode blocks.

The GBC target is not "the project"; it is the first target module and the reference case. It should remain fully working while its lessons are extracted into generic interfaces and repeatable workflows.

Evidence:

- The working GBC live capture required target-specific wiring, GPIO avoidance, timing hypotheses, color packing, sample-edge testing, and power-cycle lessons.
- The useful tools were generic in spirit: GPIO read, edge counts, PCNT clock measurement, timing-edge capture, raw LCD_CAM capture, hypothesis rendering, browser inspection, and artifact logging.
- The GPIO32 backfeed issue proved that hardware lessons are part of the reusable platform, not just incidental GBC debugging.

Confidence level: high for the layered direction; medium for exact filenames and APIs until the next target or capture mode validates the split.

## 3. Unknowns

- Which current host scripts should become stable CLI tools and which should be archived as experiment prototypes.
- Whether the target profile schema should remain JSON-only or generate firmware headers and host settings.
- How to split firmware modules between generic instrument code and target-specific adapters.
- Whether LCD_CAM/DVP is the long-term capture backend for all parallel digital buses or one backend among several.
- How much cleanup should be done before the next capture milestone.

## 4. Experiment Results

2026-05-09: The GBC target reached a stable enough point for maintenance work: live browser capture works, power cycling while capturing is now stable after moving `CLS` from GPIO32 to GPIO3, and the project has a machine-readable profile at `profiles/gbc_lcd.json`.

2026-05-09: Current repository scan found generated and temporary artifacts mixed with source:

- ESP-IDF build outputs exist at both `build_esp32p4/` and `firmware/build_esp32p4/`.
- Python caches exist under `host/__pycache__/`.
- `.DS_Store` files exist in multiple directories.
- Many host scripts encode valuable experiment history but are not yet classified as stable tools, target-specific tools, or archived prototypes.

No cleanup deletion should happen until this plan is reviewed, because some generated captures and scripts are still useful evidence.

2026-05-10: Maintenance Phase A/B started non-destructively:

- Added directory indexes for `host/lab/`, `host/tools/`, `host/workbench/`, `host/targets/gbc_lcd/`, and `host/experiments/`.
- Added `docs/host_script_inventory.md` to classify current host scripts before moving anything.
- Added `docs/AI_CONTEXT.md`, `docs/DECISIONS.md`, and `docs/DOCS_INDEX.md` to make documentation easier for AI and humans to navigate.
- Added `profiles/schema/target_profile.schema.json` as a permissive draft schema for target profiles.
- Added `host/tools/validate_profile.py` as a dependency-free profile sanity checker.
- Added `docs/artifacts/manifest_format.md` to define the future common capture manifest.
- Added `docs/gbc_lcd_journey.html` as a visual web-ready preservation report.

No scripts, firmware files, or capture artifacts were moved or deleted during this step.

## 5. Next Steps

### Cleanup Policy

Do not delete experiment evidence during normal cleanup. Move or archive first, then remove only after a documented replacement exists.

Recommended cleanup classes:

| Class | Examples | Action |
|---|---|---|
| Generated build outputs | `build_esp32p4/`, `firmware/build_esp32p4/`, `firmware/build/` | Keep out of source control; regenerate when needed |
| Local OS/cache files | `.DS_Store`, `__pycache__/`, `*.pyc` | Safe to delete after confirming no process is running |
| Raw evidence | `captures/raw/`, `captures/decoded/`, `captures/experiments/` | Preserve or move to an artifact archive; do not delete casually |
| Stable source | firmware modules, host tools, profiles, docs | Keep reviewed and documented |
| Prototype scripts | one-off renderers, scans, debug viewers | Keep initially; later move under `host/experiments/` or `archive/` with notes |

### Proposed Repository Shape

Target structure:

```text
firmware/
  main/
    instrument/          generic GPIO, timing, LCD_CAM, transport, diagnostics
    targets/gbc_lcd/     target-specific pin defaults and capture presets, if firmware-side target code is needed
    products/            future reusable runtime blocks: retimer, scaler, bridge

host/
  lab/                   stable reusable host APIs
  tools/                 stable command-line tools
  workbench/             browser workbench server and UI assets
  targets/gbc_lcd/       GBC-specific decoders, presets, compatibility wrappers
  experiments/           prototype scripts retained for traceability

profiles/
  gbc_lcd.json           first target module profile
  schema/                future profile schema and validation rules

captures/
  experiments/           reproducible sessions and manifests
  raw/                   raw capture bytes/traces
  decoded/               rendered outputs and contact sheets
  archive/               optional local-only cold storage

docs/
  targets/gbc_lcd/       future split for GBC-specific docs
  platform/              future split for generic ESP32-P4 lab docs
```

This is a direction, not an immediate refactor mandate. The first refactor should add wrappers and move only low-risk files.

### GBC As A Target Module

The GBC module should contain:

- Connector pinout and dangerous rails.
- Current working wiring, including `CLS -> GPIO3` and GPIO32 avoidance.
- Signal hypotheses and confidence.
- Timing discoveries: SPS frame marker, LP/SPL line-related behavior, current visible/crop assumptions.
- Color packing history: red-only, red/green, RGB332, RGB565/RGB666 investigations.
- Capture presets used by the browser live view.
- Known hardware caveats: original LCD removed, floating/termination uncertainty, power-cycle behavior, backfeed discovery.
- Success criteria: stable boot logo capture, then game/cartridge content, then reusable real-time capture block.

The GBC module should not own generic concepts like USB transport, GPIO edge counting, VCD export, artifact manifests, browser layout, or LCD_CAM raw capture. Those belong to the platform.

### ESP32-P4 Pipeline Blocks

Reusable pipeline blocks should be promoted only after they prove useful across captures:

| Block | Current Evidence | Future Use |
|---|---|---|
| Safe GPIO isolation | `ELECTRICAL_ISOLATE`, GPIO32 lesson | All targets, power-cycle workflows |
| GPIO inspector | `READ_GPIO`, browser pin table/timeline | Pin discovery and wiring validation |
| Activity scanner | `COUNT_GPIO_EDGES` | Find candidate clocks/sync/control lines |
| Clock measurement | PCNT `MEASURE_DCLK` style path | Identify pixel clocks and line clocks |
| Timing-edge capture | `CAPTURE_TIMING_EDGES`, CSV/VCD export | Role classification and protocol discovery |
| Raw LCD_CAM capture | current GBC live capture path | Parallel digital picture bus ingestion |
| Hypothesis decoder | crop/stride/skew/bit/color operations | Offline reconstruction, scaler inputs |
| Artifact manifest | partially present through experiment folders | AI review packs and reproducibility |
| Browser workbench | current live viewer plus tabs | Human-in-the-loop investigation |

Product modules should consume these blocks instead of duplicating them. Examples:

- Retimer: capture clocked samples, normalize timing, emit a corrected digital bus.
- Screen mod: capture target bus, convert framebuffer, drive panel output.
- Scaler: capture framebuffer or stream, apply scaling/color pipeline, output display/video.
- Protocol analyzer: capture timing/data, produce reports, VCD, images, and confidence metrics.

### Sanitization Plan

Phase A: inventory only.

- List generated files and caches. Status: started.
- List host scripts by status: stable, compatibility, experiment, obsolete. Status: documented in `docs/host_script_inventory.md`.
- List docs that are generic vs GBC-specific. Status: documented in `docs/document_inventory.md`.
- Confirm `.gitignore` covers all generated outputs. Status: started.

Phase B: non-destructive organization.

- Add `host/experiments/README.md` and `host/tools/README.md`. Status: done.
- Add compatibility wrappers before moving scripts. Status: pending.
- Add artifact manifests to new captures without rewriting old folders. Status: manifest draft added; implementation pending.
- Add profile schema draft but keep `profiles/gbc_lcd.json` working. Status: done.

Phase C: controlled cleanup.

- Delete only generated caches/build outputs after a fresh build and flash path is verified.
- Move old experiment scripts to `host/experiments/` with a short index.
- Move GBC-specific docs into `docs/targets/gbc_lcd/` only after links are updated.

Phase D: reusable API extraction.

- Promote repeated serial/probe/artifact code into `host/lab/`.
- Split browser workbench UI assets from server logic.
- Introduce generic firmware command names while preserving old command aliases.

### Preservation Rules

- Keep working GBC live capture behavior intact during cleanup.
- Do not rename a command without keeping a compatibility alias.
- Do not delete capture artifacts that contributed to a discovery unless their summary and representative output are preserved.
- Record every hardware lesson in docs before changing wiring assumptions.
- Treat project history as evidence, not clutter, until it has been summarized.
