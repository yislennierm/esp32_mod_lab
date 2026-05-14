# Decisions

Purpose: record important project decisions and the reason behind them.

Status: canonical. Add entries when a decision changes architecture, wiring, safety, commands, or workflow.

Last updated: 2026-05-11.

## Decision Format

Each decision should include:

- date
- status
- decision
- reason
- consequences
- related docs or artifacts

## 2026-05-10: AI Context Is Canonical

Status: superseded on 2026-05-10.

Decision: `docs/AI_CONTEXT.md` is the first document an AI agent should read for current project truth.

Reason: the project has many Markdown files. AI and humans need one compact, current file that points to deeper docs.

Consequences:

- Keep `docs/AI_CONTEXT.md` short and current.
- Do not duplicate long history there.
- Update it when wiring, current capture preset, working commands, or priorities change.

Related:

- `docs/DOCS_INDEX.md`
- `docs/document_inventory.md`

## 2026-05-10: Use Ant Design For The Browser Workbench UI

Status: active.

Decision: future browser UI work should use Ant Design components.

Reason: the workbench is becoming a dense operational instrument with navigation, tables, forms, status states, artifacts, profiles, and live monitoring. Ant Design fits this better than custom embedded HTML/CSS.

Consequences:

- Keep current Python endpoints and live capture behavior stable.
- Introduce a React + TypeScript frontend rather than expanding the embedded HTML indefinitely.
- Use Ant Design for layout, navigation, forms, tables, status, drawers, and feedback.
- Keep the live frame canvas custom.
- Do not switch default UI until GBC live capture is verified through the new frontend.

Related:

- `docs/ant_design_ui_plan.md`
- `docs/investigation_workbench.md`
- `host/live_lcdcam_stream_viewer.py`

## 2026-05-10: Documentation Has Canonical, Supporting, And Historical Layers

Status: active.

Decision: docs are classified by role instead of treating every Markdown file as equally authoritative.

Reason: too many peer-level docs make AI retrieval and human navigation worse.

Consequences:

- Canonical docs define current truth and contracts.
- Supporting docs hold detailed explanations and evidence.
- Historical docs preserve the journey and failed attempts.
- Detailed evidence stays available but should not be required for every task.

Related:

- `docs/DOCS_INDEX.md`
- `docs/document_inventory.md`

## 2026-05-10: Use Source -> Processing -> Destination As The System Method

Status: active.

Decision: every target/product project should be modeled as:

```text
Source bus -> ESP32-P4 capture/processing -> Destination
```

with the computer/browser/AI workbench controlling and observing the ESP32-P4.

Reason: this maps both reverse engineering and future product implementation to the same device and software blocks.

Consequences:

- UI should be organized around Project, Source, Processing, Destination, Live, Artifacts, Profile, and Logs.
- Source profiles, destination profiles, and product profiles should be separate concepts.
- ESP32-P4 processing blocks should serve both lab mode and product mode when possible.

Related:

- `docs/system_method.md`
- `docs/system_gap_assessment.md`
- `docs/investigation_workbench.md`

## 2026-05-09: GBC Is Target Module 001, Not The Whole Project

Status: active.

Decision: the Game Boy Color LCD bus is the first source target/profile and reference case, not the entire architecture.

Reason: the same lab should later investigate other console picture/display buses.

Consequences:

- GBC-specific facts belong in `profiles/gbc_lcd.json` and target docs.
- Generic capture, timing, transport, artifacts, and browser machinery should remain target-agnostic.
- Working GBC behavior must be preserved while generic APIs are extracted.

Related:

- `profiles/gbc_lcd.json`
- `docs/universal_signal_lab.md`
- `docs/project_maintenance.md`

## 2026-05-09: Move CLS From GPIO32 To GPIO3

Status: active.

Decision: current GBC `CLS` wiring is GPIO3. GPIO32 is historical and should not be used for `CLS` in the working baseline.

Reason: while the GBC was unpowered and the ESP32-P4 was powered, bus lines showed intermediate voltage. The user also observed power-cycle failures. Moving `CLS` from GPIO32 to GPIO3 made off/on switching stable while capture was running.

Consequences:

- Treat old GPIO32 `CLS` timing measurements as historical only.
- Keep GPIO32 out of current GBC allowlists unless a new hardware reason is documented.
- Repeat any important `CLS` timing measurements on GPIO3 before using them as current evidence.

Related:

- `docs/gbc_lcd_journey.html`
- `docs/timing_notes.md`
- `docs/hardware_notes.md`
- `profiles/gbc_lcd.json`

## 2026-05-09: RGB332 Is The Stable Live Baseline

Status: active.

Decision: use LCD_CAM `RGB332` as the stable diagnostic live-view baseline at that stage of the investigation.

Reason: it provides coherent live images and acceptable throughput while preserving the working geometry. Higher-depth modes are still being evaluated.

Consequences:

- Superseded: the active GBC source path is now RGB565 through `GBC_SOURCE_FRAME_BIN` / `GBC_SOURCE_STREAM_BIN`.
- Keep generic LCD_CAM RGB332 support for old artifacts and non-GBC diagnostics, but do not present RGB332 as the GBC baseline.
- Keep RGB666 as source-preservation research/offline validation, not current live baseline.

Related:

- `profiles/gbc_lcd.json`
- `docs/framebuffer_format.md`
- `docs/capture_pipeline.md`

## 2026-05-09: Preserve Broken Images And Failed Captures As Evidence

Status: active.

Decision: do not delete or hide failed/tiled/skewed/noisy captures casually.

Reason: repeated mini-frames, skew, color sparkle, and black captures directly informed sampling, stride, edge, and peripheral decisions.

Consequences:

- Cleanup should classify and archive evidence before deleting.
- Journey/report docs should link representative artifacts.
- Artifact manifests should record hypotheses and anomalies explicitly.

Related:

- `docs/gbc_lcd_journey.html`
- `docs/project_maintenance.md`
- `docs/artifacts/manifest_format.md`

## 2026-05-09: Firmware Commands Need Compatibility Aliases

Status: active.

Decision: command names should not be silently removed or renamed.

Reason: host scripts, browser UI, docs, and AI workflows depend on stable command names.

Consequences:

- Introduce generic names gradually.
- Keep old names as aliases until all callers are migrated and verified.
- Document command changes in `docs/DECISIONS.md` or a command reference.

Related:

- `docs/system_gap_assessment.md`
- `host/README.md`

## 2026-05-09: Hardware Safety Is Part Of The Architecture

Status: active.

Decision: electrical behavior, power sequencing, missing loads, straps, and backfeed risks are architecture concerns, not incidental debug notes.

Reason: the GPIO32/power-cycle issue changed the working system. The final lab and product designs must expose and control these risks.

Consequences:

- Target profiles must include dangerous rails and known concerns.
- UI must show safety and power/recovery controls.
- Future hardware should consider physical isolation or buffering.

Related:

- `docs/hardware_notes.md`
- `docs/risks_and_unknowns.md`
- `profiles/gbc_lcd.json`

## 2026-05-10: Recovery Flashing Uses Native USB At Low Baud First

Status: active.

Decision: application control and the browser backend use the native ESP32-P4 USB/JTAG/serial port. Recovery testing may use the WCH UART bridge because it can reach the ESP32-P4 ROM bootloader, but normal lab operation remains on native USB.

Reason: during FPS experiments, the board became difficult to reach until the user restored it with another project. Native USB later recovered normal firmware reliably at low baud. On 2026-05-10, `/dev/cu.wchusbserial5A470211841` successfully answered an ESP32-P4 ROM `chip_id` query and ran the esptool stub, but it did not answer the probe JSON `PING` command.

Consequences:

- Stop `host/live_lcdcam_stream_viewer.py`, `gbc_probe.py`, `idf.py`, and `esptool.py` before flashing.
- Use `scripts/build_probe_firmware.sh` and `scripts/flash_probe_firmware.sh` for normal firmware.
- Use `scripts/build_safe_recovery.sh` and `scripts/flash_safe_recovery.sh` before aggressive capture or transport experiments.
- Keep safe-recovery firmware able to answer basic commands while blocking capture commands.
- Do not move the browser/backend protocol to WCH unless firmware explicitly exposes it there.
- Do not treat WCH flashing as proven until a low-baud flash and post-flash smoke test succeed.

Related:

- `docs/firmware_recovery_workflow.md`
- `docs/dual_transport_strategy.md`
- `scripts/stop_lab_processes.sh`
- `scripts/build_safe_recovery.sh`
- `scripts/flash_safe_recovery.sh`

## 2026-05-10: Native USB Is Data Plane, WCH UART Is Recovery/Debug Plane

Status: active.

Decision: maximum-FPS work should keep native USB as the high-rate data plane and use WCH UART only for ROM recovery and, later, explicit low-rate debug/control support.

Reason: native USB currently carries the working app protocol and browser frame stream. WCH UART can reach the ROM bootloader and can be useful for recovery flashing, but UART bandwidth is not appropriate for full-frame video streaming. A temporary UART live-view test dropped RGB565 preview from the several-FPS native-USB range to about `0.24 fps`, so UART must not be treated as a performance path.

Consequences:

- FPS work must optimize LCD_CAM/GDMA lifetime, binary framing, USB write size, and host read/render paths.
- UART should not be used for frame payloads.
- Logs must not be interleaved with native USB binary frames.
- Any UART app/control support must be implemented deliberately as a separate firmware task or log sink.
- Firmware should expose `TRANSPORT_STATUS` so host tools can verify that the lab data plane is native USB before streaming.

Related:

- `docs/dual_transport_strategy.md`
- `docs/capture_pipeline.md`
- `docs/firmware_recovery_workflow.md`

## 2026-05-10: Lab Firmware And Production Firmware Are Separate Modes

Status: active.

Decision: the browser-controlled lab instrument and the direct source-to-destination production path are separate compile-time firmware modes for now.

Reason: the lab image needs command handling, browser integration, pin manipulation, and verbose instrumentation. The production image needs a minimal hot path to measure what the ESP32-P4 can do as an embedded bridge. Mixing both into one runtime mode would make performance interpretation and safety behavior less clear.

Consequences:

- Normal lab firmware is built with `scripts/build_probe_firmware.sh`.
- Early production GBC-to-SPI mirror firmware is built with `scripts/build_production_mirror.sh`.
- Production mirror does not start the browser workbench protocol.
- Production mirror currently preserves source geometry and performs only RGB565-to-RGB666 conversion.
- Future production modes should be added through an explicit profile or registry rather than hidden command flags.

Related:

- `docs/production_modes.md`
- `firmware/main/production_mirror.c`
- `scripts/build_production_mirror.sh`
- `scripts/flash_production_mirror.sh`

## 2026-05-11: Hardware-Block Research Starts In Isolated Experiment Firmware

Status: active.

Decision: risky or performance-critical ESP32-P4 hardware-block research starts as a separate ESP-IDF project under `experiments/`, not inside the full browser-controlled lab firmware.

Reason: the lab image contains command parsing, browser streaming, pin tooling, destination code, historical compatibility paths, and debugging behavior. Those are useful for investigation, but they can contaminate performance results and can make hardware bring-up harder to debug. The TinyUSB/high-speed USB work showed that testing a new transport inside the full lab context makes it too easy to confuse board routing, firmware mode, stdio, USB Serial/JTAG, TinyUSB, and host tooling.

Consequences:

- New hardware-block claims must be proven in this order: isolated experiment firmware, measured JSON evidence, lab/UI integration, then production profile.
- The lab firmware may contain compatibility benchmark commands, but they are not the highest-confidence proof for a hardware block.
- Isolated experiment apps should exclude unrelated modules explicitly in their README and JSON output.
- A successful experiment can graduate into the lab workbench only after its hot path and counters are understood.
- A successful experiment can graduate into production only after it is combined with other already-proven blocks.

Related:

- `experiments/source_ring_bench/`
- `experiments/tinyusb_bench/`
- `docs/platform/esp32p4_internal_dataflow_plan.md`
- `docs/esp32p4_modder_research_plan.md`

## 2026-05-11: Standalone Experiment Firmware Is Not The Default Flash Path

Status: active.

Decision: standalone experiment firmware may be built for compile evidence, but it must not be flashed by default on the active development board unless the experiment has a recovery-safe plan and an explicit override.

Reason: the standalone PPA SRM benchmark used its own ESP-IDF project and sdkconfig. Repeated attempts around that path forced manual recovery on the current ESP32-P4 board. That is not acceptable for the normal research loop, especially when the normal lab firmware already contains a proven command/response path and a proven source-ring benchmark command.

Consequences:

- `scripts/flash_ppa_srm_bench.sh` now refuses to flash unless `ALLOW_EXPERIMENTAL_PPA_FLASH=1` is set.
- Hardware-block benchmarks that do not need special boot/USB behavior should first be added as no-I/O commands inside the known-good lab firmware.
- Separate full-device experiment apps remain useful only when the feature being tested cannot be isolated inside the lab firmware.
- Before flashing a standalone app, record the recovery path, expected serial port, console behavior, and post-flash smoke test.

Related:

- `experiments/ppa_srm_bench/`
- `scripts/flash_ppa_srm_bench.sh`
- `docs/firmware_recovery_workflow.md`
