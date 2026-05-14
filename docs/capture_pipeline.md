# Capture Pipeline

## 1. Objective

Define the firmware-to-host capture flow used to collect, preserve, decode, and inspect LCD bus traces.

This matters because the host side is the primary experimentation layer and captures must be reproducible.

## 2. Current Understanding

Current hypothesis: initial capture should be passive and generic, with USB dumps decoded offline by Python tools. The first firmware baseline exposes command handling and diagnostics but does not enable capture commands during Phase 1. The first host tool is `host/gbc_probe.py`, which provides command transport and smoke tests.

Evidence: `PROJECT_CHARTER.md` defines the initial workflow as GBC LCD bus to ESP32-P4 capture firmware to USB dump to Python decoder to PNG reconstruction.

Confidence level: high for offline-first workflow, low for implementation details.

## 3. Unknowns

- Raw trace binary format.
- USB command framing.
- Timestamp representation.
- Buffer size and overflow reporting.
- Whether initial capture uses GPIO polling, interrupt sampling, RMT-style sampling, or LCD_CAM/DVP.

## 4. Experiment Results

2026-05-08: Added `CORE_STATUS` and pinned the USB command task to HP core 1. Since current CPU-polled capture commands run inside the USB command task, this prevents FreeRTOS from migrating those capture loops between HP cores. Verified on-device:

```json
{"command":"CORE_STATUS","current_core":1,"esp_timer_isr_affinity":"CPU0","esp_timer_task_affinity":"CPU0","freertos_cores":2,"main_task_affinity":"CPU0","ok":true,"usb_protocol_task_pinned_core":1}
```

This is a jitter-control improvement for experiments, not a replacement for DMA/peripheral capture.

2026-05-06: Added firmware command responses for `PING`, `GET_VERSION`, and `EXPORT_STATS`. Capture commands return `unsupported_in_phase1`.

2026-05-06: Verified command transport over USB-Serial/JTAG. Baseline command responses are usable as a firmware smoke test.

2026-05-06: Added `host/gbc_probe.py` for serial port listing, command execution, JSON parsing, and smoke testing.

2026-05-06: `host/gbc_probe.py smoke` passed against the flashed ESP32-P4 on `/dev/cu.usbmodem14201`.

2026-05-06: Added `host/record_experiment.py` to create timestamped experiment folders with metadata, notes, a Phase 1 voltage template, and optional smoke-test output.

2026-05-06: Added stable Phase 1 command scaffolding for `GET_PINMAP`, `MEASURE_CLOCKS`, `CAPTURE_TIMING`, `CAPTURE_RAW`, `CAPTURE_FRAME`, `SET_TRIGGER`, and `DUMP_BUFFER`. Capture commands remain blocked with `no_capture_pins_configured`.

2026-05-06: Expanded host smoke test passed against the flashed ESP32-P4. This verifies that command names are stable while capture remains disabled.

2026-05-06: Added Phase 1 CSV validation tool to generate pinmap proposal reports from measured voltage evidence.

2026-05-07: Added `MEASURE_DCLK <gpio> <duration_ms>` as a Phase 1 input-only timing command. It uses ESP-IDF PCNT rising-edge counting and is intended for MHz-class clock discovery before full trace capture exists.

2026-05-07: Added `CAPTURE_TIMING_EDGES <duration_ms>` and `host/analyze_timing_edges.py`. This creates raw JSON, CSV, and summary JSON artifacts for relative timing analysis of `SPL`, `PS`, `LP`, `CLS`, and `SPS`.

2026-05-07: Added `host/analyze_timing_relationships.py`, which converts saved timing-edge captures into SPS-to-SPS frame windows and reports per-frame signal counts and relative ordering.

2026-05-07: Added `host/capture_timing_session.py` for repeatable timing sessions. It supports manual triggers, pre-capture delay, repeated captures, raw JSON/CSV exports, per-run relationship analysis, and a combined session report.

2026-05-08: `CAPTURE_TIMING_EDGES` now includes a `red6` snapshot in each event when red lines R0-R5 are connected. This is a timing-edge data correlation tool, not pixel-rate RGB capture.

2026-05-08: Added exploratory `CAPTURE_RED_DCLK <sample_count> <timeout_ms>` and `host/capture_red_dclk.py`. It waits for `SPL` and polls for `DCLK` rising edges while sampling `red6`. The first run observed red transitions but missed most DCLK edges, so this tool is for feasibility evidence only.

2026-05-08: Added `DVP_PROBE_ALLOC`, an ESP-IDF generic DVP controller allocation probe. It does not route GPIOs or start capture. Initial on-device test failed with `ESP_ERR_NO_MEM` until PSRAM was enabled, because the ESP-IDF DVP DMA driver allocates descriptors with `MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA`. With `CONFIG_SPIRAM=y`, `CONFIG_SPIRAM_MODE_HEX=y`, and `CONFIG_SPIRAM_SPEED_200M=y`, the probe succeeded for `RAW8` at `160x144` with a `23040` byte frame length.

2026-05-08: Added `host/live_red_viewer.py`, a browser-based live viewer for the exploratory red-only polling capture. It repeatedly issues `CAPTURE_RED_DCLK`, renders the latest sample strip, keeps a scrolling waterfall, and shows sample statistics. This is a live observability tool, not a frame reconstruction path.

2026-05-08: Added experimental `DVP_CAPTURE_RAW <SPL|LP> <timeout_ms>` firmware command and `host/capture_dvp_raw.py`. The firmware maps the current six red bits into RAW8 bits 0-5 and uses already-connected `PS`/`CLS` as placeholder bits 6-7 because the generic DVP driver requires 8-bit input. The host tool saves raw JSON, raw binary, and a `160x144` red-only PNG from the lower six bits.

2026-05-08: First successful peripheral-backed frame-shaped capture: `DVP_CAPTURE_RAW SPL 1500 0 0` succeeded with `SPS`/VSYNC not inverted and `SPL`/DE not inverted. The generated PNG is `captures/decoded/dvp_raw/20260507T225520Z-dvp_raw_spl.png`, `160x144`. Earlier attempts with the generic driver's default inverted VSYNC timed out, as did inverted DE tests. This strongly suggests the generic DVP path can lock when `SPS` is treated as non-inverted VSYNC and `SPL` as non-inverted DE.

2026-05-08: Added `host/live_dvp_viewer.py`, a browser live viewer that repeatedly issues `DVP_CAPTURE_RAW` and draws the returned `160x144` RAW8 buffer. This is a low-FPS debug stream over the current JSON command channel, not a production USB video stream.

2026-05-08: Updated the experimental DVP RAW8 data mapping for the newly connected green lines. The capture now packs upper red and green data bits as `R2,R3,R4,R5,G2,G3,G4,G5` into RAW8 bits `0..7`, reported as `packing=rg44_upper_bits`. This intentionally sacrifices low color bits so one generic RAW8 frame can show red/green image structure before a wider capture path is implemented.

2026-05-08: Added `host/render_dvp_stride_scan.py` to reinterpret a saved RAW8 byte stream with alternate row strides and offsets. This is an offline hypothesis tool for checking whether a consistent but misaligned capture can be recovered by changing row slicing without recapturing hardware data.

2026-05-08: Added `host/postprocess_dvp_rg44.py` for post-capture image shifting experiments. It can globally roll X/Y, invert intensity, reverse 4-bit color order, swap red/green channels, and apply a per-line horizontal shift to test whether row phase drift is hiding recognizable structure.

2026-05-08: Added `host/debug_dvp_capture_viewer.py`, an offline browser UI for saved RAW8 captures. It serves only the local `.bin` file and performs shift/skew/invert/channel transforms in JavaScript, so it can be used without live serial capture or additional hardware stress.

2026-05-08: FPGA core review changed the next capture priority. The physical `DCLK` rate appears closer to visible-pixel transfer (`~152 clocks per LP`) than to the internal PPU dot clock (`456 dots per line`). Before more DVP frame captures, add a DCLK-per-line measurement tool that counts DCLK pulses between LP/SPL/SPS events. This will tell us whether the ESP32 camera peripheral should capture fixed-width visible bursts or whether a custom edge-counted line assembler is more appropriate.

2026-05-08: Added `CAPTURE_LINE_CLOCKS <LP|SPL> <falling|rising> <line_count> <timeout_ms>` and `host/capture_line_clocks.py`. The first polling implementation missed some line markers, producing multiples of the base interval; it was replaced with an ISR-backed marker snapshot path. The ISR-backed SPL falling capture produced almost all intervals at `160..162` DCLK rising edges, with one startup partial interval. This points away from generic DVP frame capture and toward a custom line-burst capture path keyed by SPL.

2026-05-08: Added experimental `CAPTURE_RG_LINE_BURSTS <width> <height> <timeout_ms>` and `host/capture_rg_line_bursts.py`. This command arms on `SPS` rising, waits for `SPL` falling per line, and samples `R2-R5/G2-G5` into RAW8 `rg44_upper_bits` on `DCLK` rising. The first implementation using `gpio_get_level()` captured only about `18..22` samples per line. Switching to direct `GPIO.in` register reads improved this to `96..112` samples per line. Removing `esp_timer_get_time()` from the pixel inner loop produced a complete `160x144` capture with all 144 lines reporting 160 samples.

Complete line-burst artifacts:

- Raw JSON: `captures/decoded/rg_line_bursts/20260508T162521Z-rg_line_bursts_160x144.json`
- Raw BIN: `captures/decoded/rg_line_bursts/20260508T162521Z-rg_line_bursts_160x144.bin`
- PNG: `captures/decoded/rg_line_bursts/20260508T162521Z-rg_line_bursts_160x144.png`
- Inverted PNG: `captures/decoded/rg_line_bursts/20260508T162521Z-rg_line_bursts_160x144_inverted.png`
- Summary: `captures/decoded/rg_line_bursts/20260508T162521Z-rg_line_bursts_160x144_summary.json`

Result: `frame_sync_seen=true`, `timeout=false`, `captured_lines=144`, `complete_line_count=144`, `line_count_min=160`, `line_count_max=160`, `checksum=5298135`, `transition_count=1638`. The render is frame-shaped and no longer the previous vertical-bar DVP failure, but it is still visually distorted. The next problem is sample phase, bit/channel interpretation, or line selection/order, not basic line-burst acquisition.

2026-05-08: Added `host/postprocess_rg_line_bursts.py` for line-burst-specific offline hypotheses. It can generate decimation sheets, tile extraction sheets, and phase sheets from a saved `rg44_upper_bits` raw capture. Generated artifacts from the first complete line-burst frame:

- `captures/decoded/rg_line_bursts/20260508T162521Z-rg_line_bursts_decimation_sheet.png`
- `captures/decoded/rg_line_bursts/20260508T162521Z-rg_line_bursts_tile_5x5_sheet.png`
- `captures/decoded/rg_line_bursts/20260508T162521Z-rg_line_bursts_phase_5x5_sheet.png`
- `captures/decoded/rg_line_bursts/20260508T162521Z-rg_line_bursts_phase_x5_sheet.png`
- `captures/decoded/rg_line_bursts/20260508T162521Z-rg_line_bursts_phase_y5_sheet.png`

Visual interpretation: the capture contains repeated recognizable fragments rather than random noise. The simple 5-phase decimation did not collapse it into a clean frame, so the current distortion is probably not just "take every fifth pixel/line." The repeated structure is now treated primarily as evidence of an unresolved line/window timing model. Possible explanations include using the wrong marker, starting from the wrong marker offset after `SPS`, sampling only one of several LCD-driver transfer windows, or encountering panel-driver-specific multiplexing behavior. Do not assume this is a color-depth problem.

User visual inspection identified `20260508T162521Z-rg_line_bursts_160x144_tile_r3_c0_scaled.png` as the best current decode: it shows dark text in the expected screen position without obvious repetition, but at lower resolution. This tile corresponds to the `5x5` tile sheet cell row `3`, column `0`, i.e. raw region `x=0..31`, `y=84..111`, scaled up for viewing. Larger extracted artifacts:

- `captures/decoded/rg_line_bursts/20260508T162521Z-rg_line_bursts_160x144_best_tile_r3_c0_normal_320x288.png`
- `captures/decoded/rg_line_bursts/20260508T162521Z-rg_line_bursts_160x144_best_tile_r3_c0_inverted_320x288.png`
- `captures/decoded/rg_line_bursts/20260508T162521Z-rg_line_bursts_tile_5x5_sheet_large.png`

Attempted 5x5 phase-plane untile reconstruction from the same raw frame, but it did not recover a clean high-resolution image. Current working hypothesis: the best tile is a coherent low-resolution representation of the screen, and the other tiles are not yet explained by simple x/y phase interleaving. Treat them as timing-window evidence until `LP`, `SPL`, and marker-offset captures are compared systematically.

2026-05-08: Updated `CAPTURE_RG_LINE_BURSTS` to accept `[rising|falling]` sample edge. Falling-edge capture also produced a complete `160x144` frame:

- Raw JSON: `captures/decoded/rg_line_bursts/20260508T163722Z-rg_line_bursts_160x144.json`
- PNG: `captures/decoded/rg_line_bursts/20260508T163722Z-rg_line_bursts_160x144.png`
- Inverted PNG: `captures/decoded/rg_line_bursts/20260508T163722Z-rg_line_bursts_160x144_inverted.png`

Result: `complete_line_count=144`, `checksum=5335527`, `transition_count=1578`. Falling edge changes the image but does not remove the repeated-subfield pattern.

2026-05-08: Updated `CAPTURE_RG_LINE_BURSTS` and `host/capture_rg_line_bursts.py` to accept line marker and marker-skip hypotheses:

- Firmware command: `CAPTURE_RG_LINE_BURSTS <width> <height> <timeout_ms> [rising|falling] [SPL|LP] [skip_markers] [dclk_delay_edges] [marker_stride] [marker_phase]`
- Host flags: `--edge`, `--marker`, `--skip-markers`, `--dclk-delay-edges`, `--marker-stride`, and `--marker-phase`

This was added after reviewing the repeated mini-screen artifacts. The repeated structure may mean the capture is keyed to the wrong timing signal or offset, not that the image needs blue data. New LP-keyed artifacts:

- `captures/decoded/rg_line_bursts/20260508T173537Z-rg_line_bursts_160x144.png`: `LP`, `skip_markers=0`, rising edge, `complete_line_count=144`, `checksum=5346840`, `transition_count=1388`
- `captures/decoded/rg_line_bursts/20260508T173452Z-rg_line_bursts_160x144.png`: `LP`, `skip_markers=5`, rising edge, `complete_line_count=144`, `checksum=5386365`, `transition_count=1348`
- `captures/decoded/rg_line_bursts/20260508T173752Z-rg_line_bursts_160x144.png`: `LP`, `skip_markers=10`, rising edge, `complete_line_count=144`, `checksum=5369280`, `transition_count=1402`

Result: changing marker and marker skip changes the placement of the repeated recognizable fragments while preserving coherent geometry. This supports the user-raised concern that the repeated images are more likely a timing/marker misuse artifact than a field/color-depth issue.

2026-05-08: Added and flashed DCLK-delay and marker-phase controls. These are intended to test the user's observation that the raw frame looks like roughly 25 low-resolution images packed into one capture, which may indicate we are sampling too slowly, capturing repeated transfer windows, or forcing smaller LCD-driver bursts into a `160x144` framebuffer.

New artifacts after the firmware update:

- `captures/decoded/rg_line_bursts/20260508T174502Z-rg_line_bursts_160x144.png`: `LP`, `skip_markers=10`, `dclk_delay_edges=0`, `marker_stride=1`, `marker_phase=0`, checksum `5357805`, transitions `1558`
- `captures/decoded/rg_line_bursts/20260508T174536Z-rg_line_bursts_160x144.png`: `LP`, `skip_markers=10`, `dclk_delay_edges=8`, `marker_stride=1`, `marker_phase=0`, checksum `5390700`, transitions `1482`
- `captures/decoded/rg_line_bursts/20260508T174616Z-rg_line_bursts_160x144.png`: `LP`, `skip_markers=10`, `dclk_delay_edges=32`, `marker_stride=1`, `marker_phase=0`, checksum `5365455`, transitions `1498`
- `captures/decoded/rg_line_bursts/20260508T174651Z-rg_line_bursts_160x144.png`: `LP`, `skip_markers=10`, `dclk_delay_edges=0`, `marker_stride=5`, `marker_phase=0`, checksum `5394015`, transitions `1450`
- `captures/decoded/rg_line_bursts/20260508T174818Z-rg_line_bursts_160x144.png`: `LP`, `skip_markers=10`, `dclk_delay_edges=0`, `marker_stride=5`, `marker_phase=1`, checksum `5366985`, transitions `1468`
- `captures/decoded/rg_line_bursts/20260508T174839Z-rg_line_bursts_160x144.png`: `LP`, `skip_markers=10`, `dclk_delay_edges=0`, `marker_stride=5`, `marker_phase=2`, checksum `5340720`, transitions `1474`
- `captures/decoded/rg_line_bursts/20260508T174900Z-rg_line_bursts_160x144.png`: `LP`, `skip_markers=10`, `dclk_delay_edges=0`, `marker_stride=5`, `marker_phase=3`, checksum `5385090`, transitions `1498`
- `captures/decoded/rg_line_bursts/20260508T174921Z-rg_line_bursts_160x144.png`: `LP`, `skip_markers=10`, `dclk_delay_edges=0`, `marker_stride=5`, `marker_phase=4`, checksum `5378205`, transitions `1406`

Result: DCLK delay moves the sampled content, and stride-5 phase selection changes the texture, but no tested phase collapses the repeated low-resolution views into a clean full-resolution screen. Next hypothesis to test: the valid transfer window may be narrower than 160 samples per marker or segmented inside the marker interval.

2026-05-08: Added `stop_on_next_frame` support to `CAPTURE_RG_LINE_BURSTS` and `--single-frame` to `host/capture_rg_line_bursts.py`. This corrects an important diagnostic flaw: stride captures should not keep collecting across several `SPS` frames just to fill a `144`-row output image.

Single-frame verification artifacts:

- `captures/decoded/rg_line_bursts/20260508T175615Z-rg_line_bursts_160x144.png`: `LP`, `skip_markers=10`, `marker_stride=1`, `--single-frame`, complete `144` rows.
- `captures/decoded/rg_line_bursts/20260508T175636Z-rg_line_bursts_160x144.png`: `LP`, `skip_markers=10`, `marker_stride=5`, `marker_phase=4`, `--single-frame`, only `35` rows captured before next frame sync.

Conclusion: the previous full-height stride-5 images were stacking multiple physical frames into one PNG. They are useful diagnostics, but not framebuffer candidates. The remaining repeated structure in the single-frame stride-1 capture must be solved by a faster/more faithful capture path or by identifying a narrower valid transfer window.

2026-05-09: Added browser Power-Cycle Monitor support to the live LCD_CAM workbench. This diagnostic path is deliberately outside the frame pipeline: it stops continuous capture, enters `SAFE_IDLE`, and records coarse GPIO edge-count windows for `DCLK`, `SPS`, `SPL`, `LP`, `PS`, and `CLS`.

Initial verification run:

- API: `/api/power-monitor/start?duration_ms=1200&window_ms=20`
- Artifact directory: `captures/experiments/20260509T163645Z-power_cycle_monitor/`
- Result while the target source was off: 2 samples, classified `off`, with zero edges on all monitored signals.

Interpretation: this gives a baseline for diagnosing the restart failure separately from frame capture. A successful target start should transition from `off` toward `locked`; a failed start can be compared by whether DCLK appears without SPS, line pulses appear without frame sync, or signals remain static.

2026-05-09: Updated the live capture pipeline so source loss is handled as part of normal streaming. After a frame timeout or capture failure, the host stops issuing LCD_CAM capture commands, sends `SAFE_IDLE`, and polls only `DCLK`/`SPS` until source timing returns. Once source timing is detected, the same live worker resumes frame capture.

This better matches the desired monitor model:

```text
GBC bus lines may be on or off
    -> ESP32-P4 stays passive while source timing is absent
    -> host/browser stream remains alive
    -> frame capture resumes when source timing returns
```

The workbench now exposes `source_state`, `source_wait_ms`, and the latest source probe in `/api/status`. The current live run uses `--capture-timeout-ms 800` to detect source-off faster than the earlier 2500 ms setting.

UI direction update: `Live View` is now the default browser tab and primary monitor workflow. Boot capture and power-cycle monitor controls were removed from the visible dashboard so the tool does not create a new workflow for every source-loss issue.

## 5. Next Steps

- Use `host/gbc_probe.py smoke` after every firmware flash.
- Use `host/record_experiment.py` for each measurement session.
- Define a simple raw trace format before writing decoding tools.
- Include metadata: firmware version, pin map, hypothesis name, sample edge, and capture duration.
- Add overflow and dropped-sample counters from the first firmware implementation.
- Keep PCNT clock measurements separate from full capture; they estimate frequency but do not provide phase, pulse width, duty cycle, or RGB sample timing.
- Use `host/analyze_timing_edges.py --duration-ms 100` for current timing relationship captures. Increase duration only if `overflow_count` remains zero.
- Use `host/analyze_timing_relationships.py <raw_timing_json>` after each timing-edge capture to produce frame-level reports.
- Use `host/capture_timing_session.py --manual-trigger` when coordinating GBC power-cycle or boot captures with a human operator.
- Treat timing-edge `red6` snapshots as coarse correlation evidence only; full red reconstruction requires sampling on or near DCLK.
- Move to a peripheral-backed capture path for pixel-rate sampling; polling is not sufficient for complete DCLK-edge capture.
- Inspect the first `160x144` DVP PNG visually, then repeat captures to determine stability and whether the frame start or line alignment drifts.
- Next DVP experiment should validate whether the RAW8 `rg44_upper_bits` render has recognizable structure. Full RGB666 still requires a wider capture path or multiple synchronized captures.
- De-prioritize generic DVP polarity tweaks for now. The line-clock result shows the physical bus is probably a `~160`-clock visible line burst, so a custom SPL/DCLK line assembler is the next better experiment.
- Use the complete `CAPTURE_RG_LINE_BURSTS` artifacts for offline phase/bit-order/channel hypotheses before adding more color wires or moving to full RGB666.
- Run a systematic line-marker search before connecting more color lines: compare `SPL` and `LP`, rising and falling sample edges, and marker skips around `0..10` because `LP` reports 154 events per frame while visible height is 144.
- Add firmware options for line/window selection: capture every Nth marker line, capture only a selected modulo phase, and optionally delay a programmable number of DCLK edges after each marker before storing pixels. This directly tests whether repeated fragments come from the wrong transfer window.
- Add width/window sweeps around narrower burst sizes, especially `32`, `40`, `80`, and `160`, and render them as scaled images/contact sheets. This directly tests whether the apparent 5x5 mini-screen structure comes from forcing smaller chunks into a full-width framebuffer.
- Prioritize a peripheral-backed capture path for more information per unit time. The CPU-polled path is good for controlled hypotheses, but a production reconstruction path should use ESP32-P4 LCD_CAM/DVP-style DMA or another hardware-assisted sampler rather than spanning frames or skipping markers.
- Use the Power-Cycle Monitor for off-to-on failures before changing capture timing. It should tell whether the failure is source timing absent, partial clock-only startup, missing frame sync, or missing line pulses while the ESP32-P4 is in Safe Idle.
- Prefer validating off/on behavior through the normal live stream first. The live stream should enter `no_signal` during source-off, then return to `live` without requiring browser button choreography.
- Graduate the GBC path from diagnostic capture commands into a source-specific firmware driver once the current live baseline is preserved. The driver should keep LCD_CAM/GDMA configured across frames, expose health counters, and stream compact binary frames over native USB. Keep old capture commands as regression tools.

2026-05-10: Added the first GBC source-driver compatibility layer.

New source-specific commands:

- `GBC_SOURCE_STATUS`
- `GBC_SOURCE_FRAME_BIN [timeout_ms] [RGB565] [emit_len] [pclk_invert]`

Current source-driver preset:

- source: `gbc_lcd`
- capture peripheral: ESP32-P4 LCD_CAM/GDMA
- capture size: `192x145`
- stream period: `161x145`
- visible area: `160x144`
- visible source alignment: linear stream shift `-4` pixels
- data mode: `RGB565`

The `161x145` stream is not displayed directly. GBC-specific visible-frame consumers should call the source alignment path (`gbc_lcd_source_copy_visible_rgb565`) or apply the equivalent host crop: `src_pixel = y * 161 + x - 4`, with negative source pixels clamped to the first captured pixel. This alignment was measured with the deterministic GBC alignment ROM on 2026-05-11.

For direct SPI LCD output, the same shift is applied inside the existing RGB565-to-panel conversion loop, so the 1x production display path does not allocate or copy a separate corrected `160x144` frame. Use the copy helper only when a downstream block requires a contiguous visible source buffer, such as current PPA handoff paths.
- start marker: `SPS`
- PCLK invert: `false`
- DE model: held high for the current compatibility capture
- app/data transport: native USB Serial/JTAG on `/dev/cu.usbmodem14301`
- recovery/flashing transport: WCH UART on `/dev/cu.wchusbserial5A470211841`

Direct source-driver verification:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/gbc_probe.py --port /dev/cu.usbmodem14301 --timeout 10 binary 'GBC_SOURCE_FRAME_BIN 300 RGB565 0 0' -o /tmp/gbc_source_frame.bin
```

Result: `binary_len=46690`, `payload_len=46690`, `capture_us` about `37 ms`.

The browser backend now supports:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python -B host/live_lcdcam_stream_viewer.py --port /dev/cu.usbmodem14301 --listen-port 8791 --no-pclk-invert --interval-ms 33 --capture-timeout-ms 300 --width 192 --height 145 --data-mode RGB565 --firmware-binary --gbc-source-driver --no-source-binary --continuous-capture --stream-batch-size 1 --host-crop --crop-offset 0 --crop-width 161 --crop-height 145 --profile profiles/gbc_lcd.json
```

Current measured browser status: `/api/status` reports `source_state=live` and around `5.4 fps`; `/api/frame.bin` returns `46690` bytes.

Important transport lesson: binary frame commands must not rely on line-oriented serial parsing after the payload starts. The host now discards stale input before top-level commands and can recover JSON headers prefixed by leftover binary bytes. Firmware now writes binary payloads through an explicit chunked `write()` loop and sends a newline after the payload to flush the final USB short packet without changing `binary_len`.

2026-05-10: Added `GBC_SOURCE_STREAM_BIN` as a GBC-specific batched stream command.

Purpose: remove one host command round trip per frame while keeping the current source-driver capture model intact.

Command:

```sh
GBC_SOURCE_STREAM_BIN <frame_count_1_to_64> [timeout_ms_1_to_5000] [RGB565] [emit_len_0_to_frame_bytes] [pclk_invert_0_or_1]
```

Direct benchmark:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/gbc_probe.py --port /dev/cu.usbmodem14301 --timeout 30 binary-sequence 'GBC_SOURCE_STREAM_BIN 32 300 RGB565 0 0' --count 32
```

Result:

- `32` of `32` frames received.
- Each RGB565 payload was `46690` bytes.
- Total payload was `1494080` bytes.
- Elapsed time was `4.83 s`.
- Effective stream rate was `6.626 fps`.
- Per-frame firmware `capture_us` was typically about `29 ms` to `34 ms`.

Browser backend now uses the GBC source stream command when `--gbc-source-driver --stream-batch-size N` are both active. Current launch:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python -B host/live_lcdcam_stream_viewer.py --port /dev/cu.usbmodem14301 --listen-port 8791 --no-pclk-invert --interval-ms 33 --capture-timeout-ms 300 --width 192 --height 145 --data-mode RGB565 --firmware-binary --gbc-source-driver --no-source-binary --continuous-capture --stream-batch-size 8 --host-crop --crop-offset 0 --crop-width 161 --crop-height 145 --profile profiles/gbc_lcd.json
```

Observed browser status after clean restart:

- `source_state=live`
- `server_capture_fps` about `7.1`
- `/api/frame.bin` returns a fresh `46690` byte frame.

Interpretation: batching is useful but not enough. The current RGB565 stream pushes about `46.7 KB` per frame through USB Serial/JTAG plus one JSON header per frame. The measured throughput is roughly consistent with the data-plane bandwidth being a major limiter. A 60 fps RGB565 monitor would require about `2.8 MB/s` for this payload size before headers and overhead. Therefore the next high-FPS work should prioritize:

- measuring native USB Serial/JTAG maximum binary throughput explicitly
- adding timing counters for capture time vs USB write time
- evaluating TinyUSB bulk/vendor transport for the data plane
- reducing payload only when the selected lab/product mode allows it
- keeping persistent LCD_CAM/GDMA work, but not assuming it alone can overcome the transport limit

2026-05-10: Added `USB_BENCH_STREAM_BIN` to separate USB transport throughput from capture throughput.

Purpose: prove whether the current native USB Serial/JTAG command/data path can carry full-rate RGB565 frames before changing LCD_CAM timing again. This benchmark emits deterministic synthetic binary frames and does not touch GPIO, LCD_CAM, DMA, or the GBC bus.

Command:

```sh
USB_BENCH_STREAM_BIN <frame_count_1_to_256> <payload_len_0_to_262144>
```

RGB565-sized benchmark:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/gbc_probe.py --port /dev/cu.usbmodem14301 --timeout 30 binary-sequence 'USB_BENCH_STREAM_BIN 64 46690' --count 64
```

Result:

- `64` of `64` payloads received.
- Each payload was `46690` bytes.
- Total payload was `2988160` bytes.
- Elapsed time was `7.086 s`.
- Effective payload rate was about `0.42 MB/s`.
- Effective RGB565-sized frame rate was `9.032 fps`.

Half-frame benchmark:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/gbc_probe.py --port /dev/cu.usbmodem14301 --timeout 30 binary-sequence 'USB_BENCH_STREAM_BIN 128 23345' --count 128
```

Result:

- `128` of `128` payloads received.
- Total payload was again `2988160` bytes.
- Elapsed time was `7.111 s`.
- Effective half-frame rate was `18.0 fps`.

Interpretation: the measured byte rate stayed essentially fixed while payload size changed, so native USB Serial/JTAG is confirmed as the dominant ceiling for the current RGB565 live monitor. Persistent LCD_CAM/GDMA still matters, but it cannot by itself overcome a transport-only ceiling near `9 fps` for `46690` byte frames. The next safe performance step is an isolated TinyUSB data-plane benchmark with synthetic payloads before any GBC capture code is moved.

2026-05-10: Added internal pipeline and no-payload source benchmarks.

Purpose: prove ESP32-P4 internal frame processing separately from USB/browser frame transport. These commands report counters only; they do not stream full frames to the computer.

Commands:

```sh
PIPELINE_BENCH <frame_count> [width] [height] [bytes_per_pixel_1_or_2] [target_fps_0_unlimited]
GBC_SOURCE_BENCH <frame_count_1_to_512> [timeout_ms] [RGB565] [pclk_invert_0_or_1]
GBC_PIPELINE_BENCH <frame_count_1_to_512> [timeout_ms] [RGB565] [pclk_invert_0_or_1] [target_fps_0_to_1000]
```

`PIPELINE_BENCH` is synthetic and does not touch GPIO, LCD_CAM, GDMA, or the GBC bus. It generates a frame in RAM, runs a deterministic processing pass, sends it to an output sink, and reports stage timing.

60 FPS RGB565-sized internal test:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/gbc_probe.py --port /dev/cu.usbmodem14301 --timeout 10 command "PIPELINE_BENCH 300 160 144 2 60"
```

Result:

- `300` of `300` synthetic frames completed.
- `frame_bytes=46080`.
- `target_fps=60`, `target_met=true`.
- `source_fps=60.124`, `processed_fps=60.124`, `output_fps=60.124`.
- `dropped_frames=0`.
- `avg_generate_us=641`, `avg_process_us=1168`, `avg_output_us=2`.
- `max_frame_us=1833`.

Unthrottled internal test:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/gbc_probe.py --port /dev/cu.usbmodem14301 --timeout 10 command "PIPELINE_BENCH 300 160 144 2 0"
```

Result: about `550.9 fps` and about `25.4 MB/s` synthetic frame payload through the internal generate/process/output-sink loop.

No-payload real-source compatibility test:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/gbc_probe.py --port /dev/cu.usbmodem14301 --timeout 8 command "GBC_SOURCE_BENCH 5 300 RGB565 0"
```

Result:

- `5` of `5` real source captures succeeded.
- `expected_capture_bytes=55680`.
- `expected_emit_bytes=46690`.
- `total_received_bytes=278400`.
- `capture_fps=30.801`.
- `avg_capture_us=32458`, `max_capture_us=33484`.

Interpretation: the ESP32-P4 has enough CPU/memory headroom for a simple 160x144 RGB565 processing chain at 60 FPS when frames are already internal. The current real-source compatibility path is still limited by per-frame LCD_CAM setup/teardown and reaches about `30 fps` before any full-frame USB transport is added. The next performance milestone is a persistent source driver that keeps capture configured across frames and reports dropped frames, DMA overruns, sync loss, and ring occupancy without using USB frame streaming as the proof.

Real-source pipeline benchmark:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/gbc_probe.py --port /dev/cu.usbmodem14301 --timeout 10 command "GBC_PIPELINE_BENCH 5 300 RGB565 0 60"
```

Result:

- `5` of `5` frames captured, processed, and passed to the firmware output sink.
- `capture_fps=28.057`, `processed_fps=28.057`, `output_fps=28.057`.
- `target_fps=60`, `target_met=false`, `budget_miss_frames=5`.
- `capture_frame_bytes=55680`, `emit_frame_bytes=46690`.
- `avg_capture_us=33454`, `max_capture_us=42168`.
- `avg_process_us=2170`, `max_process_us=2174`.
- `avg_output_us=1`, `max_output_us=2`.

Historical RGB332 comparison from older firmware, no longer accepted by the active GBC source commands:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/gbc_probe.py --port /dev/cu.usbmodem14301 --timeout 10 command "GBC_PIPELINE_BENCH 10 300 RGB332 0 60"
```

Result:

- `10` of `10` frames captured, processed, and passed to the firmware output sink.
- `capture_fps=30.543`.
- `target_met=false`, `budget_miss_frames=10`.
- `avg_capture_us=31646`, `max_capture_us=32396`.
- `avg_process_us=1084`.

Interpretation: reducing color depth only improves the compatibility path slightly, so the main bottleneck is not color conversion or simple processing. The firmware reports `performance_path=per_frame_lcdcam_setup_teardown` and `next_performance_path=persistent_lcdcam_gdma_ring` to make the remaining engineering step explicit. The next driver should keep LCD_CAM/GDMA configured across frames, reuse descriptors, and expose telemetry counters before it attempts to stream image payloads.

2026-05-10: Added first LCD_CAM/GDMA setup-reuse benchmark.

Purpose: test whether the roughly `30 fps` real-source ceiling is caused mainly by allocating/configuring LCD_CAM and GDMA for every frame.

Command:

```sh
GBC_PIPELINE_BENCH_PERSIST <frame_count_1_to_512> [timeout_ms] [RGB565] [pclk_invert_0_or_1] [target_fps_0_to_1000]
```

This is not a final continuous ring driver. It reuses the LCD_CAM/GDMA channel, descriptors, buffer, and signal routing across a finite benchmark batch, then returns to safe idle.

Short RGB565 check:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/gbc_probe.py --port /dev/cu.usbmodem14301 --timeout 10 command "GBC_PIPELINE_BENCH_PERSIST 3 300 RGB565 0 60"
```

Result: `3/3` frames, `capture_fps=30.756`, `avg_capture_us=24889`, `avg_process_us=2169`, `target_met=false`.

Sustained RGB565 check:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/gbc_probe.py --port /dev/cu.usbmodem14301 --timeout 10 command "GBC_PIPELINE_BENCH_PERSIST 10 300 RGB565 0 60"
```

Result: `10/10` frames, `capture_fps=28.939`, `avg_capture_us=27807`, `avg_process_us=2167`, `target_met=false`.

Historical RGB332 comparison from older firmware, no longer accepted by the active GBC source commands:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/gbc_probe.py --port /dev/cu.usbmodem14301 --timeout 10 command "GBC_PIPELINE_BENCH_PERSIST 10 300 RGB332 0 60"
```

Result: `10/10` frames, `capture_fps=29.658`, `avg_capture_us=30324`, `avg_process_us=1084`, `target_met=false`.

Interpretation: setup reuse helps the shortest batch, but sustained FPS remains around `29-31 fps`. The remaining limit is likely the current frame-boundary wait plus byte-count EOF pattern, not just allocation or peripheral init. The next performance driver should be a true continuous ring/descriptor stream that arms before the next frame boundary and records EOF/descriptor turnover without restarting capture for every frame.

2026-05-10: Added ISR rearm raw-chunk benchmark.

Purpose: test whether LCD_CAM/GDMA can ingest frame-sized chunks faster when the next DMA buffer is rearmed immediately from the GDMA EOF callback. This benchmark is explicitly not frame-aligned and should not be judged by image correctness.

Command:

```sh
GBC_REARM_BENCH <chunk_count_1_to_512> [timeout_ms] [RGB565] [pclk_invert_0_or_1]
```

Safety note: the first implementation placed too much telemetry on the `usb_protocol` task stack and triggered a stack protection fault. The board rebooted cleanly; telemetry was moved to heap allocation before the benchmark was rerun.

RGB565 short run:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/gbc_probe.py --port /dev/cu.usbmodem14301 --timeout 8 command "GBC_REARM_BENCH 2 300 RGB565 0"
```

Result: `2/2` chunks, `failed_rearms=0`, `chunk_fps=34.813`, `avg_chunk_us=19978`, `payload_mbytes_per_s=1.938`.

RGB565 longer run:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/gbc_probe.py --port /dev/cu.usbmodem14301 --timeout 8 command "GBC_REARM_BENCH 8 300 RGB565 0"
```

Result: `8/8` chunks, `failed_rearms=0`, `chunk_fps=44.397`, `avg_chunk_us=19935`, `max_chunk_us=20657`, `payload_mbytes_per_s=2.472`.

Historical RGB332 comparison from older firmware, no longer accepted by the active GBC source commands:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/gbc_probe.py --port /dev/cu.usbmodem14301 --timeout 8 command "GBC_REARM_BENCH 8 300 RGB332 0"
```

Result: `8/8` chunks, `failed_rearms=0`, `chunk_fps=49.139`, `avg_chunk_us=19914`, `max_chunk_us=20716`, `payload_mbytes_per_s=1.368`.

Interpretation: immediate rearm improves the raw chunk rate beyond the previous `~29-31 fps`, but the chunk time is still about `20 ms`. The similar RGB565 and RGB332 chunk times indicate the benchmark is probably limited by the external source's active transfer window or by the byte-count EOF/chunking model, not by CPU processing. The next step is frame-boundary-aware continuous capture: keep the fast rearm behavior, but align accepted chunks to `SPS`/frame phase and record ring ownership/overrun counters.

2026-05-10: Added RGB565 frame-phase rearm benchmark.

Purpose: keep the fast ISR rearm path, but start the benchmark relative to the source's known frame/line phase. RGB332 testing is intentionally discontinued for this path because RGB565 has shown correct enough visual output and RGB332 timing results were not materially different.

Command:

```sh
GBC_FRAME_REARM_BENCH <chunk_count_1_to_512> [timeout_ms] [RGB565] [pclk_invert_0_or_1]
```

This starts after `SPS` rising followed by `SPL` falling, then records frame-sized byte-count chunks. It still does not prove that every chunk is a final decoded frame; it proves source-phase start plus rearm timing.

Short run:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/gbc_probe.py --port /dev/cu.usbmodem14301 --timeout 8 command "GBC_FRAME_REARM_BENCH 4 300 RGB565 0"
```

Result: `4/4` chunks, `start_trigger_seen=true`, `failed_rearms=0`, `chunk_fps=41.538`, `avg_chunk_us=19758`, `avg_chunk_vs_expected_pct=118.0`.

Sustained run:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/gbc_probe.py --port /dev/cu.usbmodem14301 --timeout 8 command "GBC_FRAME_REARM_BENCH 8 300 RGB565 0"
```

Result: `8/8` chunks, `start_trigger_seen=true`, `failed_rearms=0`, `chunk_fps=43.953`, `avg_chunk_us=19897`, `max_chunk_us=20767`, `payload_mbytes_per_s=2.447`, `avg_chunk_vs_expected_pct=118.8`.

Interpretation: the source-phase trigger works and the rearm path is stable, but chunk time is still about `19.8 ms`, roughly `118%` of a `59.73 Hz` frame period. This suggests the current `192x145x2` byte-count chunk is too large for one native frame period or includes non-frame bytes. The next high-value test is a byte-count/window sweep around the known useful emitted size (`46690` bytes) and visible size (`160x144x2 = 46080` bytes), RGB565 only.

2026-05-10: Current-USB capture-card smoke test.

Purpose: test the simplest "ESP32-P4 as capture card" shape using the USB connection currently available, without browser/UI/lab controls in the hot path.

Command:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/gbc_probe.py --port /dev/cu.usbmodem14301 --timeout 30 binary-sequence "GBC_SOURCE_STREAM_BIN 16 300 RGB565 46690 0" --count 16 --output-dir captures/experiments/current_usb_capture_card_smoke
```

Result:

- `16/16` RGB565 payloads received.
- `46690` bytes per frame payload.
- `747040` total payload bytes.
- `5.975 fps` delivered to the host.
- Every frame reported `ok=true`, `start_trigger_seen=true`, `received_size=55680`, `binary_len=46690`.

Interpretation: the current USB Serial/JTAG path can stream a capture-card-style RGB565 feed, but only around `6 fps` with the current compatibility source path and serial transport. This is useful for smoke testing and low-FPS recording, not enough for final full-rate capture-card behavior.

2026-05-10: Tried a stripped single-blob capture-card stream on current USB.

Purpose: reduce lab protocol overhead by sending one JSON preamble followed by one binary blob containing compact 20-byte frame headers plus RGB565 payloads.

Command added:

```sh
GBC_CAPCARD_STREAM_BIN <frame_count_1_to_64> [timeout_ms] [emit_len_0_to_46690] [pclk_invert_0_or_1]
```

Test commands:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/gbc_probe.py --port /dev/cu.usbmodem14301 --timeout 60 binary "GBC_CAPCARD_STREAM_BIN 4 300 46690 0" -o captures/experiments/current_usb_capture_card_smoke/capcard_blob_4_rgb565.bin
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/gbc_probe.py --port /dev/cu.usbmodem14301 --timeout 20 binary "GBC_CAPCARD_STREAM_BIN 1 300 46690 0" -o captures/experiments/current_usb_capture_card_smoke/capcard_blob_1_rgb565.bin
```

Result:

- `4` frame blob expected `186840` bytes and repeatedly received `186816` bytes.
- `1` frame blob expected `46710` bytes and received `46656` bytes.
- Chunked `fwrite()`, per-frame `fflush()`, and a short yield did not make the blob complete.
- `SAFE_IDLE` succeeded after the tests.

Interpretation: the current USB Serial/JTAG stdio path is not reliable for a large single binary response in this firmware shape. The existing per-frame binary sequence remains the safer current-USB capture-card smoke path. A real capture-card data plane should avoid stdio and use an explicit USB-device endpoint or a host reader designed around smaller framed records.

2026-05-08: Returned to peripheral-backed capture after confirming stride captures can accidentally stack multiple physical frames. Changes:

- `DVP_CAPTURE_RAW` now accepts optional dimensions: `DVP_CAPTURE_RAW <SPL|LP> <timeout_ms> [vsync_invert] [de_invert] [pclk_invert] [h_res] [v_res]`.
- Added `host/capture_isp_dvp_raw.py`.
- Added experimental `ISP_DVP_CAPTURE_RAW <HSYNC:SPL|LP|NC> <DE:SPL|LP|NC> <timeout_ms> [hsync_invert] [vsync_invert] [de_invert] [pclk_invert] [h_res] [v_res]`.

Generic DVP DMA results:

- `captures/decoded/dvp_raw/20260508T180106Z-dvp_raw_lp_160x144.json`: `LP` as DE timed out with `esp_err=263`.
- `captures/decoded/dvp_raw/20260508T180232Z-dvp_raw_spl_160x144.png`: `SPL` as DE, `160x144`, completed, checksum `2459761`, raw8 transitions `21800`.
- `captures/decoded/dvp_raw/20260508T180303Z-dvp_raw_spl_160x154.png`: `SPL` as DE, `160x154`, completed, checksum `2644300`, raw8 transitions `23317`.
- `captures/decoded/dvp_raw/20260508T180404Z-dvp_raw_spl_160x144.png`: `SPL` as DE with inverted PCLK, `160x144`, completed with the same checksum as non-inverted PCLK.
- `captures/decoded/dvp_raw/20260508T180435Z-dvp_raw_spl_160x154.png`: `SPL` as DE with inverted PCLK, `160x154`, completed, checksum `2657780`, raw8 transitions `23247`.

Visual result: generic DVP DMA is capturing full buffers, but output remains vertical-stripe/noise dominated. It is faster than CPU polling, but not yet using the GBC timing correctly.

ISP-DVP results:

- `captures/decoded/isp_dvp_raw/20260508T180906Z-isp_dvp_raw_hlp_despl_160x144.json`: `LP` as HSYNC and `SPL` as DE failed with `esp_err=259`.
- `captures/decoded/isp_dvp_raw/20260508T180955Z-isp_dvp_raw_hnc_despl_160x144.json`: `HSYNC=NC`, `DE=SPL` failed with `esp_err=259`.
- `captures/decoded/isp_dvp_raw/20260508T181202Z-isp_dvp_raw_hnc_despl_160x144.json`: retry after one-shot completion fix still failed with `esp_err=259`.

Interpretation: the simple DVP controller can DMA data when `SPL` is treated as DE, but that model does not reconstruct the screen. ISP-DVP exposes HSYNC separately, which is promising in principle, but the current RAW8 bypass configuration is not yet accepted by the driver/runtime state. Next work should instrument ISP-DVP failure stage or use a lower-level LCD_CAM/GDMA path that can DMA raw samples without enforcing camera frame semantics.
- Preserve `r3 c0` from the `5x5` tile split as the current best visual target and compare future captures against it.
- Use `host/live_red_viewer.py` for human-in-the-loop feedback while wiring and signal hypotheses change, but do not use its output as proof of frame timing.

2026-05-08: Added ISP-DVP failure-stage instrumentation and fixed two setup issues.

Observed stages:

- `esp_isp_enable` failed while `flags.bypass_isp=true`; ESP-IDF does not allow enabling a bypassed ISP processor in this path.
- Changing the processor to non-bypass `RAW8 -> RAW8` moved the failure to `esp_cam_ctlr_start`.
- Registering `on_get_new_trans` before start and providing the capture buffer through the callback allowed ISP-DVP to complete full-size transfers.

Completed RAW8 ISP-DVP captures were all zero despite `received_size == buffer_len`:

- `captures/decoded/isp_dvp_raw/20260508T183101Z-isp_dvp_raw_hlp_despl_160x144.*`: `HSYNC=LP`, `DE=SPL`, checksum `0`, transitions `0`.
- `captures/decoded/isp_dvp_raw/20260508T183132Z-isp_dvp_raw_hnc_despl_160x144.*`: `HSYNC=NC`, `DE=SPL`, checksum `0`, transitions `0`.
- DE-inverted and PCLK-inverted variants also completed as all-zero buffers.

2026-05-08: Added `ISP_DVP_CAPTURE_RGB565` and `--format rgb565` in `host/capture_isp_dvp_raw.py` to test the output format used by ESP-IDF's ISP-DVP examples (`RAW8` input, `RGB565` output). Build, flash, and smoke test passed. Capture result:

- `captures/decoded/isp_dvp_raw/20260508T183810Z-isp_dvp_rgb565_hlp_despl_160x144.*`: `HSYNC=LP`, `DE=SPL`, `RGB565`, `received_size=46080`, checksum `0`, transitions `0`.

Interpretation: the ISP-DVP path now starts and DMAs a complete buffer, but both `RAW8` and `RGB565` outputs are black. This rules out the simple "RAW8 output format is unsupported" explanation. The current blocker is likely ISP-DVP gating/configuration or a deeper mismatch between the ISP camera pipeline and the GBC LCD bus. The next capture implementation should investigate a lower-level LCD_CAM/GDMA raw sampler rather than continue tuning ISP color output.

2026-05-08: Added `DVP_CAPTURE_RAW_LEN` and `host/capture_dvp_raw.py --byte-count-eof` as a narrow LCD_CAM register experiment. This keeps the generic DVP LCD_CAM/GDMA setup but switches camera EOF from `VSYNC` to `CAM_REC_DATA_BYTELEN`, attempting to finish DMA by byte count rather than frame sync.

Implementation details:

- Uses the same `DCLK -> PCLK`, `SPS -> VSYNC`, and `SPL/LP -> DE` routing as `DVP_CAPTURE_RAW`.
- Sets `cam_vs_eof_en=false`.
- Sets `cam_rec_data_bytelen=buffer_len-1`.
- Applies the register writes after `esp_cam_ctlr_start()` because the high-level start path resets LCD_CAM.

Results:

- `captures/decoded/dvp_raw/20260508T184326Z-dvp_raw_len_spl_160x144.json`: timed out with `ESP_ERR_TIMEOUT`.
- `captures/decoded/dvp_raw/20260508T184455Z-dvp_raw_len_spl_160x144.json`: timed out after changing byte count from `N` to `N-1`.
- `captures/decoded/dvp_raw/20260508T184640Z-dvp_raw_len_spl_160x144.json`: timed out after moving the register writes after high-level start.
- `captures/decoded/dvp_raw/20260508T184721Z-dvp_raw_len_spl_160x144.json`: timed out with VSYNC inversion enabled.

Interpretation: the high-level DVP wrapper does not produce a DMA completion from the LCD_CAM byte-count EOF path under this setup. The next implementation should stop trying to steer the wrapper and instead own LCD_CAM/GDMA setup directly enough to inspect descriptor progress, EOF status, and partial received byte counts.

2026-05-08: Added a private LCD_CAM/GDMA sampler:

- Firmware module: `firmware/main/lcdcam_raw.c`
- Command: `LCDCAM_RAW_CAPTURE <SPL|LP> <timeout_ms> [vsync_invert] [de_invert] [pclk_invert] [byte_count_eof] [h_res] [v_res] [start_mode]`
- Host tool: `host/capture_lcdcam_raw.py`

This path directly allocates an AXI GDMA RX channel, connects it to `CAM0`, owns the DMA descriptors, routes the current `R2-R5/G2-G5` diagnostic byte into LCD_CAM, and reports `received_size`, descriptor count, completed descriptor count, EOF status, checksum, and transitions. It still uses ESP-IDF helper APIs for clock/pin initialization, but it no longer relies on `esp_cam_ctlr_receive()`.

First private sampler artifacts:

| Artifact prefix | Mode | Size | Result |
|---|---|---:|---|
| `captures/decoded/lcdcam_raw/20260508T185729Z-lcdcam_raw_spl_160x144` | VSYNC EOF | 160x144 | Completed, nonzero buffer, checksum `5603919`, transitions `2860`, descriptor accounting reported `129` received bytes and `1/6` completed descriptors |
| `captures/decoded/lcdcam_raw/20260508T185821Z-lcdcam_raw_spl_160x144` | Byte-count EOF | 160x144 | Timed out, but received `20460` bytes, exactly `5/6` DMA descriptors, checksum `5858728` |
| `captures/decoded/lcdcam_raw/20260508T185928Z-lcdcam_raw_spl_160x127` | Byte-count EOF | 160x127 | Completed `5/5` descriptors, all `0x00` |
| `captures/decoded/lcdcam_raw/20260508T190027Z-lcdcam_raw_spl_160x128` | Byte-count EOF | 160x128 | Completed `6/6` descriptors, all `0xff` |
| `captures/decoded/lcdcam_raw/20260508T190943Z-lcdcam_raw_spl_160x144` | VSYNC EOF, start after `SPS` rising then `SPL` falling | 160x144 | Completed with `start_trigger_seen=true`, nonzero buffer, checksum `5566114`, transitions `2920`, descriptor accounting reported `143` received bytes and `1/6` completed descriptors |
| `captures/decoded/lcdcam_raw/20260508T192046Z-lcdcam_raw_spl_160x144` | VSYNC EOF, start after `SPS` rising then `SPL` falling, LCD_CAM `VH+DE` mode with `LP` as HSYNC and `SPL` as DE | 160x144 | Completed with `start_trigger_seen=true`, nonzero buffer, checksum `5563924`, transitions `2939`, descriptor accounting reported `0` received bytes and `1/6` completed descriptors |
| `captures/decoded/lcdcam_raw/20260508T192953Z-lcdcam_raw_high_320x204` | Byte-count EOF, `DE` forced high, start after `SPS`, normal PCLK edge | 320x204 | Completed full fast stream: `65280` bytes, `16/16` descriptors, checksum `15136290`, transitions `1512` |
| `captures/decoded/lcdcam_raw/20260508T193122Z-lcdcam_raw_high_320x204` | Byte-count EOF, `DE` forced high, start after `SPS`, inverted PCLK edge | 320x204 | Completed full fast stream: `65280` bytes, `16/16` descriptors, checksum `15192724`, transitions `2874` |

Fresh marker-aware reference capture:

- `captures/decoded/rg_line_bursts/20260508T191242Z-rg_line_bursts_160x144.*`
- Settings: `SPS` rising frame sync, `SPL` falling line marker, `DCLK` rising sample edge, no marker skip, no DCLK delay.
- Result: 144 captured lines, 160 samples on every line, checksum `5318025`, transitions `1688`.
- Tile sheet: `captures/decoded/rg_line_bursts/20260508T191242Z-rg_line_bursts_160x144_tile_5x5_sheet.png`

Interpretation: private LCD_CAM/GDMA control is working and can report descriptor-level progress, but it is not yet assembling the GBC bus into coherent lines. The readable CPU-polled line-burst path and the private LCD_CAM path use the same physical data pins, the same diagnostic RG44 packing, and the same nominal 160x144 output geometry. The important difference is framing: the CPU-polled path explicitly waits for each `SPL` line marker and samples exactly 160 `DCLK` edges per line, while the private LCD_CAM path captures a continuous peripheral stream and the host slices it into rows. The `20260508T190943Z` start-after-`SPS`/`SPL` test still ended after only about one descriptor fragment, so start alignment alone does not solve the camera-style frame semantics mismatch. The `20260508T192046Z` `VH+DE` test also failed to produce coherent line framing, so treating `LP/SPL/SPS` directly as camera `HSYNC/DE/VSYNC` is not sufficient. For the "capture fast now, slow down later" approach, `DE=HIGH` is the current best mode: it captures a complete 65,280-byte stream at LCD_CAM/GDMA speed. These artifacts must be decoded offline by searching row width, phase, sync markers, and sample edge.

2026-05-08: Added first blue-enabled LCD_CAM diagnostic mode.

New raw data mode:

- Command suffix: `RGB332`
- Packing: bits `7..5 = R5,R4,R3`; bits `4..2 = G5,G4,G3`; bits `1..0 = B5,B4`.
- GPIO routing: `R5/R4/R3 -> GPIO13/14/15`, `G5/G4/G3 -> GPIO7/8/9`, `B5/B4 -> GPIO50/48`.

The existing `RG44` mode remains unchanged for reproducibility of `gbc_rg44_fast_v1`. `RGB332` is the first full-color visual diagnostic and intentionally uses only upper color bits.

2026-05-08: Reduced reliable LCD_CAM capture window from `320x204` to `192x145`.

Command shape:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/capture_lcdcam_raw.py --port /dev/cu.usbmodem14401 --timeout 30 --de HIGH --no-vsync-invert --no-de-invert --pclk-invert --byte-count-eof --width 192 --height 145 --capture-timeout-ms 2500 --start-mode 1 --no-vh-de-mode --data-mode RGB332
```

Decode:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/decode_lcdcam_fast.py captures/decoded/lcdcam_raw/20260508T212557Z-lcdcam_raw_high_192x145.bin --preset gbc_rgb332_fast_v1 --frame 0
```

Result: `192x145` produces a readable `160x144` boot-screen reconstruction with the established `161x145` stream model. Direct `161x145` programmed capture is still all zero, and current `VH+DE` line-gated attempts with `SPL/LP` do not yet produce useful source frames. The reduced reliable path is therefore `192x145` capture plus `161x145` source-period decode.

2026-05-08: Added firmware binary frame transport for lower-latency live viewing.

Firmware command:

```text
LCDCAM_RAW_CAPTURE_BIN <SPL|LP|HIGH> <timeout_ms_1_to_5000> [vsync_invert_0_or_1] [de_invert_0_or_1] [pclk_invert_0_or_1] [byte_count_eof_0_or_1] [h_res_1_to_320] [v_res_1_to_240] [start_mode_0_immediate_1_after_sps_2_after_sps_spl] [vh_de_mode_0_or_1] [RG44|RGB332]
```

Transport format:

1. A compact JSON header line with `binary_len`, checksum, descriptor counts, and capture metadata.
2. Exactly `binary_len` raw bytes.

Verified command:

```text
LCDCAM_RAW_CAPTURE_BIN HIGH 2500 0 0 1 1 192 145 1 0 RGB332
```

Observed result:

- `binary_len=27840`
- `received_size=27840`
- `checksum=6626214` on one test capture
- `raw8_transitions=747`
- `packing=rgb332_upper_bits`

The browser viewer now uses this binary firmware command by default and serves `/api/frame.bin` to the browser. With host crop enabled, the HTTP body is the `161x145` source-period slice (`23345` bytes), while ESP32-P4 still captures the reduced reliable `192x145` window.

2026-05-08: Added continuous host-side capture loop for live viewing.

Historical RGB332 live path:

```text
ESP32-P4 LCD_CAM capture 192x145
    -> firmware binary header + 27840 raw bytes over USB console
    -> Python background capture loop
    -> host crop to first 161x145 bytes
    -> browser polls latest completed frame
```

Measured performance:

- Firmware capture time reported in metadata: about `27..40 ms`.
- End-to-end Python capture loop: about `137..154 ms`.
- Effective continuous acquisition rate: about `6.9 fps`.

This proves the browser polling path is no longer the main limiter. The next capture-pipeline improvement must move from per-frame command/response toward a persistent firmware stream and a binary-safe USB transport.

2026-05-09: Added batched binary frame command on the existing USB console path.

Command:

```text
LCDCAM_RAW_STREAM_BIN <frame_count_1_to_64>
```

Historical behavior:

- Captures the proven `192x145` RGB332 LCD_CAM window.
- Emits one JSON header plus `27840` raw bytes per frame.
- Repeats for the requested frame count.

Viewer command:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/live_lcdcam_stream_viewer.py --port /dev/cu.usbmodem14401 --listen-port 8787 --pclk-invert --interval-ms 33 --capture-timeout-ms 2500 --width 192 --height 145 --data-mode RGB332 --firmware-binary --no-source-binary --continuous-capture --stream-batch-size 8 --host-crop --crop-offset 0 --crop-width 161 --crop-height 145
```

Measured live rate is now about `10 fps`. This is an improvement over the previous `~7 fps`, but it is still not the final real-time architecture because each frame still performs full capture setup/teardown inside `lcdcam_raw_capture()`.

2026-05-09: Updated live capture to use non-inverted PCLK after sparkle root-cause analysis.

Historical corrected RGB332 viewer command:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/live_lcdcam_stream_viewer.py --port /dev/cu.usbmodem14401 --listen-port 8788 --no-pclk-invert --interval-ms 33 --capture-timeout-ms 2500 --width 192 --height 145 --data-mode RGB332 --firmware-binary --no-source-binary --continuous-capture --stream-batch-size 8 --host-crop --crop-offset 0 --crop-width 161 --crop-height 145 --profile profiles/gbc_lcd.json
```

Reason: repeated frame analysis showed that inverted PCLK caused unstable `G5` samples around text edges. Non-inverted PCLK produced stable frames and removed the cyan sparkle.

2026-05-09: Promoted the live LCD_CAM viewer from a single-purpose frame canvas into a profile-backed signal-lab web UI.

The existing GBC live path remains compatible: `/api/frame.bin`, `Start`, `Stop`, `Single`, `Recover`, `Safe Idle`, RGB565 rendering, geometry controls, binary transport, and `GBC_SOURCE_FRAME_BIN` capture are the active capture path.

New browser structure:

- `Dashboard`: instrument status and primary safe controls.
- `Live View`: current decoded frame canvas.
- `Profile`: active target profile loaded from `profiles/gbc_lcd.json`.
- `Signals`: candidate signal roles and current wiring from the profile.
- `Logs`: safe probe commands and browser-side action log.

New HTTP endpoints:

- `/api/profile`
- `/api/probe-command?cmd=PING`
- `/api/probe-command?cmd=GET_VERSION`
- `/api/probe-command?cmd=GET_PINMAP`
- `/api/probe-command?cmd=EXPORT_STATS`

The probe-command endpoint is allowlisted so the browser UI can inspect the instrument without becoming an unrestricted firmware command console.

2026-05-09: Added an experimental full-color preservation path independent of the stable live viewer.

Firmware command:

```text
CAPTURE_RGB666_LINE_BURSTS <width> <height> <timeout_ms> [rising|falling] [SPL|LP] [skip_markers] [dclk_delay_edges] [marker_stride] [marker_phase] [stop_on_next_frame]
```

Host tool:

```sh
python host/capture_rgb666_line_bursts.py --port /dev/cu.usbmodem14401 --timeout 30
```

Purpose: preserve all currently connected color bits as three bytes per pixel (`R6`, `G6`, `B6`) for offline color validation while leaving the working LCD_CAM `RGB565` browser path unchanged.

Verified results after flashing:

- Tiny command sanity check: `CAPTURE_RGB666_LINE_BURSTS 16 4 1000 rising SPL 0 0 1 0 1` returned `captured_lines=4`, `line_sample_counts=[16,16,16,16]`, `frame_sync_seen=true`.
- One-frame bounded 160x144 attempt: `captures/decoded/rgb666_line_bursts/20260509T115056Z-rgb666_line_bursts_160x144.*`, `captured_lines=15`, `next_frame_seen=true`.
- Complete diagnostic 160x144 buffer: `captures/decoded/rgb666_line_bursts/20260509T115426Z-rgb666_line_bursts_160x144.*`, `captured_lines=144`, captured with `stop_on_next_frame=false`.

Interpretation: the RGB666 command proves the firmware can read and preserve all 18 color wires with explicit metadata, but CPU polling cannot acquire a full `160x144` RGB666 frame within one source frame. The complete PNG is useful for color-bit inspection only; real true-color live capture still needs a peripheral-backed or otherwise lower-overhead capture path.

2026-05-09: Added RGB666 diagnostic viewing to the browser workbench.

New browser controls in the `Live View` side panel:

- `Fast RGB565`: returns to the stable GBC source live stream.
- `RGB666 Single`: stops the fast stream and captures one CPU-polled RGB666 diagnostic buffer.
- `Slow RGB666 live`: repeats the RGB666 diagnostic capture at the configured browser interval.

New HTTP endpoint:

```text
/api/rgb666-frame.bin
```

Verification: the endpoint returned `69120` bytes for one `160x144x3` RGB666 buffer with metadata reporting `captured_lines=144`, `line_sample_counts` all `160`, and `pixel_format=RGB666`. This remains a diagnostic viewer because it uses `stop_on_next_frame=false` and may span source frames.

2026-05-09: Added the first hardware-backed higher-color capture mode, `RGB664`.

Why: CPU-polled RGB666 shows repeated low-resolution tiles because it cannot keep up with the source clock. The next step is preserving more color bits while staying on the LCD_CAM/GDMA capture path that already gives stable timing.

Packing:

```text
16-bit sample = R0-R5 + G0-G5 + B2-B5
```

Verification:

- Firmware build and flash passed.
- Small command test `LCDCAM_RAW_CAPTURE_BIN HIGH 2500 0 0 0 1 16 8 1 0 RGB664` returned `binary_len=256`, `bytes_per_sample=2`, `received_size=256`, `eof_seen=true`.
- Full capture command through `host/capture_lcdcam_raw.py` returned `received_size=55680` for `192x145x2`, all `14/14` descriptors complete, checksum `13694370`, transitions `4200`.
- Artifact: `captures/decoded/lcdcam_raw/20260509T153425Z-lcdcam_raw_high_192x145.png`.
- Browser workbench was started in `RGB664` mode at `http://127.0.0.1:8791/`; `/api/frame.bin` returned `46690` bytes, matching cropped `161x145x2`.

Interpretation: the ESP32-P4 LCD_CAM/GDMA path can capture 16-bit samples with the current GBC timing model. This is a major improvement over CPU-polled RGB666. It is not final RGB666 because `B0` and `B1` are not included in this first 16-bit packing.

2026-05-09: Added standard `RGB565` as a second hardware-backed 16-bit mode.

Packing:

```text
16-bit sample = B1-B5 + G0-G5 + R1-R5
```

This matches the normal RGB565 bit allocation: 5 red, 6 green, 5 blue. It drops `R0` and `B0`, making it a better practical display/upscaling format than RGB664 if the visual geometry is correct.

Verification:

- Firmware build and flash passed.
- Full capture command through `host/capture_lcdcam_raw.py` returned `received_size=55680` for `192x145x2`, all `14/14` descriptors complete, checksum `13303188`, transitions `4200`.
- Artifact: `captures/decoded/lcdcam_raw/20260509T154430Z-lcdcam_raw_high_192x145.png`.
- Browser workbench was started in `RGB565` mode at `http://127.0.0.1:8791/`; `/api/frame.bin` returned `46690` bytes, matching cropped `161x145x2`.

2026-05-09: Extended batched binary streaming to support `RGB565`.

Change:

- `LCDCAM_RAW_STREAM_BIN` now accepts an optional data mode argument: `RG44`, `RGB332`, `RGB664`, or `RGB565`.
- The browser workbench now passes its selected data mode into the batched stream command.

Verification:

- Firmware build and flash passed after stopping a stale viewer process that was holding `/dev/cu.usbmodem14401`.
- Browser started with `--data-mode RGB565 --stream-batch-size 8`.
- `/api/status` reported stable capture with no consecutive errors and about `6.3..6.7 fps`.

Interpretation: batching improves RGB565 over the previous per-frame command path, but each RGB565 frame is larger than the historical RGB332 diagnostic payload and the firmware still performs capture setup/teardown for every frame. The next FPS gain requires keeping LCD_CAM/GDMA configured across frames or moving to a dedicated binary USB stream.

2026-05-09: Added browser-assisted boot capture workflow.

Purpose: avoid the manual Stop/Safe Idle/Recover/Start sequence when power-cycling the target to observe the boot animation.

Browser controls:

- `Arm Boot Capture`
- `Stop Boot Capture`
- frame count, wait timeout, and probe interval settings

Workflow:

```text
Arm Boot Capture
    -> stop continuous capture
    -> SAFE_IDLE
    -> poll DCLK/SPS edge counts
    -> when source timing returns, capture N RGB565 frames
    -> save raw frames
    -> render PNG frames after recording
```

Verification: dry run with the source already on captured two RGB565 frames and wrote:

- `captures/experiments/20260509T161053Z-boot_capture_rgb565/raw/frame_0000.bin`
- `captures/experiments/20260509T161053Z-boot_capture_rgb565/frames/frame_0000.png`
- `captures/experiments/20260509T161053Z-boot_capture_rgb565/summary.json`

2026-05-11: Added PPA SRM benchmark to the normal lab firmware command set.

Purpose: measure ESP32-P4 PPA scale/rotate/mirror throughput without flashing the standalone PPA experiment app that caused manual recovery on this board.

Command:

```sh
PPA_SRM_BENCH [frame_count_1_to_1000]
```

Benchmark shape:

- Synthetic input, no GBC GPIO capture.
- `160x144 RGB565` source buffer.
- `320x288 RGB565` destination buffer.
- Compares blocking PPA SRM 2x scaling against CPU 2x scaling.
- Prints JSON records for `PPA_SRM_SCALE2X_BENCH` and `CPU_SCALE2X_BENCH`.

Verification so far:

- `./scripts/build_probe_firmware.sh` passed.
- `python3 -m py_compile host/collect_ppa_srm_bench.py` passed.
- Flashed normal lab firmware over native USB Serial/JTAG on `/dev/cu.usbmodem1432201`, then reset manually and observed the app protocol on `/dev/cu.usbmodem14401`.
- `PPA_SRM_BENCH 120` was collected with `host/collect_ppa_srm_bench.py`.

Result:

```json
{"command":"PPA_SRM_SCALE2X_BENCH","fps":149.715,"avg_us":6679.3,"target_rate_met":true,"error":"none"}
{"command":"CPU_SCALE2X_BENCH","fps":80.510,"avg_us":12420.8,"target_rate_met":true,"error":"none"}
```

Artifact:

- `captures/benchmarks/ppa_srm/20260511T201252Z-ppa-srm-bench/`

Interpretation: PPA SRM is about `1.86x` faster than the current CPU 2x scaler for synthetic `160x144 RGB565 -> 320x288 RGB565`. This proves PPA is useful for production scaling work. It does not solve the current SPI LCD bandwidth ceiling by itself.

Host evidence collection:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh
python host/collect_ppa_srm_bench.py \
  --port /dev/cu.wchusbserial5A470211841 \
  --frames 120 \
  --timeout-s 30 \
  --echo
```

Artifacts are written under `captures/benchmarks/ppa_srm/`.

## Production Throughput Notes

Current best stable production test:

```text
GBC LCD bus
  -> LCD_CAM/GDMA ring capture, 192x145 RGB565 window
  -> source alignment view, 161x145 stream with -4 linear pixel shift
  -> raw SPI master using SPI_CLK_SRC_SPLL at 40 MHz
  -> SPI LCD in RGB565 panel mode
```

Measured result on 2026-05-12:

- Stable live output: about `43-44.6 fps`.
- LCD draw time: about `13.26 ms`.
- Source capture interval in stable mode: about `19.95 ms`.
- No dropped frames or draw failures in the best run.

Important distinction:

- `esp_lcd` SPI IO currently cannot select `SPI_CLK_SRC_SPLL`; with the default ESP32-P4 SPI clock source, `40 MHz` is rejected by the SPI driver.
- Raw SPI can select `SPI_CLK_SRC_SPLL`, and the same LCD path then accepts `40 MHz`.
- RGB565 panel mode is required for this path; RGB666 expands every pixel to 3 SPI bytes and wastes bandwidth for this source.

Open issue:

- Direct `161x145` ring capture showed the desired `~16.8 ms` frame interval, but stopped after 7 frames and timed out. This suggests the remaining work is in continuous ring descriptor rearming/EOF handling, not in SPI output bandwidth.
