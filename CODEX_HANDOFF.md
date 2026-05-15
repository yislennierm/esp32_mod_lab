# Codex Handoff

Last updated: 2026-05-14

## Project

This repository is evolving into an ESP32-P4 based signal lab for reverse engineering and reusing console display buses. The current concrete source module is the Game Boy Color LCD bus. The current destination module under test is a SPI LCD panel, likely ST7796S/ILI9486 class, connected as a lab output.

The project goal is not only a GBC screen mod. The larger architecture is:

Source display bus -> ESP32-P4 capture/processing/instrumentation -> Destination display/protocol

The same ESP32-P4 should work as both an investigation instrument and, later, a production firmware target.

## Lab / Project Split

The browser workbench now exposes the first version of the lab-to-project boundary:

- Blocks are reusable units such as `gbc_lcd_source`, `spi_lcd_destination`, and `production_mirror`.
- Projects compose blocks into deployable firmware.
- The first project profile is `projects/gbc_spi_lcd_mirror.json`.
- The Project tab can list blocks/projects, validate source/destination GPIO conflicts, build the production project, and flash it through the existing scripts.
- A first read-only Graph tab now renders the current project as a block flowgraph and includes an ESP32-P4 dashboard inspired by the official functional block diagram.

Important behavior: flashing from the Project tab releases the workbench serial session first. After production firmware is flashed, the interactive lab backend may no longer be able to talk to the board until lab firmware is flashed again.

## Current Hardware Mapping

### GBC LCD Source

| GBC signal | ESP32-P4 GPIO |
|---|---:|
| DCLK | 22 |
| LP | 21 |
| PS | 20 |
| SPL | 19 |
| CLS | 3 |
| SPS | 33 |
| R5 | 13 |
| R4 | 14 |
| R3 | 15 |
| R2 | 16 |
| R1 | 17 |
| R0 | 18 |
| G5 | 6 |
| G4 | 5 |
| G3 | 4 |
| G2 | 10 |
| G1 | 11 |
| G0 | 12 |
| B5 | 50 |
| B4 | 48 |
| B3 | 47 |
| B2 | 46 |
| B1 | 45 |
| B0 | 36 |

### SPI LCD Destination

| LCD signal | ESP32-P4 GPIO |
|---|---:|
| CS | 52 |
| MOSI / SDI | 31 |
| SCK | 28 |
| RESET | 29 |
| D/C | 53 |

## Known Good Production Mirror State

Current best functional production mirror:

```sh
DEST_SPI_LCD_RAW_SPI=1 DEST_SPI_LCD_PCLK_HZ=70000000 PRODUCTION_MIRROR_MODE=2 ./scripts/flash_production_mirror.sh /dev/cu.usbmodem14401
```

Observed behavior:

- Clean image on SPI LCD, no black horizontal lines.
- Around 29.9 FPS reported.
- Average capture time around 28.8 ms.
- Average draw time around 14.45 ms.
- No dropped frames or draw failures in recent test.

Important: the board was left running the good flashed image before this handoff. After the last source cleanup, a build was verified, but that cleanup was not reflashed because the running image was already good.

## Key Debugging Lessons

### Black Horizontal Lines Root Cause

The black horizontal lines were not SPI signal integrity and not the small-window LCD draw path.

Evidence:

- Full-panel generated pattern was clean.
- Generated GBC-sized `161x145` window pattern was clean.
- Freeze-frame mode captured one GBC frame and redrew it repeatedly with no black lines.
- Synchronous capture/draw mode was clean.
- Overlapped live capture/draw with PSRAM frame slots produced black lines.
- Moving overlap frame slots to internal DMA-capable RAM fixed the lines.

Conclusion:

The lines were caused by PSRAM contention/cache/DMA interaction when live capture and LCD draw overlapped while both depended on PSRAM-backed frame data.

### SPI LCD Throughput

Measured full-panel RGB666 destination-only benchmarks:

- 40 MHz: about 8.33 FPS for full `320x480`.
- 60 MHz: about 11.1 FPS.
- 80 MHz: about 14.3 FPS but user judged 80 MHz too aggressive.
- 70 MHz is the preferred current SPI clock.

Full-screen `320x480` over 4-wire SPI is not a good 60 FPS destination. For 1x GBC window output, SPI bandwidth is plausible; for full-screen scaled video, use RGB/i80/parallel/MIPI-style destination instead.

### Optimization Attempts

Tried visible-only internal handoff:

- Copied only the visible `160x144` window into internal RAM.
- Stayed clean but was slower due per-pixel extraction work.
- Reverted to full-buffer internal copy as the best clean baseline.

Tried disabling capture summary scans:

- Did not improve measured capture time because capture timing is measured before that summary work.
- Not the current bottleneck.

## Important Firmware Files

- `firmware/main/production_mirror.c`: production mirror modes and GBC -> SPI LCD pipeline.
- `firmware/main/destination_spi_lcd.c`: SPI LCD init, MADCTL, raw SPI drawing, GBC draw helpers.
- `firmware/main/gbc_lcd_source.c`: GBC source geometry and visible-frame helpers.
- `firmware/main/lcdcam_raw.c`: LCD_CAM/GDMA raw capture paths.
- `firmware/main/pinmap_current.h`: current GPIO mapping.

## Important Modes

Production mirror modes are in `firmware/main/production_mirror.c`.

- Mode 1: synchronous capture then draw. Clean but slower.
- Mode 2: overlap capture/draw. Current best with internal DMA frame slots.
- Mode 8: freeze-frame diagnostic. Captures once, redraws same frame.
- Mode 10: destination full-panel generated pattern benchmark.
- Mode 11: GBC-sized generated window diagnostic.

## Current Bottleneck

The current clean system is limited by the capture side, not SPI draw:

- Capture: about 28.8 ms.
- Draw: about 14.45 ms.

The next real performance work should not randomly tweak SPI. It should build a frame-boundary-safe faster capture path that preserves the clean memory ownership model.

Likely next directions:

- Frame-boundary-safe ring capture, avoiding the drift seen in earlier ring-direct mode.
- Capture buffer ownership handoff without unsafe concurrent PSRAM access.
- Internal line/window staging that avoids PSRAM contention.
- Revisit LCD_CAM/GDMA descriptor strategy and start/EOF semantics.

## Recovery Notes

If flashing or serial access acts stuck:

- Stop host viewers/monitors first.
- Use the USB-Serial/JTAG port that has worked as `/dev/cu.usbmodem14401`.
- User has also recovered the ESP32-P4 manually by flashing another known-good example from VS Code.

## Documentation Map

Start here for future sessions:

- `docs/AI_CONTEXT.md`
- `docs/DOCS_INDEX.md`
- `docs/system_method.md`
- `docs/project_block_model.md`
- `docs/flowgraph_lab_plan.md`
- `docs/architecture.md`
- `docs/capture_pipeline.md`
- `docs/destination_spi_lcd_lab.md`
- `docs/production_modes.md`
- `docs/platform/esp32p4_internal_dataflow_plan.md`
- `docs/experiment_log.md`

## Current Lab UI Direction

The Graph page is moving toward a GNU Radio/WYSIWYG workspace.

Recent state:

- ESP-IDF examples import as read-only lab projects.
- The graph shows functional blocks, MCU resources, and external electronics, not source files as nodes.
- Nodes can be dragged and layout can be saved.
- The first typed block editor is implemented for RTOS Task nodes.
- RTOS Task edits are stored as `params.overlay` metadata in project JSON and do not modify imported ESP-IDF source.
- Editable RTOS fields: enabled, task name, priority, stack size bytes, core affinity, and notes.
- The graph inspector is organized around block authoring:
  - Block: typed editor, connections, metadata, debug JSON.
  - Context: project-level source/destination/status/device state.
  - SDK: read-only imported ESP-IDF references.
  - Library: available reusable lab blocks.
  - Model: editing rules.

Relevant docs:

- `docs/graph_workspace_tools_plan.md`
- `docs/graph_block_editors.md`

## Repository Split / Portability Plan

Known GitHub repositories as of 2026-05-15:

- Lab/workbench repo: `https://github.com/yislennierm/esp32_mod_lab`
- First concrete project repo: `https://github.com/yislennierm/esp32p4_gbc_screen_mod`

The split is not fully enforced in code yet, but future work must preserve these roles.

`esp32_mod_lab` owns:

- Lab platform: browser workbench, ESP32-P4 block inventory, SDK importer, generic graph editor, setup scripts.
- Generic firmware instrumentation patterns and reusable host tools.
- SDK/reference inventories under `sdk_inventory/` and `inventories/`.
- Read-only imported ESP-IDF example models and generic `hello_led` style projects.

`esp32p4_gbc_screen_mod` should own, or eventually own:

- GBC-specific source profile, wiring profile, and evidence-derived assumptions.
- GBC-to-display production firmware variants.
- Project-specific PCB/pinout choices.
- Project-specific captures, screenshots, and display tuning notes that are not generic lab evidence.

Do not treat GBC as the lab itself. GBC is one project/profile pair and the first proof target. `hello_led`, imported ESP-IDF examples, and future projects must open without showing GBC as the active project.

Current local state on this machine:

- `main` tracks `origin/main` for `esp32_mod_lab`.
- Remote branch `origin/codex/three-build-project-split` exists and contains additional workbench/flowgraph changes. Do not merge it blindly; inspect it against the two-repo boundary first.

Machine-local items:

- ESP-IDF path is not portable. Use `.env` locally and do not commit it.
- macOS current IDF path was `/Users/nene/esp/v5.5/esp-idf`.
- Linux expected IDF path should usually be `$HOME/esp/v5.5/esp-idf`, unless `.env` overrides it.
- Serial ports are machine-specific. On Linux prefer `/dev/serial/by-id/...` once identified.
- The board may appear as native USB Serial/JTAG and WCH UART; keep both roles separate.

First steps on Linux:

1. Clone the repo.
2. Install or verify ESP-IDF `v5.5` for ESP32-P4.
3. Create `.env` from `.env.example` and set `IDF_PATH`, `PORT`, and optional `RECOVERY_PORT`.
4. Create Python virtualenv and install `requirements.txt`.
5. Run `npm install` and `npm run build` in `host/workbench/frontend`.
6. Run `python host/gbc_probe.py ports` to identify device names.
7. Start with lab firmware and UI, not production firmware.

Important restructuring still pending:

- Extract machine setup into one explicit lab setup script.
- Separate lab platform code from target/project modules more clearly.
- Add project/profile selection to backend commands so hardcoded GBC paths become compatibility aliases.
- Keep imported ESP-IDF examples read-only and project-local overlays separate from SDK source.
- Keep local projects and generated SDK inventories organized so they can later move to separate repos or ignored local workspaces if needed.
- Decide the handoff/export format between `esp32_mod_lab` and `esp32p4_gbc_screen_mod`: project JSON, profiles, firmware target descriptors, and generated code should move intentionally, not by copying random folders.
