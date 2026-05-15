# AI Context

Purpose: compact current truth for Codex or another AI agent. Read this first, then follow links only when deeper evidence is needed.

Status: canonical and current. Keep this file short.

Last updated: 2026-05-15.

## Project

This is the ESP32-P4 Console Signal Lab.

Goal: use the ESP32-P4 both as:

- an investigation instrument for unknown console/display picture buses
- a future implementation platform for mods such as screen bridges, scalers, retimers, recorders, and analyzers

Current source target: Game Boy Color LCD bus, profile `profiles/gbc_lcd.json`.

Current system method: source bus -> ESP32-P4 capture/processing -> destination, controlled and observed from host/browser/AI tools.

## Repository Roles

Current GitHub repos:

- `https://github.com/yislennierm/esp32_mod_lab`: generic lab/workbench, SDK inventory, graph UI, instrumentation, reusable host/firmware patterns.
- `https://github.com/yislennierm/esp32p4_gbc_screen_mod`: first concrete project repo for the GBC screen-mod work.

The split is not fully implemented yet. Until it is, keep GBC-specific assumptions from leaking into the generic lab UI and backend. Treat GBC as a project/profile, not as the lab default.

Remote lab branch `origin/codex/three-build-project-split` exists with additional workbench/flowgraph changes. Inspect before merging because it may need cleanup against the lab/project boundary.

## Canonical Docs

- Mission and hard rules: `PROJECT_CHARTER.md`
- Current AI context: `docs/AI_CONTEXT.md`
- Decisions: `docs/DECISIONS.md`
- Documentation map: `docs/DOCS_INDEX.md`
- System method: `docs/system_method.md`
- Gap assessment: `docs/system_gap_assessment.md`
- UI direction: `docs/ant_design_ui_plan.md`
- Firmware recovery: `docs/firmware_recovery_workflow.md`
- Dual transport strategy: `docs/dual_transport_strategy.md`
- Production firmware modes: `docs/production_modes.md`
- Active source profile: `profiles/gbc_lcd.json`
- GBC visual journey: `docs/gbc_lcd_journey.html`

## Current GBC Wiring

Timing/control:

| Signal | ESP32-P4 GPIO | Current status |
|---|---:|---|
| DCLK | 22 | sample/pixel clock candidate |
| LP | 21 | line-marker candidate |
| PS | 20 | power/blanking/control candidate |
| SPL | 19 | line/start/data-enable candidate |
| CLS | 3 | current stable wiring; moved from GPIO32 |
| SPS | 33 | frame-marker candidate |

Pixel bus:

| Channel | Mapping |
|---|---|
| Red | R5 GPIO13, R4 GPIO14, R3 GPIO15, R2 GPIO16, R1 GPIO17, R0 GPIO18 |
| Green | G5 GPIO6, G4 GPIO5, G3 GPIO4, G2 GPIO10, G1 GPIO11, G0 GPIO12 |
| Blue | B5 GPIO50, B4 GPIO48, B3 GPIO47, B2 GPIO46, B1 GPIO45, B0 GPIO36 |

Do not reconnect `CLS` to GPIO32. GPIO32 is historical and was associated with power-cycle/backfeed trouble.

## Dangerous Lines

Do not connect these GBC lines to ESP32-P4 GPIO:

`V0-V9`, `VCOM`, `VEE`, `VSHA`, `VSHD`, `VCC`, `VDD`, `VSS`, `DGND`.

ESP32-P4 GPIO is not assumed 5V tolerant.

## Working Capture Baseline

- peripheral: ESP32-P4 LCD_CAM
- data mode: `RGB565`
- programmed capture size: `192x145`
- decoded stream model: row stride `161` samples, rows per period `145`
- visible crop: `160x144` after linear stream shift `-4` pixels
- PCLK invert: `false`
- DE level: `HIGH`
- start marker: `SPS`
- stream command: `GBC_SOURCE_FRAME_BIN`
- stream batch size: `1`

Current recovered browser/live mode from 2026-05-10:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python -B host/live_lcdcam_stream_viewer.py --port /dev/cu.usbmodem14301 --listen-port 8791 --no-pclk-invert --interval-ms 33 --capture-timeout-ms 300 --width 192 --height 145 --data-mode RGB565 --firmware-binary --gbc-source-driver --no-source-binary --continuous-capture --stream-batch-size 1 --host-crop --crop-offset 0 --crop-width 161 --crop-height 145 --profile profiles/gbc_lcd.json
```

Port may change. Discover current device before assuming `/dev/cu.usbmodem14301`. RGB565 is now the GBC source baseline; older RGB332 artifacts are historical evidence only.

## Working Commands And Tools

Build and flash scripts:

- `scripts/stop_lab_processes.sh`
- `scripts/build_probe_firmware.sh`
- `scripts/flash_probe_firmware.sh <serial-port>`
- `scripts/build_source_ring_bench.sh`
- `scripts/flash_source_ring_bench.sh <serial-port>`
- `scripts/build_production_mirror.sh`
- `scripts/flash_production_mirror.sh <serial-port>`
- `scripts/build_safe_recovery.sh`
- `scripts/flash_safe_recovery.sh <serial-port>`

Current normal lab firmware uses UART console/control at `115200` on the WCH bridge, currently `/dev/cu.wchusbserial5A470211841`. Native USB Serial/JTAG still enumerates on this board, but it did not answer the probe JSON protocol during the 2026-05-11 source-ring integration test, so do not assume it is the active app-control port.

Hardware-block research rule:

- Start risky or performance-critical ESP32-P4 block work in isolated firmware under `experiments/`.
- Prove it with JSON counters/evidence before integrating into the lab UI or production firmware.
- First implemented isolated app: `experiments/source_ring_bench/`.
- `source_ring_bench` excludes lab protocol, browser stream, SPI LCD destination, TinyUSB, PNG rendering, and frame payload streaming.
- Standalone experiment apps are not the default flash path on the active board after the PPA standalone app forced repeated manual recovery. Prefer adding no-I/O hardware-block benchmarks to the known-good lab firmware command path when that is sufficient.
- PPA SRM lab command added in firmware source: `PPA_SRM_BENCH [frame_count]`. It uses synthetic `160x144 RGB565 -> 320x288 RGB565` buffers, compares PPA SRM against CPU 2x scaling, prints JSON, and does not touch GBC GPIO, SPI LCD, browser streaming, or frame payload transport. 2026-05-11 result: PPA `149.715 fps` / `6679.3 us`, CPU `80.510 fps` / `12420.8 us`, speedup `1.86x`, artifact `captures/benchmarks/ppa_srm/20260511T201252Z-ppa-srm-bench/`.
- Host collector added: `python host/collect_ppa_srm_bench.py --port /dev/cu.wchusbserial5A470211841 --frames 120 --timeout-s 30 --echo`. It writes artifacts under `captures/benchmarks/ppa_srm/`.

Early production mirror mode:

- Build with `./scripts/build_production_mirror.sh`.
- Flash with `./scripts/flash_production_mirror.sh /dev/cu.wchusbserial5A470211841`.
- It does not run the browser/lab command server.
- It boots directly into GBC source capture -> SPI LCD draw.
- It applies no rotation, mirror, scaling, or geometry fix. Only RGB565 to RGB666 color conversion is performed.
- Metrics appear on USB Serial/JTAG, currently `/dev/cu.usbmodem14401` in the latest run.
- Initial known result from 2026-05-10: about `14.9 fps`, `avg_capture_us ~= 41953`, `avg_draw_us ~= 24986`, zero capture/draw failures in the sampled window.
- Current best production result from 2026-05-10 after overlapping LCD_CAM capture with SPI drawing: about `29.86 fps`, `avg_capture_us ~= 23333`, `avg_draw_us ~= 25076`, zero dropped/capture/draw failures in the sampled window.
- Experimental raw write-only SPI with `SPI_DEVICE_NO_DUMMY` was tested on the older `SCK=28`, `MOSI=31` wiring and disabled by default because it measured about `14.9 fps` with `avg_draw_us ~= 32511`.
- Current known-good SPI LCD destination wiring is `CS=52`, `SDI/MOSI=31`, `SCK=28`, `RESET=29`, `D/C=53`.
- This known-good SPI LCD wiring uses ESP-IDF GPIO-matrix routing. Do not set `SPICOMMON_BUSFLAG_IOMUX_PINS` with these pins.
- 2026-05-11 PPA-backed production mode `PRODUCTION_MIRROR_MODE=3` was built and flashed through `/dev/cu.usbmodem14401`. It captures GBC RGB565, PPA-scales visible `160x144` to `320x288`, then draws through the known-good RGB666 SPI LCD path. Metrics with GBC powered on: about `5.97 fps`, `avg_capture_us ~= 41789`, `avg_crop_us ~= 1564`, `avg_ppa_us ~= 8222`, `avg_draw_us ~= 115619`, zero capture/PPA/draw failures. Interpretation: PPA integration works, but this synchronous 2x SPI LCD mode is draw-bandwidth limited and still uses the older synchronous source capture path.
- 2026-05-11 PPA-backed production mode `PRODUCTION_MIRROR_MODE=4` measures 1x PPA pass-through. Valid build used `BUILD_DIR=build_production_mirror_m4`. Metrics: about `14.93 fps`, `avg_capture_us ~= 34768`, `avg_crop_us ~= 1453`, `avg_ppa_us ~= 5248`, `avg_draw_us ~= 25395`, zero failures. User visual feedback: glitchy pixels. Interpretation: PPA 1x costs about `5.25 ms` but is timing evidence only until image artifacts are isolated; skip it for pure unmodified 1x output. Production build/flash scripts now force `idf.py reconfigure` to avoid stale `PRODUCTION_MIRROR_MODE` cache.
- 2026-05-11 production mode `PRODUCTION_MIRROR_MODE=5` is the correct source-ring + PPA + LCD test. It uses `lcdcam_raw_ring_capture_loop` at native visible `160x144 RGB565`, CPU copy-only frame handoff, PPA SRM 2x to `320x288`, then SPI LCD RGB666 draw. Metrics: `avg_capture_us ~= 16520`, `avg_copy_us ~= 3000`, `avg_ppa_us ~= 8650`, `avg_draw_us ~= 116166`, displayed about `8 fps`, captured about `60 fps`, many dropped frames because SPI draw is slower than source. JSON reports `"cpu_scaling":false` and `"source_path":"lcdcam_raw_ring_capture_loop"`. There is no CPU geometric scaling; destination still does RGB565-to-RGB666 color expansion for the current SPI panel.
- 2026-05-11 production mode `PRODUCTION_MIRROR_MODE=6` is the 1x source-ring + PPA + LCD variant. It reports `production_mirror_ring_ppa_1x`, `"cpu_scaling":false`, and `"source_path":"lcdcam_raw_ring_capture_loop"`. Metrics: displayed about `32 fps`, captured about `60 fps`, `avg_capture_us ~= 16537`, `avg_copy_us ~= 3376`, `avg_ppa_us ~= 5716`, `avg_draw_us ~= 25392`, zero capture/PPA/draw failures. User should report whether the slow scrolling persists; if yes, debug ring frame/line phase next.
- 2026-05-11 production mode `PRODUCTION_MIRROR_MODE=7` captures the solved stream geometry `161x145 RGB565`, copies the visible `160x144` region, runs PPA at `1.0x`, and draws through the known-good SPI LCD RGB666 path. It reports `production_mirror_ring_stream_ppa_1x`, `"cpu_scaling":false`, `"capture_width":161`, and `"visible_width":160`. Corrected metrics after padding DMA/cache-sync length: displayed about `32 fps`, captured about `60 fps`, `avg_capture_us ~= 16742`, `avg_copy_us ~= 3390`, `avg_ppa_us ~= 5544`, `avg_draw_us ~= 25370`, zero capture/PPA/draw failures. First mode-7 flash exposed `esp_cache_msync` alignment errors because `161x145x2` is not 64-byte aligned; `lcdcam_raw_ring_capture_loop` now pads the backing allocation/cache-sync length while keeping the true hardware byte count. If visual scrolling persists, debug continuous-ring start-of-frame/line phase next.
- The SPI LCD destination has two useful MADCTL policies:
  - `0x08`: source-straight diagnostic mode, BGR only, no orientation correction.
  - `0xE8`: panel-corrected mode that made the GBC image visually upright on the SPI LCD. This costs no ESP32 per-pixel processing because the LCD controller performs the address scan transform.
- Production defaults to panel-corrected `0xE8`; lab/source analysis should still treat the captured source buffer as factual and apply viewer transforms outside the source module.
- ESP32-P4 normal SPI2 IO_MUX pins are `CLK=9`, `MOSI=8`, and `CS=7`. A 2026-05-11 experiment proved these pins can output clean 100 kHz SPI, but the LCD stayed white after the rewire. The old GPIO-matrix wiring was restored and color output returned only after removing the forced IO_MUX flag.
- Full-rate GBC display on this RGB666 SPI LCD is still not expected at `20 MHz` because `160x144x3x59.7` requires about `33.0 Mbit/s` before overhead; any future destination bandwidth work must preserve the known-good old-pin baseline first.

Current transport model:

- Native USB: app protocol, browser backend, binary frame data.
- WCH UART: ROM recovery candidate; future logs/control only after explicit firmware support.
- Do not put logs on the native USB binary frame stream during FPS work.

Firmware/host commands known to matter:

- `PING`
- `GET_VERSION`
- `GET_PINMAP`
- `EXPORT_STATS`
- `READ_GPIO <gpio>`
- `COUNT_GPIO_EDGES <gpio> <duration_ms>`
- `MEASURE_DCLK <gpio> <duration_ms>`
- `CAPTURE_TIMING_EDGES <duration_ms>`
- `CAPTURE_LINE_CLOCKS <marker> <edge> <line_count> <timeout_ms>`
- `GBC_SOURCE_STATUS`
- `GBC_SOURCE_FRAME_BIN [timeout_ms] [RGB565] [emit_len] [pclk_invert]`
- `GBC_SOURCE_STREAM_BIN <frame_count> [timeout_ms] [RGB565] [emit_len] [pclk_invert]`
- `LCDCAM_RAW_CAPTURE_BIN ...`
- `LCDCAM_RAW_STREAM_BIN <frame_count> ...`

Current GBC source-driver check:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/gbc_probe.py --port /dev/cu.usbmodem14301 --timeout 10 binary 'GBC_SOURCE_FRAME_BIN 300 RGB565 0 0' -o /tmp/gbc_source_frame.bin
```

Expected current result: `binary_len=46690`, `payload_len=46690`, `data_mode=RGB565`, `width=192`, `height=145`, and a capture time around tens of milliseconds. The raw payload is the `161x145` source stream. Visible-frame consumers apply `src_pixel = y * 161 + x - 4` to produce `160x144`. If this command fails short by a few bytes, check the firmware binary write path and host stale-input recovery before changing capture timing.

Current GBC batch source-driver benchmark:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/gbc_probe.py --port /dev/cu.usbmodem14301 --timeout 30 binary-sequence 'GBC_SOURCE_STREAM_BIN 32 300 RGB565 0 0' --count 32
```

Known result from 2026-05-10: `32/32` frames, `46690` bytes each, `4.83 s`, `6.626 fps`. Browser with `--stream-batch-size 8` reports about `7.1 fps`.

Current synthetic USB transport benchmark:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/gbc_probe.py --port /dev/cu.usbmodem14301 --timeout 30 binary-sequence 'USB_BENCH_STREAM_BIN 64 46690' --count 64
```

Known result from 2026-05-10: `64/64` payloads, `2988160` bytes total, `7.086 s`, `9.032 fps`, about `0.42 MB/s`.

Half-size payload check:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/gbc_probe.py --port /dev/cu.usbmodem14301 --timeout 30 binary-sequence 'USB_BENCH_STREAM_BIN 128 23345' --count 128
```

Known result from 2026-05-10: `128/128` payloads, same `2988160` total bytes, `7.111 s`, `18.0 fps`, about `0.42 MB/s`.

Performance interpretation: the current native USB Serial/JTAG command stream is a confirmed FPS limiter for RGB565. `46690` bytes at 60 fps is about `2.8 MB/s` before overhead, while the measured transport-only path is about `0.42 MB/s`. Do not assume persistent LCD_CAM alone will reach source frame rate. The next safe transport step is an isolated TinyUSB benchmark with synthetic payloads before moving GBC frames onto it.

TinyUSB experiment status:

- `experiments/tinyusb_bench/` exists as a separate ESP-IDF project.
- `scripts/build_tinyusb_bench.sh` builds it.
- `scripts/flash_tinyusb_bench.sh <port>` flashes it.
- The firmware implements `PING`, `GET_VERSION`, and `USB_BENCH_STREAM_BIN`.
- It does not touch GPIO, LCD_CAM, GDMA, or the GBC bus.
- Build and WCH flash succeeded on 2026-05-10.
- With the current two-cable setup, neither `/dev/cu.usbmodem14301` nor `/dev/cu.usbmodem5A470211841` answered `PING`.
- Normal probe firmware was restored and `/dev/cu.usbmodem14301` answered `PING`.

Current TinyUSB interpretation: code/build is not the blocker. The board's accessible USB-OTG path is not confirmed. Do not spend more time on TinyUSB CDC/vendor code until the exact board USB-OTG D+/D- routing is identified.

Current isolated source-ring benchmark:

- App: `experiments/source_ring_bench/`.
- Build: `./scripts/build_source_ring_bench.sh`.
- Flash: `./scripts/flash_source_ring_bench.sh /dev/cu.wchusbserial5A470211841`.
- 2026-05-11 double-buffer rearm result at `192x145 RGB565`: `120/120`, no drops/errors, `completed_fps ~= 49.65`, `avg_capture_us ~= 19980`.
- 2026-05-11 low-level cyclic LCD_CAM/GDMA ring result at `192x145 RGB565`: `120/120`, no drops/errors, `completed_fps ~= 50.09`, `avg_capture_us ~= 19967`.
- 2026-05-11 low-level cyclic LCD_CAM/GDMA ring result at native visible `160x144 RGB565`: `120/120`, no drops/errors, `completed_fps ~= 60.53`, `avg_capture_us ~= 16520`, `target_rate_met=true`.
- Interpretation: ESP32-P4 source ingress can sustain native visible-size GBC RGB565 payload rate in the isolated counters-only benchmark. The older `~50 fps` number was for the oversized `192x145` compatibility buffer (`55,680` bytes) instead of native visible `160x144` (`46,080` bytes). Next source work is turning the low-level ring into a production source frame ring and then validating image phase/line geometry, not more browser or SPI tuning.
- Host-side evidence collector: `host/collect_source_ring_bench.py`.
- Current evidence artifact: `captures/benchmarks/source_ring/20260511T184144Z-source-ring-bench/`, showing `60.532 fps`, `target_rate_met=True`, `drops=0`, and `dma_errors=0` for native visible `160x144`.
- Normal lab firmware command added: `SOURCE_RING_LOWLEVEL_BENCH <frame_count> [timeout_ms] [width] [height] [RGB565] [pclk_invert] [frame_sync]`.
- 2026-05-11 normal lab firmware result over WCH UART: `SOURCE_RING_LOWLEVEL_BENCH 120 300 160 144 RGB565 0 1` returned `completed_fps=60.532`, `target_rate_met=true`, `dropped_frames=0`, `dma_errors=0`, `ring_rearm_failures=0`, and `unknown_eof_desc=0`.

Current internal pipeline proof commands:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/gbc_probe.py --port /dev/cu.usbmodem14301 --timeout 10 command "PIPELINE_BENCH 300 160 144 2 60"
```

Known result from 2026-05-10: `300/300`, `target_met=true`, `dropped_frames=0`, `source_fps=60.124`, `processed_fps=60.124`, `output_fps=60.124`, `max_frame_us=1833`.

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/gbc_probe.py --port /dev/cu.usbmodem14301 --timeout 10 command "PIPELINE_BENCH 300 160 144 2 0"
```

Known result from 2026-05-10: about `550.9 fps` for synthetic 160x144 RGB565 generate/process/output-sink.

Current no-payload real-source benchmark:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/gbc_probe.py --port /dev/cu.usbmodem14301 --timeout 8 command "GBC_SOURCE_BENCH 5 300 RGB565 0"
```

Known result from 2026-05-10: `5/5` captures, `capture_fps=30.801`, `avg_capture_us=32458`, `max_capture_us=33484`, `expected_capture_bytes=55680`, `expected_emit_bytes=46690`.

Current real-source pipeline benchmark:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/gbc_probe.py --port /dev/cu.usbmodem14301 --timeout 10 command "GBC_PIPELINE_BENCH 5 300 RGB565 0 60"
```

Known result from 2026-05-10: `5/5` captures, `capture_fps=28.057`, `processed_fps=28.057`, `output_fps=28.057`, `target_met=false`, `budget_miss_frames=5`, `avg_capture_us=33454`, `avg_process_us=2170`. RGB332 comparison returned `30.543 fps`, so color depth is not the main limit.

Current setup-reuse benchmark:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/gbc_probe.py --port /dev/cu.usbmodem14301 --timeout 10 command "GBC_PIPELINE_BENCH_PERSIST 10 300 RGB565 0 60"
```

Known result from 2026-05-10: `10/10` frames, `capture_fps=28.939`, `avg_capture_us=27807`, `avg_process_us=2167`, `target_met=false`. Short 3-frame RGB565 run reached `30.756 fps`; RGB332 10-frame run reached `29.658 fps`.

Current raw rearm benchmark:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/gbc_probe.py --port /dev/cu.usbmodem14301 --timeout 8 command "GBC_REARM_BENCH 8 300 RGB565 0"
```

Known result from 2026-05-10: `8/8` raw chunks, `failed_rearms=0`, `chunk_fps=44.397`, `avg_chunk_us=19935`, `payload_mbytes_per_s=2.472`. RGB332 comparison returned `49.139 chunks/sec` with a similar `avg_chunk_us=19914`. This benchmark is not frame-aligned; use it only as raw LCD_CAM/GDMA rearm evidence.

Current RGB565 frame-phase rearm benchmark:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/gbc_probe.py --port /dev/cu.usbmodem14301 --timeout 8 command "GBC_FRAME_REARM_BENCH 8 300 RGB565 0"
```

Known result from 2026-05-10: `8/8` chunks, `start_trigger_seen=true`, `failed_rearms=0`, `chunk_fps=43.953`, `avg_chunk_us=19897`, `avg_chunk_vs_expected_pct=118.8`. RGB332 is no longer tested on this path; RGB565 is the working color baseline.

Current-USB capture-card smoke test:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/gbc_probe.py --port /dev/cu.usbmodem14301 --timeout 30 binary-sequence "GBC_SOURCE_STREAM_BIN 16 300 RGB565 46690 0" --count 16 --output-dir captures/experiments/current_usb_capture_card_smoke
```

Known result from 2026-05-10: `16/16` RGB565 payloads received, `46690` bytes each, `747040` total payload bytes, `5.975 fps` delivered to host. This proves a simple capture-card-style stream works over current USB, but only at low FPS.

Temporary stripped blob stream:

- Command: `GBC_CAPCARD_STREAM_BIN <frame_count> [timeout_ms] [emit_len] [pclk_invert]`
- It emits one JSON preamble followed by one binary blob with 20-byte frame records plus RGB565 payloads.
- Result from 2026-05-10: unreliable on current USB Serial/JTAG stdio path. `4` frames expected `186840` bytes but received `186816`; `1` frame expected `46710` but received `46656`. Chunked writes and flush/yield did not fix it.
- Keep using `GBC_SOURCE_STREAM_BIN` per-frame binary sequence for current-USB smoke tests until a non-stdio USB-device data endpoint exists.

USB Host documentation conclusion: ESP32-P4 USB Host is for the ESP32-P4 controlling external USB devices. It is not the API for streaming frames from ESP32-P4 to the computer; that requires USB device mode, currently USB Serial/JTAG or a future TinyUSB device bulk/vendor/CDC data plane.

Interpretation: internal processing headroom is good; current real-source capture still needs a persistent LCD_CAM/GDMA source driver to reach the GBC frame rate. Browser/USB FPS should not be used as the proof of internal pipeline performance.
- `SAFE_IDLE`
- `ELECTRICAL_ISOLATE`

Profile validation:

```sh
python3 host/tools/validate_profile.py profiles/gbc_lcd.json
```

## Current Known Truth

- GBC source capture and live monitor work in RGB565 mode.
- RGB565 is the active GBC baseline; RGB332 is retained only as generic LCD_CAM/history support.
- RGB666 full source preservation is still an offline/research path, not the live baseline.
- The source appears to behave like `160` visible transfers plus a trailing `161st` byte/transfer and `145` rows per frame period. This is still a hypothesis.
- `SPS` is the strongest frame-marker candidate.
- `LP` and `SPL` are line-related, but exact semantics are not fully proven.
- `DCLK` is likely a gated LCD transfer clock, not a raw internal PPU dot clock.
- Visual success is evidence, not proof. Vary content with a cartridge before finalizing color/timing.

## Current Priorities

1. Keep the working GBC live capture path intact.
2. Continue hardening the method-aligned browser UI around `Project`, `Source`, `Processing`, `Destination`, `Live`, `Artifacts`, `Profile`, `Logs`.
3. Continue the React + TypeScript + Ant Design frontend under `host/workbench/frontend/` without changing capture endpoints.
4. Add artifact manifests to new captures.
5. Extract reusable host code into `host/lab/` without breaking old script names.
6. Harden target/source profile schema and validation.
7. Define ESP32-P4 processing block interfaces before building retimer/scaler/product modes.

## Do Not Break

- Do not move/delete capture artifacts without documenting replacements.
- Do not rename commands without compatibility aliases.
- Do not move `host/live_lcdcam_stream_viewer.py` until a wrapper exists and current GBC live view is verified.
- The Ant frontend now has a native live canvas and is served by `host/live_lcdcam_stream_viewer.py` for non-API routes. The Python server remains the capture/API backend.
- Do not silently rewrite `profiles/gbc_lcd.json`.
- Do not treat GPIO32 CLS data as current; it is historical.
- Do not assume standard `VSYNC`/`HSYNC`/`DE` semantics.

## When Starting A New Task

1. Read this file.
2. Check `docs/DECISIONS.md` for constraints.
3. Check `profiles/gbc_lcd.json` for machine-readable current wiring/presets.
4. If changing docs or structure, update `docs/DOCS_INDEX.md`.
5. If changing behavior, preserve old commands and verify the GBC baseline.
