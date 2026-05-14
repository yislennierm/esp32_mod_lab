# Experiment Log

## 1. Objective

Maintain a chronological record of experiments, measurements, captures, failures, and conclusions.

This matters because reverse engineering must be reproducible and traceable.

## 2. Current Understanding

Current hypothesis: Phase 1 input-only firmware can communicate with the ESP32-P4 and observe selected GBC LCD timing/control lines without assigning capture pins.

Evidence: serial command smoke tests pass, firmware capture pin count remains zero, and input-only GPIO reads/edge counts have been recorded for the current temporary wiring.

Confidence level: high.

## 3. Unknowns

- Hardware revision under test.
- Probe equipment and settings.
- Exact LCD board/connector variant.
- Baseline voltage and timing measurements.

## 4. Experiment Results

2026-05-06: Created initial ESP-IDF Phase 1 firmware baseline. Capture pin count is intentionally zero until electrical measurements are documented.

2026-05-06: Configured ESP-IDF target `esp32p4` using build directory `firmware/build_esp32p4`. Initial sandboxed configure failed because macOS process enumeration was blocked; rerunning outside the sandbox succeeded.

2026-05-06: Built firmware successfully with ESP-IDF v5.5. Output binary: `firmware/build_esp32p4/gbc_p4_probe.bin`. Incremental rebuild completed cleanly after removing one zero-pin configuration warning.

2026-05-06: Flash attempts failed on both `/dev/cu.usbmodem5A470211841` and `/dev/cu.wchusbserial5A470211841` with `Failed to connect to ESP32-P4: No serial data received`. Board likely needs manual ROM download mode or different reset/boot wiring.

2026-05-06: Retried after board re-enumerated as `/dev/cu.usbmodem14201`. Flash succeeded. Detected chip: ESP32-P4 revision v1.0, USB mode USB-Serial/JTAG, MAC `30:ed:a0:e0:fc:c4`.

2026-05-06: Reconfigured firmware primary console to USB-Serial/JTAG using `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`, rebuilt, and reflashed successfully.

2026-05-06: Verified serial command interface on `/dev/cu.usbmodem14201`. `PING`, `GET_VERSION`, and `EXPORT_STATS` returned valid JSON. Diagnostics reported `capture_pin_count=0`, `dropped_sample_count=0`, `overflow_count=0`, and `sync_loss_count=0`.

2026-05-06: Added host CLI `host/gbc_probe.py` to automate serial command execution and smoke testing.

2026-05-06: Ran `python host/gbc_probe.py --port /dev/cu.usbmodem14201 smoke`. Result passed with `ok=true`, no errors, and `capture_pin_count=0`.

2026-05-06: Added `host/record_experiment.py` for reproducible experiment sessions. It writes metadata, notes, a Phase 1 voltage template, and optional smoke-test JSON.

2026-05-06: Created baseline experiment session at `captures/experiments/20260505T223950Z-phase1-baseline-smoke`. The session contains `metadata.json`, `notes.md`, `phase1_voltage_template.csv`, and `smoke_test.json`. Smoke test passed with `capture_pin_count=0`, `dropped_sample_count=0`, `overflow_count=0`, and `sync_loss_count=0`.

2026-05-06: Added firmware scaffolding for required command names while keeping capture disabled. `GET_PINMAP` reports zero configured pins; timing/capture/buffer commands report `no_capture_pins_configured`.

2026-05-06: Rebuilt and reflashed the Phase 1 command scaffolding firmware. Expanded smoke test passed on `/dev/cu.usbmodem14201`: `GET_PINMAP` returned zero pins, and `MEASURE_CLOCKS`, `CAPTURE_TIMING`, `CAPTURE_RAW`, `CAPTURE_FRAME`, `SET_TRIGGER`, and `DUMP_BUFFER` were all blocked with `no_capture_pins_configured`.

2026-05-06: Added `host/validate_phase1_measurements.py` to validate voltage measurement CSVs and produce pinmap proposal reports without changing firmware.

2026-05-06: Ran validator against the original baseline template at `captures/experiments/20260505T223950Z-phase1-baseline-smoke/phase1_voltage_template.csv`. Validation was blocked because the older template lacks `proposed_gpio`, and the report was written to `phase1_validation_report.md`.

2026-05-06: Created updated template session at `captures/experiments/20260505T224746Z-phase1-voltage-template-v2`. Validation passed with zero safe pinmap entries and warnings for all candidate digital signals remaining `unknown`.

2026-05-06: Added `docs/esp32p4_gpio_inventory.md` and firmware support for input-only `READ_GPIO 33`. GPIO33 is a test GPIO allowlist entry, not a GBC capture assignment.

2026-05-06: Built and flashed GPIO33 test firmware on `/dev/cu.usbmodem14424301`. `READ_GPIO 33` returned `ok=true`, `mode=input_only`, pulls disabled, and level `1`. `READ_GPIO 32` was rejected with `gpio_not_allowlisted`. Full smoke test still passed with `capture_pin_count=0`.

2026-05-06: Temporary timing-signal GPIO33 input-level test run with `READ_GPIO 33`. Result: GPIO33 level `1`, input-only, pull-up disabled, pull-down disabled. Later pinout correction identifies this as `SPS -> GPIO33`.

2026-05-06: Added input-only `COUNT_GPIO_EDGES 33 <duration_ms>` command to measure temporary timing-signal activity without enabling capture pin mapping.

2026-05-06: Ran `COUNT_GPIO_EDGES 33 1000` on temporary timing signal GPIO33. Result: rising_edges=4029, falling_edges=2622, total_edges=6651 over 1000 ms. GBC power state was not explicitly recorded at command time, so this is an activity observation only. Later pinout correction identifies this as `SPS -> GPIO33`.

2026-05-06: With GBC explicitly ON, temporary timing signal GPIO33 produced `COUNT_GPIO_EDGES 33 1000`: rising_edges=0, falling_edges=8656, falling_hz=8656.0. `COUNT_GPIO_EDGES 33 5000`: rising_edges=0, falling_edges=43297, falling_hz=8659.4. Sequential `READ_GPIO 33` returned level=0. Later pinout correction identifies this as `SPS -> GPIO33`; edge polarity/count method needs refinement because only falling edges were classified.

2026-05-07: Added official-doc-derived ESP32-P4 GPIO candidate tiers to `docs/esp32p4_gpio_inventory.md` to avoid USB-JTAG pins, strapping pins, and analog-role pins during progressive GBC bus mapping.

2026-05-07: User reported temporary wiring: GBC pin 36 `DCLK -> GPIO22`, GBC pin 35 `LP -> GPIO21`, and GBC pin 34 `PS -> GPIO20`. Added GPIO20-GPIO22 to input-only test allowlist.

2026-05-07: Built and flashed GPIO20/GPIO21/GPIO22 allowlist firmware on `/dev/cu.usbmodem14401`. With current GBC wiring, input-only tests returned: `PS/GPIO20` level=0 and 0 edges over 1000 ms; `LP/GPIO21` level=0 and 0 edges over 1000 ms; `DCLK/GPIO22` level=0 and 0 edges over 1000 ms. This may indicate inactive lines, wrong connector pins, changed GBC state, or wiring/contact issues; repeat with controlled state and compare against SPL/GPIO33.

2026-05-07: User provided corrected full display bus table. Current connected timing/control lines are: `DCLK -> GPIO22`, `LP -> GPIO21`, `PS -> GPIO20`, `SPL -> GPIO19`, `CLS -> GPIO32`, and `SPS -> GPIO33`. GPIO33 historical observations should be interpreted as SPS, not SPL. Added GPIO19 and GPIO32 to input-only allowlist.

2026-05-07: Built and flashed updated input-only allowlist firmware on `/dev/cu.usbmodem14401`. Build completed cleanly and flash succeeded on ESP32-P4 revision v1.0.

2026-05-07: Ran controlled 1000 ms input-only GPIO baseline across all currently connected timing/control lines with GBC ON:

| Signal | GPIO | Static Level | Rising Edges | Falling Edges | Interpretation |
|---|---:|---:|---:|---:|---|
| SPL | 19 | 0 | 0 | 8656 | Active timing signal near 8.656 kHz |
| PS | 20 | 1 | 9195 | 9196 | Active signal near 9.196 kHz, semantics unknown |
| LP | 21 | 0 | 0 | 9196 | Active line-rate candidate near 9.196 kHz |
| DCLK | 22 | 1 | 46003 | 57970 | Activity detected; not a valid MHz frequency measurement |
| CLS | 32 | 1 | 9196 | 9195 | Active signal near 9.196 kHz |
| SPS | 33 | 1 | 60 | 60 | Strong frame-marker candidate |

The edge-count command uses GPIO interrupts and classifies polarity by reading the level in the ISR. It is useful as a coarse activity detector for slower signals, but not sufficient for DCLK frequency measurement or pulse-width/polarity conclusions.

2026-05-07: Added PCNT-backed firmware command `MEASURE_DCLK <gpio> <duration_ms>` using ESP-IDF `driver/pulse_cnt.h`. Build completed cleanly and flash succeeded on `/dev/cu.usbmodem14401`.

2026-05-07: Ran `MEASURE_DCLK 22` with multiple durations. Results: 100 ms = 138299 rising edges / 1.382990 MHz; 500 ms = 698096 rising edges / 1.396192 MHz; 1000 ms = 1393728 rising edges / 1.393728 MHz.

2026-05-07: Ran PCNT rising-edge measurements across all currently connected timing/control lines for comparison:

| Signal | GPIO | Duration | Rising Edges | Rising Edge Hz |
|---|---:|---:|---:|---:|
| SPL | 19 | 1000 ms | 8659 | 8659 |
| PS | 20 | 1000 ms | 9197 | 9197 |
| LP | 21 | 1000 ms | 9197 | 9197 |
| DCLK | 22 | 1000 ms | 1395226 | 1395226 |
| CLS | 32 | 1000 ms | 9197 | 9197 |
| SPS | 33 | 1000 ms | 60 | 60 |

Conclusion: PCNT confirms GPIO22 is the fastest connected line but measures about 1.395 MHz, not the expected 6-8 MHz. This should be treated as a discovery requiring independent verification, not silently forced into the previous hypothesis.

2026-05-07: Added `CAPTURE_TIMING_EDGES <duration_ms>` firmware command for timestamping `SPL`, `PS`, `LP`, `CLS`, and `SPS` edges into an 8192-event buffer. DCLK is excluded from this ISR timestamp path. Built and flashed successfully on `/dev/cu.usbmodem14401`.

2026-05-07: Added host analyzer `host/analyze_timing_edges.py`. It captures timing edges, saves raw JSON, exports CSV, writes a summary JSON, and prints per-signal counts/rates plus SPS rising intervals.

2026-05-07: Ran `python host/analyze_timing_edges.py --port /dev/cu.usbmodem14401 --timeout 15 --duration-ms 100`. Result: `ok=true`, duration 100 ms, 5456 events, overflow 0. Artifacts:

- `captures/decoded/timing_edges/20260507T214244Z-timing_edges_100ms.json`
- `captures/decoded/timing_edges/20260507T214244Z-timing_edges_100ms.csv`
- `captures/decoded/timing_edges/20260507T214244Z-timing_edges_100ms_summary.json`

Analyzer summary:

| Signal | Events | Rate | Level 0 Edges | Level 1 Edges | Mean Interval |
|---|---:|---:|---:|---:|---:|
| CLS | 1833 | 18.330 kHz | 917 | 916 | 54.36 us |
| LP | 916 | 9.160 kHz | 916 | 0 | 108.72 us |
| PS | 1833 | 18.330 kHz | 916 | 917 | 54.36 us |
| SPL | 862 | 8.620 kHz | 862 | 0 | 115.54 us |
| SPS | 12 | 120 Hz | 6 | 6 | 7629.91 us across all SPS edges |

SPS rising intervals: `16743`, `16745`, `16743`, `16740`, `16743` us. Mean SPS rising interval: 16742.8 us, about 59.73 Hz.

2026-05-07: Added `host/analyze_timing_relationships.py` to analyze saved timing-edge JSON files by SPS rising-to-rising frame windows. Running it against `captures/decoded/timing_edges/20260507T214244Z-timing_edges_100ms.json` produced:

- Complete SPS-to-SPS frames: 5
- Mean frame duration: 16742.8 us
- Mean LP count per frame: 154.0
- Mean SPL count per frame: 145.0
- First LP after SPS rising: 3-6 us
- First SPL after SPS rising: 451-453 us
- `LP -> SPL` next-edge offset: mean 13.94 us, min 11 us, max 18 us

Artifacts:

- `captures/decoded/timing_relationships/20260507T214244Z-timing_relationships_100ms.json`
- `captures/decoded/timing_relationships/20260507T214244Z-timing_relationships_100ms.md`

Conclusion: the current capture strongly supports `SPS` as frame sync. It also shows stable per-frame counts of `LP=154` and `SPL=145`, which are now key hypotheses for line/visible-region reconstruction.

2026-05-07: Added `host/capture_timing_session.py` for repeatable timing sessions with optional manual trigger prompts. Dry run command:

`python host/capture_timing_session.py --port /dev/cu.usbmodem14401 --timeout 15 --duration-ms 100 --repeat 2 --label steady_state_test`

Result: both runs completed with `overflow=0`, `frames=5`, `LP/frame=154.0`, and `SPL/frame=145.0`. Artifacts:

- `captures/experiments/timing_sessions/20260507T215223Z-steady_state_test/session_summary.json`
- `captures/experiments/timing_sessions/20260507T215223Z-steady_state_test/session_report.md`

2026-05-07: Ran manual-trigger session `boot_power_on` with `--repeat 3`, `--duration-ms 100`, and `--pre-delay-ms 250`. User coordinated GBC power state manually. Results:

| Run | GBC State / Intent | Events | Complete Frames | LP/frame | SPL/frame | Interpretation |
|---:|---|---:|---:|---:|---:|---|
| 1 | off-to-on attempt | 0 | 0 |  |  | Capture window was too early or GBC was still off |
| 2 | off-to-on attempt | 0 | 0 |  |  | Capture window was too early or GBC was still off |
| 3 | already on control | 5460 | 5 | 154.0 | 145.0 | LCD bus captured normally |

Artifacts:

- `captures/experiments/timing_sessions/20260507T215311Z-boot_power_on/session_summary.json`
- `captures/experiments/timing_sessions/20260507T215311Z-boot_power_on/session_report.md`

Conclusion: firmware, wiring, and host session tooling are functional. The 250 ms manual boot delay is too short or too imprecise for the initial active LCD window. Repeat boot sessions with longer delays such as 1000 ms and 2000 ms.

2026-05-08: User connected all six red data bits:

| GBC Pin | Signal | ESP32-P4 GPIO |
|---:|---|---:|
| 19 | R5 | 13 |
| 18 | R4 | 14 |
| 17 | R3 | 15 |
| 16 | R2 | 16 |
| 15 | R1 | 17 |
| 14 | R0 | 18 |

Added GPIO13-GPIO18 to the input-only test allowlist. These are temporary red data test inputs; `capture_pin_count` remains zero.

2026-05-08: Built and flashed red data input-only allowlist firmware on `/dev/cu.usbmodem14401`. Ran 1000 ms `READ_GPIO` and `COUNT_GPIO_EDGES` tests:

| Signal | GPIO | Static Level | Rising Edges | Falling Edges | Total Edges | Interpretation |
|---|---:|---:|---:|---:|---:|---|
| R5 | 13 | 1 | 15964 | 5981 | 21945 | Active red data line |
| R4 | 14 | 1 | 15904 | 6138 | 22042 | Active red data line |
| R3 | 15 | 1 | 15742 | 6220 | 21962 | Active red data line |
| R2 | 16 | 1 | 15639 | 6490 | 22129 | Active red data line |
| R1 | 17 | 1 | 15625 | 6585 | 22210 | Active red data line |
| R0 | 18 | 1 | 0 | 0 | 0 | Static high during this test; may be content-dependent, least significant bit behavior, or wiring issue |

The GPIO ISR edge counts are activity indicators only and are not pixel-rate captures. R5-R1 activity confirms the red bus is observable as digital input under current conditions.

2026-05-08: Extended `CAPTURE_TIMING_EDGES` so each timing event also snapshots `red6`, packed as `R0=bit0` through `R5=bit5`. Built, flashed, and ran a 100 ms red timing-edge capture after fixing one startup timestamp underflow guard.

Artifacts:

- `captures/decoded/red_timing_edges/20260507T221639Z-timing_edges_100ms.json`
- `captures/decoded/red_timing_edges/20260507T221639Z-timing_edges_100ms.csv`
- `captures/decoded/red_timing_edges/20260507T221639Z-timing_edges_100ms_summary.json`

Result: `ok=true`, duration 100 ms, 5424 events, overflow 0. Red sample distribution across all timing edges:

| red6 value | Count | Notes |
|---:|---:|---|
| 63 (`0x3f`) | 5328 | All red bits high |
| 7 (`0x07`) | 54 | R2-R5 low, R0-R1 high |
| 15 (`0x0f`) | 27 | R4-R5 low, R0-R3 high |
| 31 (`0x1f`) | 14 | R5 low, R0-R4 high |
| 3 (`0x03`) | 1 | R2-R5 low, R0-R1 high |

Red variation appeared only on `SPL` events in this capture. `CLS`, `LP`, `PS`, and `SPS` events all observed `red6=0x3f`. This suggests SPL is close to meaningful red data timing, but it is not yet pixel-rate sampling. R0 and R1 remained high in all timing-edge snapshots.

2026-05-08: Smoke test passed after red timing snapshot firmware flash. `capture_pin_count` remains zero and Phase 1 capture commands remain blocked.

2026-05-09: Added explicit software isolation for the connected LCD bus GPIOs after user observed likely ESP32-P4 back-powering into an unpowered GBC. New firmware command `ELECTRICAL_ISOLATE` and alias `SAFE_ISOLATE` detach LCD_CAM inputs and configure currently connected capture GPIOs as disabled pads with no internal pulls. `SAFE_IDLE` intentionally remains the less disruptive floating-input state. Browser Stop now requests `ELECTRICAL_ISOLATE` so target power-cycle tests can stop capture and disable ESP32-P4 pads before the GBC is switched off or on.

2026-05-09: Audited firmware GPIO configuration for unintended pull-ups. No application code intentionally enables pull-ups on the connected GBC LCD capture pins. Updated startup so `app_main()` immediately enters `ELECTRICAL_ISOLATE` before initializing the command loop, and updated older DVP probe capture paths to return to `ELECTRICAL_ISOLATE` after success or failure. Built and flashed successfully on `/dev/cu.usbmodem14401`; verification command returned `mode=lcdcam_detached_gpio_disabled_no_pulls`.

2026-05-09: User moved `CLS` from ESP32-P4 GPIO32 to GPIO3 after GPIO32 appeared to be the line causing or contributing to the backfeed/power issue. Updated firmware allowlists, timing-edge signal mapping, USB metadata, LCD bus profile, browser power monitor, and software isolation GPIO lists from GPIO32 to GPIO3. GPIO32 is no longer treated as a connected LCD bus pin by the current firmware.

2026-05-08: Added exploratory `CAPTURE_RED_DCLK <sample_count> <timeout_ms>` firmware command and `host/capture_red_dclk.py`. The command waits for an `SPL` falling edge and then polls for `DCLK` rising edges, snapshotting red6 values. This is a polling experiment, not guaranteed pixel-rate capture.

Ran:

`python host/capture_red_dclk.py --port /dev/cu.usbmodem14401 --timeout 10 --samples 512 --capture-timeout-ms 100`

Result: `ok=true`, trigger seen, no timeout, 512 samples. Artifacts:

- `captures/decoded/red_dclk/20260507T222037Z-red_dclk_512samples.json`
- `captures/decoded/red_dclk/20260507T222037Z-red_dclk_512samples.csv`
- `captures/decoded/red_dclk/20260507T222037Z-red_dclk_512samples_summary.json`

Summary:

| Metric | Value |
|---|---:|
| Unique red values | 16 |
| Red transitions | 130 |
| Sample span | 7414 us |
| Minimum sample gap | 4 us |
| Maximum sample gap | 82 us |
| Mean sample gap | 14.51 us |

Most common red values: `0x3f` count 399, `0x01` count 20, `0x03` count 15, `0x0f` count 14, `0x31` count 14, `0x21` count 12. The polling loop clearly observes changing red data, but it misses most DCLK edges because observed sample gaps are much larger than the measured DCLK period. A peripheral/DMA capture path is required for reliable pixel-rate sampling.

2026-05-08: Smoke test passed after DCLK-windowed red polling firmware flash.

2026-05-08: Added ESP-IDF generic DVP controller allocation probe command `DVP_PROBE_ALLOC`. This command links `esp_driver_cam`, allocates a DVP controller for `RAW8` `160x144`, asks the driver for the frame buffer length, then deletes the controller. It intentionally uses `pin_dont_init=true`, so no GPIOs are routed and no capture is started.

First run after adding the command returned `esp_err=257` (`ESP_ERR_NO_MEM`). Local ESP-IDF source inspection showed the DVP DMA path allocates descriptors from PSRAM-capable DMA memory. The project had `CONFIG_SPIRAM` disabled.

Enabled PSRAM in `firmware/sdkconfig.defaults` and `firmware/sdkconfig`:

- `CONFIG_SPIRAM=y`
- `CONFIG_SPIRAM_MODE_HEX=y`
- `CONFIG_SPIRAM_SPEED_200M=y`

Rebuilt and flashed successfully. Then ran:

`python host/gbc_probe.py --port /dev/cu.usbmodem14401 --timeout 5 command "DVP_PROBE_ALLOC"`

Result:

| Field | Value |
|---|---:|
| ok | true |
| controller_count | 1 |
| max_data_width | 16 |
| configured_width | 8 |
| color | RAW8 |
| h_res | 160 |
| v_res | 144 |
| frame_buffer_len | 23040 |
| backup_buffer_disabled | true |

Smoke test also passed after the PSRAM-enabled flash. Existing Phase 1 capture commands remain blocked with `capture_pin_count=0`.

2026-05-08: Added `DVP_CAPTURE_RAW <SPL|LP> <timeout_ms> [vsync_invert] [de_invert]` and `host/capture_dvp_raw.py`. The first implementation crashed because the generic DVP driver interprets the `on_get_new_trans` callback return value as "buffer supplied". Fixed the callback to return `true` after assigning the transaction buffer.

DVP polarity tests:

| DE Source | VSYNC Invert | DE Invert | Result |
|---|---:|---:|---|
| SPL | 1 | 0 | timeout |
| LP | 1 | 0 | timeout |
| SPL | 1 | 1 | timeout |
| LP | 1 | 1 | timeout |
| SPL | 0 | 0 | success |

Successful command:

`python host/capture_dvp_raw.py --port /dev/cu.usbmodem14401 --timeout 30 --de SPL --capture-timeout-ms 1500 --no-vsync-invert --no-de-invert`

Artifacts:

- `captures/decoded/dvp_raw/20260507T225520Z-dvp_raw_spl.json`
- `captures/decoded/dvp_raw/20260507T225520Z-dvp_raw_spl.bin`
- `captures/decoded/dvp_raw/20260507T225520Z-dvp_raw_spl.png`
- `captures/decoded/dvp_raw/20260507T225520Z-dvp_raw_spl_summary.json`

Result: `160x144` PNG, checksum `2448467`, lower-six-bit transitions `21196`. This is the first frame-shaped peripheral-backed capture, but it is still red-only and uses temporary placeholder bits for RAW8 bits 6-7.

2026-05-08: Added `host/render_dvp_raw.py` and generated an inverted render from the same raw DVP buffer to test whether the LCD bus should be interpreted as active-low for visible intensity.

Artifacts:

- Normal render: `captures/decoded/dvp_raw/20260507T225520Z-dvp_raw_spl.png`
- Inverted render: `captures/decoded/dvp_raw/20260507T225520Z-dvp_raw_spl_inverted.png`

This is a rendering-polarity experiment only. The raw capture data is unchanged.

2026-05-08: User corrected green bus wiring to ESP32-P4 GPIO range `12:7`. Updated current mapping to `G0 -> GPIO12`, `G1 -> GPIO11`, `G2 -> GPIO10`, `G3 -> GPIO9`, `G4 -> GPIO8`, and `G5 -> GPIO7`. Added GPIO12 to the input-only allowlist.

2026-05-08: Updated the experimental DVP RAW8 capture mapping to include red and green upper bits in one frame: `bit0=R2/GPIO16`, `bit1=R3/GPIO15`, `bit2=R4/GPIO14`, `bit3=R5/GPIO13`, `bit4=G2/GPIO10`, `bit5=G3/GPIO9`, `bit6=G4/GPIO8`, and `bit7=G5/GPIO7`. The host renderer and live browser viewer now interpret this as a red/green `rg44_upper_bits` diagnostic image.

Built and flashed the red/green RAW8 diagnostic firmware to `/dev/cu.usbmodem14401`. Captured one saved DVP frame with `SPS` as VSYNC, `SPL` as DE, non-inverted VSYNC/DE, and non-inverted DCLK/PCLK:

- `captures/decoded/dvp_raw/20260508T151840Z-dvp_raw_spl.json`
- `captures/decoded/dvp_raw/20260508T151840Z-dvp_raw_spl.bin`
- `captures/decoded/dvp_raw/20260508T151840Z-dvp_raw_spl.png`
- `captures/decoded/dvp_raw/20260508T151840Z-dvp_raw_spl_summary.json`

Result: `160x144`, checksum `2449667`, `raw8_transitions=21881`. Started `host/live_dvp_viewer.py` at `http://127.0.0.1:8766/` using the same capture polarity.

User observed a consistent but misaligned red/green bar pattern in the browser viewer. Stopped the viewer and captured four DVP timing variants:

| DE | PCLK invert | Artifact prefix | Result |
|---|---:|---|---|
| SPL | false | `captures/decoded/dvp_raw/20260508T152415Z-dvp_raw_spl` | checksum `2474563`, `raw8_transitions=21746` |
| SPL | true | `captures/decoded/dvp_raw/20260508T152454Z-dvp_raw_spl` | checksum `2474563`, `raw8_transitions=21746` |
| LP | false | `captures/decoded/dvp_raw/20260508T152543Z-dvp_raw_lp` | checksum `2474563`, `raw8_transitions=21746` |
| LP | true | `captures/decoded/dvp_raw/20260508T152628Z-dvp_raw_lp` | checksum `2474563`, `raw8_transitions=21746` |

Generated a red/green interpretation contact sheet:

- `captures/decoded/dvp_raw/20260508T152415Z-dvp_raw_spl_variants.png`

The identical checksums across DE and PCLK-invert variants suggest this misalignment is not explained by a simple sample-edge toggle or LP/SPL gate swap in the current generic DVP setup. It may indicate the driver is not using those signals as expected, the polarity override is ineffective for this path, or the captured frame needs a different row/stride interpretation.

Green input-only activity check:

| Signal | GPIO | Rising edges | Falling edges | Total edges | Notes |
|---|---:|---:|---:|---:|---|
| G5 | 7 | 15772 | 6271 | 22043 | Active |
| G4 | 8 | 15974 | 6093 | 22067 | Active |
| G3 | 9 | 15933 | 6157 | 22090 | Active |
| G2 | 10 | 15909 | 6203 | 22112 | Active |
| G1 | 11 | 15788 | 6372 | 22160 | Active |
| G0 | 12 | 0 | 0 | 0 | Static in this screen state |

Added `host/render_dvp_stride_scan.py` and generated offline row-slicing hypothesis sheets from `20260508T152415Z-dvp_raw_spl.bin`:

- `captures/decoded/dvp_raw/20260508T152415Z-dvp_raw_spl_stride_scan.png`
- `captures/decoded/dvp_raw/20260508T152415Z-dvp_raw_spl_stride_scan_narrow.png`

The sheets did not reveal a recognizable boot-logo image. Some stride hypotheses produce diagonal drift and some produce repeated vertical separators, which supports a stable periodic byte stream but does not solve frame alignment. Local ESP-IDF source review found that `esp_cam_ctlr_dvp_init()` connects VSYNC with inversion enabled by default, while DE and PCLK are connected non-inverted. ESP-IDF's ISP-DVP driver has explicit polarity flags and should be the next capture-path experiment if generic DVP remains ambiguous.

2026-05-08: User reported the GBC power instability happened again while the live DVP browser viewer was running. Stopped and force-killed the stale live viewer process on port `8766`. This confirms the power issue is repeatable enough to block more live DVP capture until we isolate direct GPIO loading/back-powering versus capture-peripheral behavior.

2026-05-08: User clarified the expected no-cartridge post-boot target: after the logo animation, the screen should contain a dark Nintendo-logo region in the lower middle. Ran one single-frame DVP capture only, with live viewer stopped:

- `captures/decoded/dvp_raw/20260508T154306Z-dvp_raw_spl.json`
- `captures/decoded/dvp_raw/20260508T154306Z-dvp_raw_spl.bin`
- `captures/decoded/dvp_raw/20260508T154306Z-dvp_raw_spl.png`
- `captures/decoded/dvp_raw/20260508T154306Z-dvp_raw_spl_summary.json`
- `captures/decoded/dvp_raw/20260508T154306Z-dvp_raw_spl_inverted.png`
- `captures/decoded/dvp_raw/20260508T154306Z-dvp_raw_spl_stride_scan.png`
- `captures/decoded/dvp_raw/20260508T154306Z-dvp_raw_spl_variants.png`

Result: `160x144`, checksum `2644326`, `raw8_transitions=20972`. The render still shows a consistent vertical-bar structure and no obvious dark lower-middle Nintendo-logo region. This further supports that the current generic DVP capture configuration is not reconstructing visible pixels correctly.

Added `host/postprocess_dvp_rg44.py` and generated post-capture shift/skew contact sheets from the same raw capture:

- `captures/decoded/dvp_raw/20260508T154306Z-dvp_raw_spl_postprocess_shift_sheet.png`
- `captures/decoded/dvp_raw/20260508T154306Z-dvp_raw_spl_postprocess_shift_sheet_inverted.png`

The tool confirms we can move the captured data globally and per line after capture, but these shift hypotheses still did not reveal a lower-middle Nintendo-logo-like dark region. This suggests the current byte stream is not just shifted; the capture path is likely sampling the wrong phase/window or the DVP sync model is mismatched to the GBC LCD bus.

Added `host/debug_dvp_capture_viewer.py` and started it for `captures/decoded/dvp_raw/20260508T154306Z-dvp_raw_spl.bin` at `http://127.0.0.1:8767/`. This is an offline viewer with sliders and does not communicate with the ESP32-P4.

2026-05-08: Reviewed open FPGA Game Boy references:

- `MiSTer-devel/Gameboy_MiSTer` `rtl/lcd.v`
- `MiSTer-devel/Gameboy_MiSTer` `rtl/video.v`
- `zephray/VerilogBoy` `rtl/ppu.v`

Findings: FPGA cores model internal PPU timing as `456` dots per line and `154` lines per frame, with `160x144` visible output. MiSTer has a separate `lcd_clkena` pixel-valid signal and VerilogBoy explicitly separates pixel clock from valid output. Comparing this to our measurements (`DCLK ~= 1.395MHz`, `LP ~= 9198Hz`) gives about `152` DCLK pulses per LP period. This suggests the GBC LCD flex bus likely exposes a gated visible-transfer clock rather than the internal full dot clock. Next experiment should count DCLK pulses per LP/SPL interval directly.

2026-05-08: Added and flashed `CAPTURE_LINE_CLOCKS`. The first polling implementation showed LP deltas in multiples of `161`, proving the base interval but also proving polling missed some line-marker pulses. Reworked the command to use LP/SPL GPIO interrupts and PCNT DCLK snapshots from ISR context, then rebuilt and flashed successfully.

Line-clock artifacts:

- Polling LP falling proof-of-base: `captures/decoded/line_clocks/20260508T161247Z-line_clocks_lp_falling.json`
- Polling SPL falling proof-of-base: `captures/decoded/line_clocks/20260508T161328Z-line_clocks_spl_falling.json`
- ISR LP falling: `captures/decoded/line_clocks/20260508T161502Z-line_clocks_lp_falling.json`
- ISR SPL falling: `captures/decoded/line_clocks/20260508T161523Z-line_clocks_spl_falling.json`

Result: ISR `SPL` falling captured 180 samples with DCLK deltas `160`, `161`, or `162` for 179 samples and one startup partial value of `11`. ISR `LP` falling captured 168 intervals at `161` and 12 zero-delta observations. Conclusion: `SPL` falling is the stronger visible-line burst marker candidate, and the next capture architecture should assemble line bursts rather than keep forcing the generic DVP frame model.

2026-05-08: Implemented `CAPTURE_RG_LINE_BURSTS` and `host/capture_rg_line_bursts.py`.

Iteration results:

| Implementation | Artifact prefix | Line sample result | Conclusion |
|---|---|---|---|
| `gpio_get_level()` inside pixel loop | `captures/decoded/rg_line_bursts/20260508T162136Z-rg_line_bursts_160x144` | `18..22` samples per line | Too slow |
| Direct `GPIO.in` reads plus timer check | `captures/decoded/rg_line_bursts/20260508T162354Z-rg_line_bursts_160x144` | `96` or `112` samples per line | Better, but timer check still too expensive |
| Direct `GPIO.in` reads with fixed loop budget | `captures/decoded/rg_line_bursts/20260508T162521Z-rg_line_bursts_160x144` | `160` samples on all 144 lines | First complete line-burst frame-shaped capture |

The final PNG is not yet a recognizable clean GBC screen, but it is no longer the generic DVP vertical-bar failure. Capture geometry is now under our control and complete for the currently connected red/green upper bits.

2026-05-08: User observed the complete line-burst PNG appears to contain many repeated screen-like fragments, roughly like repeated smaller screens. Added `host/postprocess_rg_line_bursts.py` and generated decimation/tile/phase sheets from the same raw capture. Tile views show recognizable fragments, but 5-phase decimation does not produce a clean single screen. This supports a repeated/interleaved subfield hypothesis rather than a simple over-sampling factor.

2026-05-08: Added sample-edge argument to `CAPTURE_RG_LINE_BURSTS` and captured falling-edge data:

- `captures/decoded/rg_line_bursts/20260508T163722Z-rg_line_bursts_160x144.json`
- `captures/decoded/rg_line_bursts/20260508T163722Z-rg_line_bursts_160x144.png`
- `captures/decoded/rg_line_bursts/20260508T163722Z-rg_line_bursts_160x144_inverted.png`

Falling edge produced complete geometry (`160` samples on all `144` lines) but did not remove the repeated pattern. Conclusion: keep both edge options, but move next to line/subfield selection.

2026-05-08: User identified `20260508T162521Z-rg_line_bursts_160x144_tile_r3_c0_scaled.png` as the best visual decode so far: lower resolution but with the dark text in the right position and no obvious repetition. Generated enlarged normal/inverted versions:

- `captures/decoded/rg_line_bursts/20260508T162521Z-rg_line_bursts_160x144_best_tile_r3_c0_normal_320x288.png`
- `captures/decoded/rg_line_bursts/20260508T162521Z-rg_line_bursts_160x144_best_tile_r3_c0_inverted_320x288.png`

Also tested a 5x5 untile/interleave reconstruction, but it did not produce a clean high-resolution frame. Conclusion: the good `r3 c0` tile should be treated as a coherent low-resolution subfield, not automatically as one phase plane of a full-resolution image.

2026-05-08: User challenged the interpretation of repeated mini-screen artifacts, noting that they may mean we are misusing a timing signal rather than seeing fields. Revised the working interpretation accordingly and added marker/skip testing to `CAPTURE_RG_LINE_BURSTS`.

New LP-keyed capture artifacts:

- `captures/decoded/rg_line_bursts/20260508T173537Z-rg_line_bursts_160x144.png`: `LP`, `skip_markers=0`, rising edge, complete `160x144`, checksum `5346840`
- `captures/decoded/rg_line_bursts/20260508T173452Z-rg_line_bursts_160x144.png`: `LP`, `skip_markers=5`, rising edge, complete `160x144`, checksum `5386365`
- `captures/decoded/rg_line_bursts/20260508T173752Z-rg_line_bursts_160x144.png`: `LP`, `skip_markers=10`, rising edge, complete `160x144`, checksum `5369280`

Generated tile sheet for the `LP`, `skip_markers=10` capture:

- `captures/decoded/rg_line_bursts/20260508T173752Z-rg_line_bursts_tile_5x5_sheet_large.png`

Conclusion: marker and marker skip change image placement coherently, so the capture contains real image data but the row/window timing model is not solved. Do not prioritize adding blue lines until `LP`/`SPL`, marker skip, and DCLK phase delay are systematically tested.

2026-05-08: Added and flashed capture-window controls:

- `dclk_delay_edges`: skip a fixed number of DCLK sample edges after each selected marker before storing pixels.
- `marker_stride`: capture every Nth marker.
- `marker_phase`: select which modulo phase of `marker_stride` to store.

Build and flash succeeded. Smoke test after experiments returned `ok=true`.

Capture results using `LP`, `skip_markers=10`, rising edge:

| Artifact prefix | DCLK Delay | Stride | Phase | Checksum | Transitions | Result |
|---|---:|---:|---:|---:|---:|---|
| `20260508T174502Z-rg_line_bursts_160x144` | 0 | 1 | 0 | 5357805 | 1558 | Baseline after firmware update |
| `20260508T174536Z-rg_line_bursts_160x144` | 8 | 1 | 0 | 5390700 | 1482 | Window moved; geometry still complete |
| `20260508T174616Z-rg_line_bursts_160x144` | 32 | 1 | 0 | 5365455 | 1498 | Larger delay still complete |
| `20260508T174651Z-rg_line_bursts_160x144` | 0 | 5 | 0 | 5394015 | 1450 | Phase texture changed |
| `20260508T174818Z-rg_line_bursts_160x144` | 0 | 5 | 1 | 5366985 | 1468 | Phase texture changed |
| `20260508T174839Z-rg_line_bursts_160x144` | 0 | 5 | 2 | 5340720 | 1474 | Phase texture changed |
| `20260508T174900Z-rg_line_bursts_160x144` | 0 | 5 | 3 | 5385090 | 1498 | Phase texture changed |
| `20260508T174921Z-rg_line_bursts_160x144` | 0 | 5 | 4 | 5378205 | 1406 | Phase texture changed |

Conclusion: the new controls work, but stride-5 phase selection alone does not recover a clean frame. The user's "25-ish low-resolution frames" observation remains useful; the next experiment should sweep narrower capture widths and DCLK delays because we may be forcing smaller transfer chunks into a 160-pixel row model.

2026-05-08: Added `--single-frame` / `stop_on_next_frame` mode after identifying that full-height stride captures can span multiple physical frames.

Verification captures:

| Artifact prefix | Marker | Skip | Stride | Phase | Single Frame | Captured Lines | Next Frame Seen | Conclusion |
|---|---|---:|---:|---:|---|---:|---|---|
| `20260508T175615Z-rg_line_bursts_160x144` | LP | 10 | 1 | 0 | yes | 144 | no | Baseline fills 144 rows before next detected frame edge |
| `20260508T175636Z-rg_line_bursts_160x144` | LP | 10 | 5 | 4 | yes | 35 | yes | Stride-5 no longer stacks frames to fill the image |

Conclusion: the user's interpretation was correct for the stride image. The old `20260508T174921Z` style capture packed multiple low-resolution frame slices into one PNG because the firmware kept running across frames. The remaining single-frame stride-1 artifact still needs a faster/more faithful capture path or a narrower-window model.

## 5. Next Steps

- Preserve current wiring table and continue input-only tests before assigning any GBC LCD capture GPIOs.
- Add a better DCLK measurement path because GPIO interrupt edge counts are not suitable for 6-8 MHz.
- Verify the GPIO22/DCLK frequency at the connector with external equipment or a second capture method.
- Use timing-edge artifacts to derive frame/line ordering before connecting RGB data pins.
- Repeat timing relationship analysis during boot logo and gameplay to check whether LP/SPL counts remain stable.
- Use manual-trigger timing sessions to coordinate GBC power-cycle captures.
- For boot captures, use a longer `--pre-delay-ms` than 250 ms because the first manual boot attempt produced zero timing events.
- Add dated entries for every measurement session.
- Include equipment, wiring, firmware version, host command, raw outputs, and conclusions.
- Link captures and screenshots stored under `captures/`.
- Build a GPIO-routed DVP trial only after documenting the 8-bit data mapping and choosing which timing signal is safe to test as DVP `DE`.
- Build a custom line-burst capture around `SPS` frame sync, `SPL` falling line start, and about `160` DCLK-sampled pixels per line.
- Create line-burst-specific post-processing tools to search sample phase, bit order, line order, and active-window offset using the complete `20260508T162521Z` capture.
- Add marker/phase controls to `CAPTURE_RG_LINE_BURSTS`: compare `SPL` and `LP`, marker skip, modulo marker selection, and fixed DCLK delay after marker before storing pixels.
- Sweep line-burst widths (`32`, `40`, `80`, `160`) and render scaled outputs/contact sheets to test whether the capture is slower than the true transfer or whether the LCD bus emits smaller chunks per marker.
- Investigate ESP32-P4 LCD_CAM/DVP DMA capture again, using the line-burst findings as constraints, because the next direction is sampling more data faster rather than skipping markers across frames.

2026-05-08: Implemented variable-size generic DVP captures and an experimental ISP-DVP capture path.

Firmware and host changes:

- `DVP_CAPTURE_RAW` accepts optional `h_res` and `v_res`.
- Added `ISP_DVP_CAPTURE_RAW` command to test separate HSYNC and DE inputs.
- Added `host/capture_isp_dvp_raw.py`.
- Firmware build and flash succeeded; smoke test passed afterward.

Generic DVP DMA experiment results:

| Artifact prefix | DE | Size | PCLK Invert | Result |
|---|---|---:|---|---|
| `20260508T180106Z-dvp_raw_lp_160x144` | LP | 160x144 | no | Failed, `esp_err=263` |
| `20260508T180232Z-dvp_raw_spl_160x144` | SPL | 160x144 | no | Completed, stripe/noise dominated |
| `20260508T180303Z-dvp_raw_spl_160x154` | SPL | 160x154 | no | Completed, stripe/noise dominated |
| `20260508T180404Z-dvp_raw_spl_160x144` | SPL | 160x144 | yes | Completed, equivalent to non-inverted checksum |
| `20260508T180435Z-dvp_raw_spl_160x154` | SPL | 160x154 | yes | Completed, still stripe/noise dominated |

ISP-DVP experiment results:

| Artifact prefix | HSYNC | DE | Result |
|---|---|---|---|
| `20260508T180906Z-isp_dvp_raw_hlp_despl_160x144` | LP | SPL | Failed, `esp_err=259` |
| `20260508T180955Z-isp_dvp_raw_hnc_despl_160x144` | NC | SPL | Failed, `esp_err=259` |
| `20260508T181202Z-isp_dvp_raw_hnc_despl_160x144` | NC | SPL | Failed after one-shot completion fix, `esp_err=259` |

Conclusion: generic DVP DMA captures data faster, but the current camera-style sync model still does not reconstruct the GBC screen. ISP-DVP is the right conceptual direction because it exposes HSYNC separately, but the current configuration needs failure-stage instrumentation or a different ISP format setup.

2026-05-08: Pinned the USB command/capture task to HP core 1 and added `CORE_STATUS`.

Firmware change:

- `usb_protocol_start()` now uses `xTaskCreatePinnedToCore(..., core=1)` when built with two FreeRTOS cores.
- `CORE_STATUS` reports the current core, pinned task core, main task affinity, and ESP timer task/ISR affinity.

Verification:

```json
{"command":"CORE_STATUS","current_core":1,"esp_timer_isr_affinity":"CPU0","esp_timer_task_affinity":"CPU0","freertos_cores":2,"main_task_affinity":"CPU0","ok":true,"usb_protocol_task_pinned_core":1}
```

Conclusion: CPU-polled capture commands now run on HP core 1 instead of migrating between cores. `app_main` and ESP timer remain on CPU0. This should reduce scheduler migration jitter in polling experiments, while DMA capture remains the preferred long-term path.

2026-05-08: Instrumented ISP-DVP failure stages and tested ISP RGB565 output.

Firmware and host changes:

- ISP-DVP failures now report `failure_stage`, `failure_err`, and `received_size`.
- Changed the ISP-DVP path from bypassed ISP to non-bypass setup after `esp_isp_enable` rejected bypass mode.
- Reworked ISP-DVP capture to provide the DMA transaction through `on_get_new_trans` before `esp_cam_ctlr_start`.
- Added `ISP_DVP_CAPTURE_RGB565` and `host/capture_isp_dvp_raw.py --format rgb565`.

On-device findings:

- Bypassed ISP failed at `esp_isp_enable`.
- Non-bypass RAW8 without callback-provided transaction failed at `esp_cam_ctlr_start`.
- Callback-driven RAW8 ISP-DVP completed full `160x144` buffers but returned all-zero payloads for `LP/SPL`, `NC/SPL`, DE-inverted, and PCLK-inverted variants.
- RGB565 ISP-DVP also completed a full `160x144` buffer and returned all zeros:
  `captures/decoded/isp_dvp_raw/20260508T183810Z-isp_dvp_rgb565_hlp_despl_160x144.*`, `received_size=46080`, checksum `0`, transitions `0`.

Verification:

- Firmware build passed.
- Flash to `/dev/cu.usbmodem14401` passed.
- `host/gbc_probe.py --port /dev/cu.usbmodem14401 --timeout 3 smoke` passed after flashing.

Conclusion: ISP-DVP is now reaching complete DMA transfers, but it is not yielding usable bus data. RGB565 output being all zero rules out the simplest ISP output-format explanation. The next implementation direction should be a lower-level LCD_CAM/GDMA raw sampler or another path that avoids ISP camera-frame semantics.

2026-05-08: Added and tested a byte-count EOF DVP capture mode.

Firmware and host changes:

- Added `DVP_CAPTURE_RAW_LEN`.
- Added `host/capture_dvp_raw.py --byte-count-eof`.
- The command uses the generic DVP LCD_CAM/GDMA setup but switches LCD_CAM from VSYNC EOF to `CAM_REC_DATA_BYTELEN` EOF.

Verification:

- Firmware build passed.
- Flash to `/dev/cu.usbmodem14401` passed.
- Smoke test passed after flashing.

Experiment results:

| Artifact | Change | Result |
|---|---|---|
| `captures/decoded/dvp_raw/20260508T184326Z-dvp_raw_len_spl_160x144.json` | Initial `CAM_REC_DATA_BYTELEN=N` | Timeout, `esp_err=263` |
| `captures/decoded/dvp_raw/20260508T184455Z-dvp_raw_len_spl_160x144.json` | `CAM_REC_DATA_BYTELEN=N-1` | Timeout, `esp_err=263` |
| `captures/decoded/dvp_raw/20260508T184640Z-dvp_raw_len_spl_160x144.json` | Register writes after high-level `esp_cam_ctlr_start()` | Timeout, `esp_err=263` |
| `captures/decoded/dvp_raw/20260508T184721Z-dvp_raw_len_spl_160x144.json` | Same as above with VSYNC inverted | Timeout, `esp_err=263` |

Conclusion: the generic DVP wrapper is not a good place to force byte-count EOF. The next implementation should directly manage LCD_CAM/GDMA descriptors and report partial descriptor progress on timeout.

2026-05-08: Implemented and tested first private LCD_CAM/GDMA sampler.

Firmware and host changes:

- Added `firmware/main/lcdcam_raw.c` and `firmware/main/lcdcam_raw.h`.
- Added `LCDCAM_RAW_CAPTURE`.
- Added `host/capture_lcdcam_raw.py`.
- The sampler allocates its own AXI GDMA RX channel, connects it to `CAM0`, owns RX DMA descriptors, routes the existing `R2-R5/G2-G5` diagnostic byte, and reports descriptor progress.

Verification:

- Firmware build passed.
- Flash to `/dev/cu.usbmodem14401` passed.
- Smoke test passed.

Experiment results:

| Artifact | Mode | Result |
|---|---|---|
| `captures/decoded/lcdcam_raw/20260508T185729Z-lcdcam_raw_spl_160x144.*` | VSYNC EOF | Completed; checksum `5603919`, transitions `2860`, `received_size=129`, `completed_descriptors=1/6` |
| `captures/decoded/lcdcam_raw/20260508T185821Z-lcdcam_raw_spl_160x144.*` | Byte-count EOF | Timed out; `received_size=20460`, exactly `5/6` descriptors, checksum `5858728` |
| `captures/decoded/lcdcam_raw/20260508T185928Z-lcdcam_raw_spl_160x127.*` | Byte-count EOF | Completed; `5/5` descriptors, all `0x00` |
| `captures/decoded/lcdcam_raw/20260508T190027Z-lcdcam_raw_spl_160x128.*` | Byte-count EOF | Completed; `6/6` descriptors, all `0xff` |

Conclusion: direct LCD_CAM/GDMA ownership works and gives better observability than the high-level DVP wrapper. The current byte-count mode can complete but is sampling static bus states. The VSYNC-ended private capture sees variable data but descriptor accounting is not coherent yet. Next firmware step: add per-descriptor length/owner/eof reporting and test capture start ordering relative to `SPS/SPL`.

2026-05-08: Tested private LCD_CAM/GDMA start ordering against a fresh marker-aware reference.

Experiment results:

| Artifact | Mode | Result |
|---|---|---|
| `captures/decoded/lcdcam_raw/20260508T190943Z-lcdcam_raw_spl_160x144.*` | Private LCD_CAM/GDMA, VSYNC EOF, start after `SPS` rising then `SPL` falling | Completed; `start_trigger_seen=true`, checksum `5566114`, transitions `2920`, `received_size=143`, `completed_descriptors=1/6` |
| `captures/decoded/rg_line_bursts/20260508T191242Z-rg_line_bursts_160x144.*` | CPU-polled marker-aware reference, `SPS` rising, `SPL` falling, `DCLK` rising | Completed; `144/144` lines, `160/160` samples per line, checksum `5318025`, transitions `1688` |

Generated comparison artifact:

- `captures/decoded/rg_line_bursts/20260508T191242Z-rg_line_bursts_160x144_tile_5x5_sheet.png`
- `captures/decoded/compare_marker_vs_lcdcam_20260508T191242Z.png`: left is the earlier best tile crop, middle is the fresh marker-aware full-frame reference, right is the private LCD_CAM start-after-`SPS`/`SPL` capture.

Conclusion: the fresh CPU-polled line-burst capture remains coherent with the same wiring and data packing, while the private LCD_CAM/GDMA stream still ends after a short fragment when using VSYNC EOF. This makes the current mismatch a framing/gating problem rather than evidence that the RG wiring changed or that the bus is fully unstable. The next useful implementation is line-windowed peripheral capture, or a lower-overhead marker-aware sampler that preserves the proven `SPS`/`SPL`/`DCLK` relationship.

2026-05-08: Added and tested LCD_CAM `VH+DE` diagnostic mode.

Implementation:

- Added an optional `vh_de_mode` argument to `LCDCAM_RAW_CAPTURE`.
- In this mode, LCD_CAM uses `DE`, `HSYNC`, and `VSYNC` instead of only `DE` and `VSYNC`.
- With `DE=SPL`, the alternate marker `LP` is routed as `HSYNC`.

Experiment result:

| Artifact | Mode | Result |
|---|---|---|
| `captures/decoded/lcdcam_raw/20260508T192046Z-lcdcam_raw_spl_160x144.*` | Private LCD_CAM/GDMA, `SPL` as DE, `LP` as HSYNC, `SPS` as VSYNC, start after `SPS` then `SPL` | Completed; checksum `5563924`, transitions `2939`, descriptor accounting reported `received_size=0`, `completed_descriptors=1/6` |

Generated artifact:

- `captures/decoded/lcdcam_raw/20260508T192046Z-lcdcam_raw_spl_160x144_tile_5x5_sheet.png`

Conclusion: enabling LCD_CAM's conventional `HSYNC/DE/VSYNC` mode did not fix the tiling/framing problem. The GBC LCD bus markers should not yet be treated as a normal DVP camera interface. The strongest current hypothesis is that CPU polling is producing a decimated but line-synchronized image, while LCD_CAM is sampling faster but lacks the explicit line-window control required by this bus.

2026-05-08: Shifted LCD_CAM strategy to "capture fast now, decode later."

Implementation:

- Added `DE=HIGH` to `LCDCAM_RAW_CAPTURE`.
- `DE=HIGH` routes GPIO matrix constant-one into LCD_CAM `DE`, so the peripheral samples every accepted `DCLK` instead of being gated by `SPL` or `LP`.
- Added `host/render_raw_stream_widths.py` to render one raw stream at multiple candidate row widths without changing the original capture.

Experiments:

| Artifact | Mode | Result |
|---|---|---|
| `captures/decoded/lcdcam_raw/20260508T192441Z-lcdcam_raw_spl_320x204.*` | Byte-count EOF, `SPL` as DE, start after `SPS/SPL` | Timed out but filled `20460` bytes; checksum `13572872`, transitions `21398` |
| `captures/decoded/lcdcam_raw/20260508T192613Z-lcdcam_raw_lp_320x204.*` | Byte-count EOF, `LP` as DE, start after `SPS` | Produced byte-identical data to the `SPL`-gated attempt, suggesting the gated byte-count mode is not a trustworthy framing primitive |
| `captures/decoded/lcdcam_raw/20260508T192953Z-lcdcam_raw_high_320x204.*` | Byte-count EOF, `DE=HIGH`, normal PCLK edge | Completed full `65280`-byte capture, `16/16` descriptors, checksum `15136290`, transitions `1512` |
| `captures/decoded/lcdcam_raw/20260508T193122Z-lcdcam_raw_high_320x204.*` | Byte-count EOF, `DE=HIGH`, inverted PCLK edge | Completed full `65280`-byte capture, `16/16` descriptors, checksum `15192724`, transitions `2874` |

Width sweep outputs were written under:

- `captures/decoded/lcdcam_raw/width_sweeps/`

Conclusion: `DE=HIGH` is now the best fast-capture mode because it produces complete DMA buffers at peripheral speed. It intentionally discards line gating during capture. Reconstruction should now happen offline by searching candidate row widths, frame offsets, sample edge, and marker-derived phase relationships.

2026-05-08: User reported that `20260508T193122Z-lcdcam_raw_high_320x204_stream_w320_h204` shows roughly six readable repeated screens, with letters leaning left.

Follow-up artifacts:

- Fine width sweep around `320`: `captures/decoded/lcdcam_raw/width_sweeps/fine_193122/`
- Deskew sweep for `width=320`, `height=204`: `captures/decoded/lcdcam_raw/deskew_193122_w320/`

Interpretation: `width=320` is close to a meaningful stream stride, but the buffer likely contains multiple frame/field windows and a residual per-line phase error. Left-leaning text is consistent with a small row-stride mismatch or a constant per-line phase drift. The next decoder work should search row stride and per-row phase correction before attempting final `160x144` extraction.

2026-05-08: User identified `20260508T193122Z-lcdcam_raw_high_320x204_w320_h204_skewp0p00` as straight, with multiple readable frames in a mosaic.

Generated extraction candidates:

- `captures/decoded/lcdcam_raw/frame_extract_193122_sw160/`: interpret the raw stream as `160`-wide rows and extract `160x144` windows at multiple vertical offsets.
- `captures/decoded/lcdcam_raw/frame_extract_193122_sw320_halves/`: keep `320`-wide stream rows and extract left/right `160x144` halves at multiple vertical offsets.

Interpretation: no deskew being needed means the stream order is stable. The mosaic may be two 160-wide line windows packed side-by-side, or several frame windows concatenated in a 160-wide stream. Visual inspection of these extraction candidates should determine whether the correct model is `stream_width=160` with vertical frame offsets, or `stream_width=320` with left/right half extraction.

2026-05-08: Added browser gallery for capture artifact review.

Implementation:

- Added `host/capture_gallery.py`.
- The gallery recursively serves PNG artifacts from a capture directory, supports filename/folder filtering, tile-size controls, sort order, and click-to-enlarge viewing.

Run command used:

```sh
python3 host/capture_gallery.py --root captures/decoded/lcdcam_raw --listen-port 8766
```

Local URL:

- `http://127.0.0.1:8766/`

Reason: manual folder browsing is now too slow for candidate selection. The current work depends on rapid visual comparison of many frame extraction, width sweep, and deskew hypotheses.

2026-05-08: User tuned the selected extraction in the slider viewer.

Selected source:

- `captures/decoded/lcdcam_raw/frame_extract_193122_sw160/20260508T193122Z-lcdcam_raw_high_320x204_sw160_x0_y240_160x144.bin`

Viewer state:

```json
{"xShift":82,"yShift":49,"lineShift":1,"fineLineShift":0,"invert":false,"reverseBits":false,"swapChannels":false}
```

Generated artifacts:

- Exact state render: `captures/decoded/lcdcam_raw/interactive_states_193122/20260508T193122_sw160_x0_y240_state_x82_y49_line1_fine0.png`
- Refinement grid: `captures/decoded/lcdcam_raw/interactive_states_193122/`
- Focused gallery: `http://127.0.0.1:8769/`

Interpretation: this confirms a useful extraction window around `stream_width=160`, source `y=240`, plus additional phase correction. The nonzero `lineShift=1` means our current extraction is close but still compensating for line-to-line phase drift in post-processing.

2026-05-08: User identified `grid_x86_y49_line1_finem0p10.png` as almost correct.

Generated tighter refinement set:

- Best candidate copy: `captures/decoded/lcdcam_raw/interactive_refine_193122_x86_y49/best_from_user_x86_y49_line1_finem0p10.png`
- Tight grid around `xShift=86`, `yShift=49`, `lineShift=1`, `fineLineShift=-0.10`: `captures/decoded/lcdcam_raw/interactive_refine_193122_x86_y49/`
- Focused gallery: `http://127.0.0.1:8770/`

Current best visual decode state:

```json
{"xShift":86,"yShift":49,"lineShift":1,"fineLineShift":-0.10,"invert":false,"reverseBits":false,"swapChannels":false}
```

2026-05-08: Offline period analysis found a strong `161`-byte row model.

Method:

- Treated the `DE=HIGH` fast capture `20260508T193122Z-lcdcam_raw_high_320x204.bin` as rows of varying candidate widths.
- For each width, compared rows against later rows over candidate periods.
- The strongest repeat was `width=161`, `period=145` rows. The mean row difference at that period was near zero compared with neighboring widths/periods.

Interpretation:

- `161` bytes per line explains why the earlier `160`-wide interactive view needed a roughly `+1` pixel-per-row line correction.
- `145` rows per repeat matches the measured `SPL` count per frame, so the fast stream likely contains 145 visible/transfer lines per frame rather than a markerless random continuum.
- This means we likely can identify frame start/end from the raw stream: frame windows repeat every `161 * 145 = 23345` bytes after the initial `SPS` trigger.

Generated artifacts:

- Full stream rendered as `161`-wide rows: `captures/decoded/lcdcam_raw/width_sweeps/fine_193122/20260508T193122Z-lcdcam_raw_high_320x204_stream_w161_h405.png`
- Normal `160x144` frame candidates: `captures/decoded/lcdcam_raw/frame_extract_193122_sw161_period145/`
- Inverted `160x144` frame candidates: `captures/decoded/lcdcam_raw/frame_extract_193122_sw161_period145_inverted/`
- Focused gallery: `http://127.0.0.1:8771/`

2026-05-08: Added live LCD_CAM stream viewer with decode sliders.

Implementation:

- Added `host/live_lcdcam_stream_viewer.py`.
- The server repeatedly runs `LCDCAM_RAW_CAPTURE HIGH 2500 0 0 1 1 320 204 1 0`.
- The browser keeps sliders for stream width, source X/Y, X/Y shift, line skew, fine skew, invert, bit order, and channel swap.

Run command:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/live_lcdcam_stream_viewer.py --port /dev/cu.usbmodem14401 --listen-port 8772 --pclk-invert --interval-ms 1400
```

Local URL:

- `http://127.0.0.1:8772/`

Initial recommended controls:

- `streamWidth=161`
- Try `sourceY=0`, `145`, or `290`
- Toggle `invert` if colors are backwards

2026-05-08: Promoted the fast-stream decode to a named reproducible preset.

Implementation:

- Added `host/decode_lcdcam_fast.py`.
- Added preset `gbc_rg44_fast_v1`.

Preset:

```json
{
  "stream_width": 161,
  "frame_period_rows": 145,
  "visible_width": 160,
  "visible_height": 144,
  "x_offset": 0,
  "y_offset": 0,
  "invert": false,
  "packing": "RAW8 RG44"
}
```

Generated outputs from the known good fast capture:

- `captures/decoded/lcdcam_raw/decoded_fast_v1_193122/`
- `captures/decoded/lcdcam_raw/decoded_fast_v1_193122_inverted/`

Conclusion: future `DE=HIGH`, DCLK-inverted LCD_CAM captures can now be decoded with one repeatable command instead of manual slider reconstruction.

2026-05-08: Re-tested `gbc_rg44_fast_v1` on a fresh fast capture.

Capture:

- Command mode: `LCDCAM_RAW_CAPTURE HIGH`, byte-count EOF, DCLK inverted edge, start after `SPS`.
- Raw JSON: `captures/decoded/lcdcam_raw/20260508T200442Z-lcdcam_raw_high_320x204.json`
- Raw BIN: `captures/decoded/lcdcam_raw/20260508T200442Z-lcdcam_raw_high_320x204.bin`
- Result: `ok=true`, `received_size=65280`, `completed_descriptors=16/16`, checksum `15192882`, transitions `2871`.

Decoded outputs:

- Normal: `captures/decoded/lcdcam_raw/decoded_fast_v1_200442/`
- Inverted: `captures/decoded/lcdcam_raw/decoded_fast_v1_200442_inverted/`
- Focused gallery: `http://127.0.0.1:8774/`

Interpretation: the fresh capture metrics are very close to the earlier `20260508T193122Z` good capture, and the preset again found two complete frame periods. This is the first repeatability check for `gbc_rg44_fast_v1`.

2026-05-08: User connected blue data lines.

Wiring:

| GBC Pin | Signal | ESP32-P4 GPIO |
|---:|---|---:|
| 31 | B5 | 50 |
| 30 | B4 | 48 |
| 29 | B3 | 47 |
| 28 | B2 | 46 |
| 27 | B1 | 45 |
| 26 | B0 | 36 |

Changes:

- Added GPIO36, GPIO45, GPIO46, GPIO47, GPIO48, and GPIO50 to the input-only test allowlist.
- Added `RGB332` LCD_CAM data mode while preserving existing `RG44`.
- Added host RGB332 rendering and preset `gbc_rgb332_fast_v1`.
- Built, flashed to `/dev/cu.usbmodem14401`, and smoke-tested successfully.

Initial RGB332 packing:

```text
bit7 R5, bit6 R4, bit5 R3,
bit4 G5, bit3 G4, bit2 G3,
bit1 B5, bit0 B4
```

Note: lower blue bits `B3-B0` are wired but not yet used by the first RGB332 diagnostic path.

2026-05-08: Checked blue GPIO activity and captured first RGB332 fast stream.

Blue input-only edge counts over 1000 ms:

| Signal | GPIO | Rising | Falling | Total |
|---|---:|---:|---:|---:|
| B5 | 50 | 9067 | 472 | 9539 |
| B4 | 48 | 9067 | 472 | 9539 |
| B3 | 47 | 9086 | 480 | 9566 |
| B2 | 46 | 9077 | 480 | 9557 |
| B1 | 45 | 9075 | 480 | 9555 |
| B0 | 36 | 0 | 0 | 0 |

Interpretation: `B5` through `B1` are active in this screen state. `B0` was static during this check; this may be legitimate low-bit behavior for the boot screen, a wiring issue, or a board/GPIO caveat. The initial RGB332 capture only uses `B5` and `B4`, so it is not blocked by the static `B0` result.

Capture:

- Command mode: `LCDCAM_RAW_CAPTURE HIGH`, byte-count EOF, DCLK inverted edge, start after `SPS`, data mode `RGB332`.
- Raw JSON: `captures/decoded/lcdcam_raw/20260508T202905Z-lcdcam_raw_high_320x204.json`
- Raw BIN: `captures/decoded/lcdcam_raw/20260508T202905Z-lcdcam_raw_high_320x204.bin`
- Result: `ok=true`, `received_size=65280`, `completed_descriptors=16/16`, checksum `15226914`, transitions `2243`, `failure_stage=none`.

Decoded outputs:

- Normal: `captures/decoded/lcdcam_raw/decoded_rgb332_fast_v1_20260508T202905Z/`
- Inverted: `captures/decoded/lcdcam_raw/decoded_rgb332_fast_v1_20260508T202905Z_inverted/`
- Focused gallery: `http://127.0.0.1:8775/`

Conclusion: full-color diagnostic capture is now wired and reproducible enough for visual inspection. The geometry is still using the proven fast-stream model, `stream_width=161` and `frame_period_rows=145`.

2026-05-08: Updated live browser viewer for safer manual capture control.

Reason: user observed that while active capture is running, switching the GBC off and on can cause the console to blink and power off, suggesting the active capture setup may be loading or disturbing the bus/power state.

Changes:

- `host/live_lcdcam_stream_viewer.py` now starts with capture stopped by default.
- Added browser controls: `Start`, `Stop`, `Single`, and `Auto capture`.
- Added `--data-mode RG44|RGB332`; default is now `RGB332` for the current blue-enabled wiring.
- Browser decode now supports `RGB332` directly, in addition to the older `RG44` behavior.

Run command:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/live_lcdcam_stream_viewer.py --port /dev/cu.usbmodem14401 --listen-port 8776 --pclk-invert --interval-ms 1500 --data-mode RGB332
```

Local URL:

- `http://127.0.0.1:8776/`

Recommended procedure: press `Stop` before powering the GBC off or on. Use `Single` immediately after a stable boot state to minimize capture duty cycle while investigating the power issue.

2026-05-08: Reprioritized live viewer controls around color and bit diagnostics.

Changes:

- Moved color/bit operations above geometry controls.
- Added channel order selector: `RGB`, `RBG`, `GRB`, `GBR`, `BRG`, and `BGR`.
- Added channel enable shortcuts: `Only R`, `Only G`, `Only B`, and `All Channels`.
- Added per-channel invert controls.
- Added per-channel bit-reverse controls.
- Added packed bit-plane masks `b7` through `b0`.

Reason: after the frame geometry became recognizable, color ordering and bit interpretation became the highest-value debugging variables. For current `RGB332` captures, `b7..b5` are red, `b4..b2` are green, and `b1..b0` are blue.

Updated local URL:

- `http://127.0.0.1:8777/`

2026-05-08: Added explicit passive-bus recovery after suspected capture loading issue.

Observation: user reported that the browser did not appear to stop capture, or that one or more pins may have remained in a low-resistance mode. Process check found two live viewer servers still running on ports `8776` and `8777`; both were killed before firmware changes.

Firmware changes:

- Added `SAFE_IDLE` command.
- `SAFE_IDLE` stops LCD_CAM, detaches LCD_CAM input signals from the GPIO matrix by routing them to constant zero, and reconfigures all currently wired GBC bus GPIOs as floating inputs with interrupts disabled.
- `lcdcam_raw_capture()` now calls the same safe-idle routine before capture setup and after both success and failure cleanup paths.

Host changes:

- Browser `Stop` now calls `/api/safe-idle`.
- The live viewer server calls `SAFE_IDLE` when shutting down.

Verification:

- Build passed.
- Flash to `/dev/cu.usbmodem14401` passed.
- Direct command returned:

```json
{"command":"SAFE_IDLE","err":0,"error":"none","mode":"lcdcam_detached_gpio_floating_input","ok":true}
```

- Smoke test passed after flashing.

Next hardware check: with no live viewer process running and after direct `SAFE_IDLE`, verify whether the GBC can power on and stay on. If power is still unstable, the issue is likely physical/electrical loading from the wiring or board pins rather than active firmware capture.

2026-05-08: Started reduced-payload live viewer for higher browser FPS.

Reason: after increasing power supply current, the GBC remained stable enough to try faster browser updates. The previous live viewer used `320x204` raw captures, or `65280` bytes per capture. The current fast-stream model only needs about one decoded frame period, so the viewer was restarted with `161x145`, or `23345` bytes per capture.

Run command:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/live_lcdcam_stream_viewer.py --port /dev/cu.usbmodem14401 --listen-port 8778 --pclk-invert --interval-ms 250 --capture-timeout-ms 1000 --width 161 --height 145 --data-mode RGB332
```

Local URL:

- `http://127.0.0.1:8778/`

Expected impact: roughly 64 percent less serial/JSON payload per frame than `320x204`, so browser update rate should improve without changing the decode model.

Result: the reduced `161x145` capture returned a complete buffer but all pixel data was zero:

- `received_size=23345`
- `checksum=0`
- `min_value=0`
- `max_value=0`
- `raw8_transitions=0`

Conclusion: the reduced byte-count capture starts or ends outside the valid LCD_CAM data window. Reverted live viewing to the known-good `320x204` capture size.

2026-05-08: Added binary browser transport for live viewer frames.

Implementation:

- Added `/api/frame.bin` to `host/live_lcdcam_stream_viewer.py`.
- Python server still receives firmware JSON over serial, but converts `data_hex` to raw bytes before responding to the browser.
- Capture metadata is sent in the `X-Capture-Meta` HTTP header.
- Browser now consumes `ArrayBuffer` instead of parsing hex in JavaScript.

Run command:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/live_lcdcam_stream_viewer.py --port /dev/cu.usbmodem14401 --listen-port 8780 --pclk-invert --interval-ms 450 --capture-timeout-ms 2500 --width 320 --height 204 --data-mode RGB332
```

Local URL:

- `http://127.0.0.1:8780/`

Expected impact: less local HTTP payload and no browser-side hex parsing. Serial transport is still JSON/hex, so further FPS improvement requires a firmware command that streams raw binary capture bytes directly over USB.

2026-05-08: Added host-side crop for live viewer payload reduction.

Reason: direct `161x145` LCD_CAM DMA capture returned all zeros, but the valid frame is known to be inside the reliable `320x204` raw capture. Host-side crop keeps the proven hardware capture window and only reduces what the local Python server sends to the browser.

Implementation:

- Added live viewer options: `--host-crop`, `--crop-offset`, `--crop-width`, and `--crop-height`.
- The server captures `320x204` from ESP32-P4, converts firmware hex to bytes, then slices the byte payload before sending `/api/frame.bin`.
- Metadata includes `host_crop_offset`, `host_crop_len`, and `host_cropped_size`.

Run command:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/live_lcdcam_stream_viewer.py --port /dev/cu.usbmodem14401 --listen-port 8781 --pclk-invert --interval-ms 350 --capture-timeout-ms 2500 --width 320 --height 204 --data-mode RGB332 --host-crop --crop-offset 0 --crop-width 161 --crop-height 145
```

Local URL:

- `http://127.0.0.1:8781/`

Current crop: first `23345` bytes of the reliable raw stream, equivalent to `161x145`. If this crop is visually wrong, the next variable is `crop_offset`, not the LCD_CAM capture size.

2026-05-08: Tested tighter source-period LCD_CAM capture sizes.

Reason: the decoded frame model is now `161` stream bytes by `145` rows, so the next experiment was to make ESP32-P4 capture closer to that original source period instead of always taking the larger `320x204` diagnostic window.

Control result:

- `161x145`, `DE=HIGH`, byte-count EOF, start after `SPS`: completed `23345` bytes but all zeros.
- `161x145`, start modes immediate, after `SPS`, and after `SPS` then `SPL`: all completed as all-zero buffers.
- `SPL/LP` line-gated `VH+DE` mode at `161x145`: timed out or produced empty/zero buffers. VSYNC EOF variants completed but received no useful data.

Width sweep with `DE=HIGH`, `RGB332`, byte-count EOF, start after `SPS`:

| Size | Result |
|---|---|
| `180x145` | Complete, all zero |
| `192x145` | Complete, nonzero, checksum `6625878`, transitions `747` |
| `200x145` | Complete, all zero |
| `224x145` | Complete, all zero |
| `232x145` | Complete, all zero |
| `239x145` | Complete, all zero |
| `240x145` | Complete, all zero |
| `255x145` | Complete, all zero |
| `256x145` | Complete, nonzero, checksum `8624358`, transitions `1432` |
| `288x145` | Complete, all zero |

Decoded artifacts:

- `captures/decoded/lcdcam_raw/20260508T212557Z-lcdcam_raw_high_192x145_gbc_rgb332_fast_v1/20260508T212557Z-lcdcam_raw_high_192x145_gbc_rgb332_fast_v1_frame0_x0_y0.png`
- `captures/decoded/lcdcam_raw/20260508T212934Z-lcdcam_raw_high_256x145_gbc_rgb332_fast_v1/20260508T212934Z-lcdcam_raw_high_256x145_gbc_rgb332_fast_v1_frame0_x0_y0.png`

Both decoded frames show a recognizable boot screen with `GAME BOY` text and the Nintendo mark. The `192x145` capture is the smallest confirmed useful LCD_CAM geometry so far. It still captures `27840` bytes, then decodes the first `161x145` source-period stream model from that buffer.

Interpretation: the current private LCD_CAM path has geometry/alignment behavior that prevents a direct `161x145` byte-count capture from returning live samples. This is probably a peripheral configuration or FIFO/DMA alignment issue, not a GBC source-size issue, because `192x145`, `256x145`, `320x145`, and `320x204` all produce live samples under otherwise similar settings. For now, use `192x145` as the reduced reliable capture window. A true source-exact firmware path still requires either fixing LCD_CAM line/frame gating or adding a lower-level line-window sampler that waits for `SPS/SPL` and captures exactly the source DCLK windows.

2026-05-08: Added firmware binary capture transport for real-time browser viewing.

Reason: the JSON/hex capture response doubled the USB serial payload size and forced host/browser hex parsing. This was the largest immediate FPS bottleneck after reducing the reliable capture window to `192x145`.

Implementation:

- Added firmware command `LCDCAM_RAW_CAPTURE_BIN`.
- The command prints one JSON header with `binary_len`, capture metadata, checksum, descriptor counts, and failure fields.
- On success, firmware writes the raw capture buffer directly after the header with `fwrite()`.
- Added `ProbeClient.command_binary()` to read the JSON header and then exactly `binary_len` raw bytes.
- Updated `host/live_lcdcam_stream_viewer.py` to use firmware binary transport by default.

Verification:

- Build passed.
- Flash to `/dev/cu.usbmodem14401` passed.
- Test command `LCDCAM_RAW_CAPTURE_BIN HIGH 2500 0 0 1 1 192 145 1 0 RGB332` returned `binary_len=27840`, `received_size=27840`, nonzero checksum, and `747` raw transitions.
- Local HTTP check against `http://127.0.0.1:8783/api/frame.bin` returned status `200`, body length `23345`, and metadata `transport=firmware_binary`.

Current live viewer:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/live_lcdcam_stream_viewer.py --port /dev/cu.usbmodem14401 --listen-port 8783 --pclk-invert --interval-ms 120 --capture-timeout-ms 2500 --width 192 --height 145 --data-mode RGB332 --firmware-binary --host-crop --crop-offset 0 --crop-width 161 --crop-height 145
```

Local URL:

- `http://127.0.0.1:8783/`

This is still request/response per frame over USB CDC serial. The next major FPS improvement would be a streaming mode that keeps LCD_CAM/GDMA and USB writes in a loop after `START_STREAM`, followed later by a true USB bulk endpoint if CDC serial becomes the limiting factor.

2026-05-08: Added continuous server-side live capture mode.

Reason: browser-driven capture still waited for a full serial command/response on every `/api/frame.bin` request. Continuous mode keeps a Python background thread capturing frames as fast as the ESP32-P4 command path allows. The browser then polls the latest completed frame without blocking directly on the serial command.

Implementation:

- Added `--continuous-capture` to `host/live_lcdcam_stream_viewer.py`.
- Added `/api/start`, `/api/stop`, and `/api/status`.
- `Start` begins the server-side capture loop.
- `Stop` stops the loop and sends `SAFE_IDLE`.
- `/api/frame.bin` returns the most recent completed frame when continuous mode is enabled.
- Frame metadata now includes `server_frame_count`, `server_last_capture_ms`, and `server_capture_fps`.

Current stable command:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/live_lcdcam_stream_viewer.py --port /dev/cu.usbmodem14401 --listen-port 8785 --pclk-invert --interval-ms 33 --capture-timeout-ms 2500 --width 192 --height 145 --data-mode RGB332 --firmware-binary --no-source-binary --continuous-capture --host-crop --crop-offset 0 --crop-width 161 --crop-height 145
```

Local URL:

- `http://127.0.0.1:8785/`

Measured result after `/api/start`:

- Browser payload: `23345` bytes after host crop.
- Server capture rate: about `6.86 fps`.
- Server capture loop time: typically `137..154 ms`.
- Firmware-reported `capture_us`: about `27..40 ms` in sampled frames.

Interpretation: continuous server-side capture improves UI behavior and removes browser request timing from the capture loop, but it does not solve the main FPS limit. The remaining gap is between firmware capture time (`~30 ms`) and full host loop time (`~140 ms`), which points to USB console transport, command/response framing, stdio flushing, and per-frame LCD_CAM/GDMA setup/teardown as the next bottlenecks.

Attempted but not promoted:

- A short `LCDCAM_RAW_CAPTURE_SRC_BIN` command that emits only the first `161x145` source-period bytes.
- It reduced intended payload to `23345` bytes, but arbitrary-length binary writes over the current console path produced short reads in host testing (`23296/23345` bytes received in one run).
- Keep using the reliable full `192x145` binary payload plus host crop until the transport is made explicitly binary-safe.

Next required speed work:

1. Persistent firmware streaming mode that configures LCD_CAM/GDMA once and emits repeated frames until stopped.
2. A binary-safe USB transport, preferably TinyUSB bulk or a deliberately framed CDC protocol with escaping/length checks and resynchronization.
3. Avoid per-frame heap allocation, DVP init/deinit, GPIO matrix setup, and `SAFE_IDLE` inside the hot frame loop.

2026-05-09: Added batched firmware binary streaming.

Reason: continuous host capture still issued one firmware command per frame, giving about `6.9 fps`. The next safe optimization on the existing console transport was to amortize command/response overhead across multiple binary frames.

Implementation:

- Added firmware command `LCDCAM_RAW_STREAM_BIN <frame_count_1_to_64>`.
- The command emits repeated `192x145` RGB332 binary frame responses, each with its own JSON header and `27840` raw bytes.
- Added `ProbeClient.command_binary_sequence()` to read multiple header+payload frames from one command.
- Added `--stream-batch-size` to `host/live_lcdcam_stream_viewer.py`.
- Split viewer serial locking from latest-frame state locking so the browser can continue reading the most recent frame while the background thread reads a batch.

Benchmarks:

| Batch size | Direct serial result |
|---:|---:|
| 4 | `4` frames in `407.8 ms`, about `9.81 fps` |
| 8 | `8` frames in `738.9 ms`, about `10.83 fps` |
| 16 | `16` frames in `1406.7 ms`, about `11.37 fps` |

Chosen live setting: batch size `8`. It gives most of the improvement without making `Stop` wait as long as a 16-frame batch.

Current viewer:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/live_lcdcam_stream_viewer.py --port /dev/cu.usbmodem14401 --listen-port 8787 --pclk-invert --interval-ms 33 --capture-timeout-ms 2500 --width 192 --height 145 --data-mode RGB332 --firmware-binary --no-source-binary --continuous-capture --stream-batch-size 8 --host-crop --crop-offset 0 --crop-width 161 --crop-height 145
```

Local URL:

- `http://127.0.0.1:8787/`

Measured browser-path result after `/api/start`:

- `transport=firmware_binary_batch`
- Browser payload: `23345` bytes after host crop
- Server capture FPS: about `10.24 fps`
- Latest-frame loop time in sampled responses: about `83..97 ms`
- Firmware capture time in sampled responses: about `24..37 ms`

Interpretation: batching confirms that command overhead was significant. The remaining ceiling is still the console transport plus per-frame LCD_CAM setup/teardown. The next speed step must either keep LCD_CAM/GDMA configured across frames or move to a binary-safe USB bulk/CDC stream that can carry arbitrary frame lengths reliably.

2026-05-09: Added viewer recovery controls for GBC power-cycle reconnect issues.

Reason: switching the GBC off/on while the capture loop is active can leave the host-side serial object or firmware capture path in a stale state. The existing `Stop` sends `SAFE_IDLE`, but reconnect recovery needed a single explicit action in the browser.

Implementation:

- Added `Recover` and `Safe Idle` buttons to `host/live_lcdcam_stream_viewer.py`.
- Added `/api/recover`.
- Recovery stops the background capture loop, closes and reopens the serial port, drains any available startup output without blocking, sends `SAFE_IDLE`, and clears host error state.
- Added automatic recovery attempt after three consecutive capture errors.
- Fixed `ProbeClient.drain_startup()` so it no longer blocks trying to read 8192 bytes when fewer bytes are available.

Verification on `http://127.0.0.1:8791/`:

- `/api/recover` returned `safe_idle_ok=true`.
- Start, Stop, then Recover worked after active capture.
- Stop returned with `running=false` and Recover returned `SAFE_IDLE` mode `lcdcam_detached_gpio_floating_input`.

Current recovery workflow for GBC power cycling:

1. Press `Stop`.
2. Press `Recover` if the next Start does not resume cleanly after switching the GBC off/on.
3. Wait for `safe_idle=true`.
4. Switch the GBC power state.
5. Press `Start`.

2026-05-09: Started turning the live viewer into a universal ESP32-P4 signal-lab browser UI while keeping the working GBC capture path intact.

Changes:

- Kept the existing `/api/frame.bin` binary frame path and RGB332 decode controls.
- Added tabs: `Dashboard`, `Live View`, `Profile`, `Signals`, and `Logs`.
- Added profile loading from `profiles/gbc_lcd.json` through `/api/profile`.
- Added an allowlisted `/api/probe-command` endpoint for safe commands: `PING`, `GET_VERSION`, `GET_PINMAP`, `EXPORT_STATS`, and `SAFE_IDLE`.
- Changed the viewer default PCLK setting to non-inverted because that is the current verified GBC edge.
- Documented the browser UI as the future lab bench for target-agnostic investigation.

Verification:

- `python3 -m py_compile host/live_lcdcam_stream_viewer.py` passed.

2026-05-09: Implemented first browser workbench investigation features.

Changes:

- Added `Safety`, `Pins`, and `Timing` tabs to `host/live_lcdcam_stream_viewer.py`.
- Added profile-backed `/api/workbench/gpios`.
- Added profile-wide static GPIO reads through `/api/workbench/read-gpios`.
- Added profile-wide edge scans through `/api/workbench/count-edges-all`.
- Added selected GPIO clock measurement through `/api/workbench/measure-clock`.
- Added timing-edge capture, summary generation, relationship analysis, and artifact writing through `/api/workbench/capture-timing`.
- Added line-clock snapshot capture through `/api/workbench/line-clocks`.
- Kept existing live capture controls and `/api/frame.bin` behavior unchanged.

Verification while GBC was active:

- `/api/workbench/gpios` returned the profile-connected GPIO list.
- `/api/workbench/read-gpios` returned input-only levels for all profile GPIOs.
- `/api/workbench/count-edges-all?duration_ms=10` returned activity for timing and RGB pins.
- `/api/workbench/measure-clock?gpio=22&duration_ms=100` returned about `1.388 MHz`.
- `/api/workbench/capture-timing?duration_ms=20` returned `1069` events with `overflow_count=0` and wrote artifacts under `captures/experiments/20260509T104246Z-gbc_lcd-timing_edges/`.
- `/api/workbench/line-clocks?marker=SPL&edge=falling&line_count=8&timeout_ms=1000` returned 8 line-clock samples and wrote artifacts under `captures/experiments/20260509T104258Z-gbc_lcd-line_clocks/`.

2026-05-09: Added a rolling pin-level timeline to the browser `Pins` tab.

Implementation:

- Timeline uses the existing profile-allowlisted `/api/workbench/read-gpios` endpoint.
- One row is drawn for each GPIO in the active target profile.
- Cells are colored by static level: high, low, or read error.
- Timeline has `Start Timeline`, `Stop Timeline`, `Clear`, and sample interval controls.
- Timeline polling stops on recovery and on main capture stop actions.

Purpose: visualize slow pin-level changes during manual investigation. It does not replace edge counts or timing captures for fast signals.

2026-05-09: Refined the browser workbench layout.

- Left side is now treated as the data/viewer surface.
- Right side is now a contextual control surface for the active tab.
- Pin read, edge scan, clock measurement, timeline parameters, timing capture parameters, and line-clock parameters were moved to the right-side controls.
- The pin timeline label was changed to `Logic-Style Level Timeline` to make clear that it resembles a logic analyzer view but is based on polling.

2026-05-09: Verified live-view data path after the workbench layout refactor.

- `/api/start` started the background capture thread.
- `/api/frame.bin` returned `23345` bytes, matching the `161x145` host-cropped RGB332 frame-period payload.
- Response metadata reported `transport=firmware_binary_batch`, `pclk_invert=false`, `start_trigger_seen=true`, `capture_us=25032`, and server capture rate about `11.37 fps` during the sample.
- `/api/stop` stopped capture and returned `running=false`.

Follow-up fix: added `/api/single-frame.bin` and changed browser `Single` buttons to use it, so `Single` forces a fresh one-shot capture instead of returning a cached latest frame when continuous mode is enabled.

2026-05-09: Added PulseView-compatible timing export path.

Changes:

- Added `host/export_pulseview.py`.
- The exporter converts `CAPTURE_TIMING_EDGES` raw JSON into VCD with `1 us` timescale.
- VCD includes timing/control signals and `red6` when present.
- Workbench timing captures now write `timing_edges.vcd` automatically in their experiment folder.

Verification:

- `python3 -m py_compile host/export_pulseview.py host/live_lcdcam_stream_viewer.py` passed.
- `python3 host/export_pulseview.py captures/experiments/20260509T104246Z-gbc_lcd-timing_edges/raw.json -o /tmp/gbc_timing_edges.vcd` created a VCD with SPS, LP, SPL, CLS, PS, and red6 definitions.

Limitation: this export is event-based timing data, not a full-rate sampled logic analyzer capture.

2026-05-09: Added and verified experimental RGB666 line-burst capture.

Changes:

- Added firmware command `CAPTURE_RGB666_LINE_BURSTS`.
- Added host tool `host/capture_rgb666_line_bursts.py`.
- Kept the existing LCD_CAM `RGB332` live-view path unchanged.

Verification:

- `python3 -m py_compile host/capture_rgb666_line_bursts.py` passed.
- ESP-IDF firmware build passed and flashed to `/dev/cu.usbmodem14401`.
- `CAPTURE_RGB666_LINE_BURSTS 16 4 1000 rising SPL 0 0 1 0 1` returned `captured_lines=4`, all line counts `16`, and `frame_sync_seen=true`.
- `python host/capture_rgb666_line_bursts.py --port /dev/cu.usbmodem14401 --timeout 30` saved `captures/decoded/rgb666_line_bursts/20260509T115056Z-rgb666_line_bursts_160x144.*`, but only `15` lines were captured before the next `SPS`.
- `python host/capture_rgb666_line_bursts.py --port /dev/cu.usbmodem14401 --timeout 30 --width 160 --height 144 --no-stop-on-next-frame` saved `captures/decoded/rgb666_line_bursts/20260509T115426Z-rgb666_line_bursts_160x144.*` with all `144` rows.

Interpretation: all connected color bits can now be preserved into a reproducible artifact as `R6/G6/B6`, but CPU polling is too slow for coherent one-frame full-resolution RGB666. The full artifact may span multiple source frames and should be used for color-bit validation, not as proof of real-time true-color capture.

2026-05-09: Added RGB666 diagnostic display mode to the browser workbench.

Changes:

- Added `/api/rgb666-frame.bin` to serve a binary `160x144x3` RGB666 buffer with capture metadata in `X-Capture-Meta`.
- Added `RGB666 Single` and `Slow RGB666 live` controls to the `Live View` panel.
- Added browser-side `RGB666` rendering while keeping the existing `RGB332` decode controls and fast stream unchanged.

Verification:

- `python3 -m py_compile host/live_lcdcam_stream_viewer.py` passed.
- `/api/rgb666-frame.bin` returned `69120` bytes.
- Metadata reported `pixel_format=RGB666`, `captured_lines=144`, `checksum=4105196`, and `transition_count=3194`.

Limitation: this mode is intentionally labeled diagnostic because it captures with `stop_on_next_frame=false`; it is slower and may mix source frames.

2026-05-09: Added and verified experimental LCD_CAM/GDMA `RGB664` capture.

Changes:

- Added firmware data mode `RGB664`.
- Added 16-bit LCD_CAM input routing for `R0-R5`, `G0-G5`, and `B2-B5`.
- Added host PNG rendering for `rgb664_r0_r5_g0_g5_b2_b5`.
- Added browser rendering support for `RGB664` two-byte samples.

Verification:

- `python3 -m py_compile host/capture_lcdcam_raw.py host/render_dvp_raw.py host/live_lcdcam_stream_viewer.py` passed.
- ESP-IDF build passed and flashed to `/dev/cu.usbmodem14401`.
- `LCDCAM_RAW_CAPTURE_BIN HIGH 2500 0 0 0 1 16 8 1 0 RGB664` returned `ok=true`, `bytes_per_sample=2`, `binary_len=256`, and `received_size=256`.
- `host/capture_lcdcam_raw.py ... --data-mode RGB664 --width 192 --height 145` saved `captures/decoded/lcdcam_raw/20260509T153425Z-lcdcam_raw_high_192x145.*` with `received_size=55680` and all descriptors complete.
- Browser workbench in `RGB664` mode returned `46690` bytes from `/api/frame.bin`, matching cropped `161x145x2`.

Interpretation: the 16-bit hardware capture path works with the existing timing model. This is the next candidate to visually compare against the stable RGB332 path. If the image is coherent, the remaining true-color problem is preserving `B0/B1` or deciding whether those low bits matter for downstream processing.

2026-05-09: Added and verified experimental LCD_CAM/GDMA `RGB565` capture.

Changes:

- Added firmware data mode `RGB565`.
- Routed LCD_CAM 16-bit samples as standard RGB565: `B1-B5`, `G0-G5`, `R1-R5`.
- Added host PNG rendering for `rgb565_upper_bits_standard`.
- Added browser rendering support for `RGB565` two-byte samples.

Verification:

- `python3 -m py_compile host/capture_lcdcam_raw.py host/render_dvp_raw.py host/live_lcdcam_stream_viewer.py` passed.
- ESP-IDF build passed and flashed to `/dev/cu.usbmodem14401`.
- `host/capture_lcdcam_raw.py ... --data-mode RGB565 --width 192 --height 145` saved `captures/decoded/lcdcam_raw/20260509T154430Z-lcdcam_raw_high_192x145.*` with `received_size=55680` and all descriptors complete.
- Browser workbench in `RGB565` mode returned `46690` bytes from `/api/frame.bin`, matching cropped `161x145x2`.

Interpretation: RGB565 works as a hardware-speed capture format. If the image is visually coherent, it should become the practical live color mode while full RGB666 remains the source-preservation research goal.

2026-05-09: Enabled batched binary streaming for `RGB565`.

Changes:

- Extended `LCDCAM_RAW_STREAM_BIN` to accept optional data mode: `LCDCAM_RAW_STREAM_BIN <frame_count> [pclk_invert] [RG44|RGB332|RGB664|RGB565]`.
- Updated browser workbench batch capture to pass the active data mode.

Verification:

- `python3 -m py_compile host/live_lcdcam_stream_viewer.py` passed.
- ESP-IDF build passed.
- First flash attempts failed because a stale browser process was still holding `/dev/cu.usbmodem14401`; after force-stopping it, flash succeeded.
- Browser command: `--data-mode RGB565 --stream-batch-size 8`.
- `/api/status` reported `server_capture_fps` around `6.3..6.7` with `consecutive_errors=0`.

Interpretation: RGB565 can now use the same batched transport strategy as RGB332. The remaining frame-rate limit is mostly per-frame LCD_CAM/GDMA setup and USB console transport overhead, not the browser polling loop.

2026-05-09: Implemented browser-assisted boot capture.

Changes:

- Added Boot Capture controls to the dashboard side panel.
- Added `/api/boot/arm`, `/api/boot/status`, and `/api/boot/stop`.
- Boot capture stops live capture, enters `SAFE_IDLE`, waits for DCLK/SPS edge activity, records RGB565 frames, and renders PNGs after recording.

Verification:

- `python3 -m py_compile host/live_lcdcam_stream_viewer.py` passed.
- Restarted RGB565 workbench at `http://127.0.0.1:8791/`.
- Dry run: `/api/boot/arm?frames=2&wait_ms=3000&probe_ms=100`.
- Result: `state=done`, `frames_captured=2`, `last_dclk_edges=5585`, `last_sps_edges=6`.
- Artifacts written under `captures/experiments/20260509T161053Z-boot_capture_rgb565/`.

Interpretation: the workflow can now arm, lock to source timing, and record a frame sequence without manual button choreography. The real test is using it with the GBC off, then powering on after arming.

2026-05-09: Added stale-frame safeguards to the live viewer.

Changes:

- The live canvas is cleared when capture is stopped.
- The `Live View` tab now shows a banner for live/single/stale state.
- `/api/frame.bin` returns HTTP `409` with `live capture is stopped; no current frame` when continuous capture is stopped.
- Frame metadata now includes `server_frame_is_live`.

Verification:

- With capture stopped, `/api/frame.bin` returned `409` and no image bytes.
- After starting capture, `/api/frame.bin` returned `46690` bytes with `server_running=true` and `server_frame_is_live=true`.

Interpretation: the browser should no longer silently display a locally retained frame as if it were live. If the canvas shows an image, the banner and metadata now indicate whether it came from a live stream or a one-shot capture.

2026-05-09: Tightened stale-frame behavior while source timing is lost.

Observation: with the GBC switched off, the live viewer could still show the last successful canvas frame while the capture thread was blocked waiting for new timing.

Changes:

- Added a browser-side freshness timer that clears the canvas if no successful frame arrives for more than `1200 ms`.
- Added server-side frame age tracking.
- `/api/frame.bin` now refuses stale frames even if the capture thread is still technically running.
- Capture errors clear the browser canvas and update the banner instead of leaving the last image visible.

Verification:

- With capture stopped, `/api/frame.bin` returns `409`.
- With capture running but no fresh frame, `/api/frame.bin` no longer returns image bytes; it returns an error such as `lcdcam_raw_capture_failed`.
- `/api/status` exposes `server_frame_age_ms` so stale live state is visible.

Interpretation: a visible image should now mean either a fresh live frame or an explicitly labeled one-shot/diagnostic frame. If the source is off or capture is stale, the canvas should clear.

2026-05-09: Added passive Power-Cycle Monitor to the browser workbench.

Changes:

- Added dashboard UI controls for `Start Monitor` / `Stop Monitor`.
- Added `/api/power-monitor/start`, `/api/power-monitor/status`, and `/api/power-monitor/stop`.
- Monitor mode stops live capture, enters `SAFE_IDLE`, then samples `DCLK`, `SPS`, `SPL`, `LP`, `PS`, and `CLS` with `COUNT_GPIO_EDGES` plus level reads.
- Each sample is classified as `off`, `clock_only`, `frame_no_line`, `line_no_frame`, `locked`, `unstable`, or `unknown`.
- Results are saved as `raw.json`, `samples.csv`, and `summary.json` under `captures/experiments/<timestamp>-power_cycle_monitor/`.

Verification:

- `python3 -m py_compile host/live_lcdcam_stream_viewer.py` passed.
- Restarted workbench at `http://127.0.0.1:8791/`.
- Dry run: `/api/power-monitor/start?duration_ms=1200&window_ms=20`.
- Artifact directory: `captures/experiments/20260509T163645Z-power_cycle_monitor/`.
- Result with source off: 2 samples, classified `off`, zero edges on all monitored signals.

Interpretation: this gives a passive diagnostic path for the GBC restart issue. The next useful experiment is a failed off-to-on attempt and a successful off-to-on attempt, compared by their state timelines.

2026-05-09: Changed the live browser stream to handle source off/on inside the normal live pipeline instead of requiring a separate workflow.

Changes:

- Added live source state tracking: `starting`, `live`, `no_signal`, and `source_detected`.
- After a frame capture failure, the host stops attempting LCD_CAM frame reads, sends `SAFE_IDLE`, and passively polls `DCLK`/`SPS`.
- When `DCLK` and `SPS` activity returns, the same live capture thread resumes LCD_CAM frame capture automatically.
- `/api/status` now reports `source_state`, `source_wait_ms`, and the last `DCLK`/`SPS` probe result.
- Restarted the workbench with a shorter live timeout: `--capture-timeout-ms 800`, so source loss should be detected faster than the previous 2500 ms timeout.

Verification:

- `python3 -m py_compile host/live_lcdcam_stream_viewer.py` passed.
- Workbench restarted at `http://127.0.0.1:8791/`.
- With source present, `/api/status` reported `source_state=live`, `server_capture_fps=6.34`, and `consecutive_errors=0`.

Interpretation: the intended steady-state model is now `GBC bus may appear/disappear -> ESP32-P4 live worker idles or captures -> browser keeps polling one stable endpoint`. The separate Power-Cycle Monitor remains useful for saving evidence, but normal off/on behavior should be debugged in the live stream first.

2026-05-09: Simplified the browser UI so Live View is the default monitor workflow.

Changes:

- Removed Boot Capture and Power-Cycle Monitor sections from the visible Dashboard controls.
- Stopped polling their status endpoints from the browser page.
- Made `Live View` the default active tab and `Live View Controls` the default side panel.
- The browser now auto-starts live capture on page load when the server is running in continuous-capture mode.
- The host server also starts the continuous capture worker at launch, so the stream is alive before the browser page finishes loading.
- Dashboard now focuses on instrument/source status and points users back to the live stream for normal off/on behavior.

Interpretation: the main tool behavior is now a monitor-style stream. Specialized capture/diagnostic backend endpoints can remain available for future scripts, but they are not the primary UI path.

2026-05-09: Hardened browser event binding after the UI cleanup.

Observation: after removing visible Boot Capture and Power-Cycle Monitor controls, the page could become unresponsive if JavaScript attempted to bind or update a removed optional element.

Changes:

- Added safe `on(id, event, handler)` event binding for UI controls.
- Converted the main button bindings to tolerate missing optional elements.
- Restarted the workbench after verification.

Verification:

- `python3 -m py_compile host/live_lcdcam_stream_viewer.py` passed.
- Served page contains the safe event binder and no visible `Boot Capture` or `Power-Cycle Monitor` sections.
- `/api/status` reported `running=true`, `source_state=live`, and about `6.1 fps`.

Follow-up fix: the served JavaScript still failed to parse because a hidden helper used a newline escape inside the Python HTML string. Replaced that with `String.fromCharCode(10)`, restarted the workbench with `python -B`, verified the served JavaScript with `node --check`, and evaluated it against a DOM stub successfully.

2026-05-09: Changed live source reacquire for boot visibility.

Observation: after switching the GBC on, the live stream recovered only after several seconds. That is too late for watching the boot animation. The previous reacquire path waited for both `DCLK` and `SPS`, then resumed batched 8-frame streaming.

Changes:

- Reacquire now polls only `DCLK` in 5 ms windows.
- As soon as DCLK activity is detected, source state becomes `clock_detected` and LCD_CAM capture is re-armed.
- The first 12 captures after reacquire are forced through the single-frame path before returning to 8-frame batch streaming. This avoids hiding early boot frames inside a completed batch.
- Browser error text now says `waiting for source` instead of presenting the live stream as stopped.

Verification:

- `python3 -m py_compile host/live_lcdcam_stream_viewer.py` passed.
- Served JavaScript passed `node --check`.
- Workbench restarted with `python -B`.
- `/api/status` reported `running=true`, `source_state=live`, and about `6.3 fps`.

2026-05-09: Normalized GBC-off / source-loss UI state.

Observation: with the GBC off, the browser could alternate between `no current live frame` and `live capture stopped`. The capture worker was still running; the inconsistent text came from separate browser/server paths describing the same source-loss condition.

Changes:

- Successful frame metadata now includes `source_state=live`.
- `/api/frame.bin` error responses now include `X-Capture-Meta` with current source state.
- Browser capture errors read that metadata before choosing the displayed state.
- The browser now uses `waiting for source` for source-off while the monitor is still running. `stopped` should be reserved for an explicit Stop action.

Verification:

- `python3 -m py_compile host/live_lcdcam_stream_viewer.py` passed.
- Restarted lower-latency live workbench with `--stream-batch-size 1` and `--capture-timeout-ms 300`.
- `/api/frame.bin` success metadata includes `source_state=live`.

2026-05-09: Investigated suspected ESP32-P4 back-powering of the unpowered GBC LCD bus.

Observation: with GBC battery/bench power disconnected and the ESP32-P4 still powered, the user measured about 1.8 V to 2.5 V on multiple connected timing/sync LCD lines. This strongly suggests the powered ESP32-P4 side is biasing or back-powering the unpowered GBC through direct GPIO connections.

Code review result:

- Capture, timing, and GPIO read paths configure pins as inputs and disable internal pull-ups/pull-downs.
- LCD_CAM capture routing explicitly attaches connected GPIOs to peripheral input signals during capture.
- The previous `SAFE_IDLE` state detached LCD_CAM but still left connected pins as floating inputs.

Change:

- Kept `SAFE_IDLE` as a floating-input state for normal recovery/debug work.
- Added explicit `ELECTRICAL_ISOLATE` and `SAFE_ISOLATE` USB commands for the stronger software isolation state: LCD_CAM detached, all known connected capture GPIOs configured as `GPIO_MODE_DISABLE`, and no internal pulls.
- Exposed those commands through the browser probe command allowlist and made browser Stop request `ELECTRICAL_ISOLATE`.

Interpretation: this removes unnecessary software-side input attachment while idle, but it does not prove the bus is electrically safe. Powered GPIO pads can still leak or clamp into an unpowered external circuit. If measured bus voltages remain high after `ELECTRICAL_ISOLATE`, hardware isolation is required.

2026-05-10: Graduated the current GBC live path into an explicit source-driver compatibility command.

Reason: the GBC source is now understood well enough for a target-specific performance path. Generic investigation commands remain valuable, but they should not be the permanent hot path once a source has a known timing/profile hypothesis.

Changes:

- Added `firmware/main/gbc_lcd_source.c` and `firmware/main/gbc_lcd_source.h`.
- Added `GBC_SOURCE_STATUS`.
- Added `GBC_SOURCE_FRAME_BIN [timeout_ms] [RGB332|RGB565] [emit_len] [pclk_invert]`.
- Updated the live backend with `--gbc-source-driver`, which uses `GBC_SOURCE_FRAME_BIN` instead of the generic LCD_CAM stream command.
- Hardened `host/gbc_probe.py` binary framing so stale binary bytes from a failed read are flushed before the next command, and JSON headers can be recovered if prefixed by binary garbage.
- Replaced large firmware binary `fwrite(stdout)` with a chunked `write()` loop and a trailing newline flush byte outside `binary_len`.

Verification:

- `python3 -m py_compile host/gbc_probe.py host/live_lcdcam_stream_viewer.py` passed.
- `scripts/build_probe_firmware.sh` passed.
- `scripts/flash_probe_firmware.sh /dev/cu.wchusbserial5A470211841` flashed successfully at `115200`.
- Native USB `/dev/cu.usbmodem14301` answered `PING` after WCH flashing.
- Direct source-driver frame read succeeded:

```json
{"binary_len":46690,"payload_len":46690,"data_mode":"RGB565","width":192,"height":145,"capture_us":36973,"ok":true}
```

- Browser backend restarted at `http://127.0.0.1:8791/` using `--gbc-source-driver`.
- `/api/status` reported `source_state=live`, `server_frame_count=57`, and `server_capture_fps=5.43`.
- `/api/frame.bin` returned `46690` bytes, matching the current RGB565 source-driver frame payload.

Interpretation: this is still the compatibility source-driver path because it starts/stops LCD_CAM per frame. It proves the source-specific command and binary transport are stable again. The next FPS step is a persistent LCD_CAM/GDMA source driver with compact binary frame headers, not more browser/UI polling changes.

2026-05-10: Added and benchmarked GBC source-driver batch streaming.

Reason: the live browser path still ran one source-driver command per frame. A target-specific stream command removes host command round trips while preserving the known-good GBC capture settings.

Changes:

- Added `GBC_SOURCE_STREAM_BIN <frame_count> [timeout_ms] [RGB332|RGB565] [emit_len] [pclk_invert]`.
- Added `host/gbc_probe.py binary-sequence` for direct multi-frame binary protocol tests.
- Updated `host/live_lcdcam_stream_viewer.py` so `--gbc-source-driver --stream-batch-size N` uses `GBC_SOURCE_STREAM_BIN`.
- Built and flashed normal probe firmware through WCH UART at `115200`.

Verification:

- `python3 -m py_compile host/gbc_probe.py host/live_lcdcam_stream_viewer.py` passed.
- `scripts/build_probe_firmware.sh` passed.
- `scripts/flash_probe_firmware.sh /dev/cu.wchusbserial5A470211841` passed.
- Native USB `PING` passed after stopping the old backend process.
- Direct `GBC_SOURCE_STREAM_BIN 8 300 RGB565 0 0` returned `8/8` frames.
- Direct `GBC_SOURCE_STREAM_BIN 32 300 RGB565 0 0` returned `32/32` frames, `46690` bytes per frame, `4.83 s` elapsed, `6.626 fps`.
- Clean browser restart at `http://127.0.0.1:8791/` with `--stream-batch-size 8` returned fresh `46690` byte frames and `/api/status` reported about `7.1 fps`.

Important observation: an attempted RGB332 direct benchmark was run while the browser backend still held the serial port, causing a pyserial multiple-access/disconnect error and stale browser state. The backend was killed and restarted cleanly. Do not run direct serial benchmarks while the browser backend is active.

Interpretation: batching improved the browser path modestly, but it did not change the main limit. RGB565 payloads are `46690` bytes; at 60 fps this requires about `2.8 MB/s` before metadata. The current native USB Serial/JTAG command stream is measuring far below that. The next FPS work must explicitly separate capture time from USB write/read throughput and likely needs a better data plane such as TinyUSB bulk/vendor transport.

2026-05-10: Added capture-free USB Serial/JTAG synthetic throughput benchmark.

Reason: before changing LCD_CAM/GDMA again, the project needed to separate capture speed from USB data-plane speed. The benchmark must be electrically safe and independent of the GBC bus.

Changes:

- Added `USB_BENCH_STREAM_BIN <frame_count_1_to_256> <payload_len_0_to_262144>`.
- The command emits JSON frame headers and deterministic binary payloads.
- It does not configure GPIO, LCD_CAM, DMA, or any source pins.
- It uses the same host `binary-sequence` reader as frame streaming, so it tests the real command/data parser path.

Verification:

- `python3 -m py_compile host/gbc_probe.py host/live_lcdcam_stream_viewer.py` passed.
- `scripts/build_probe_firmware.sh` passed.
- `scripts/flash_probe_firmware.sh /dev/cu.wchusbserial5A470211841` passed.
- Native USB `PING` on `/dev/cu.usbmodem14301` returned `PONG`.

Benchmarks:

- `USB_BENCH_STREAM_BIN 64 46690`: `64/64` payloads, `2988160` bytes total, `7.086 s`, `9.032 fps`, about `0.42 MB/s`.
- `USB_BENCH_STREAM_BIN 128 23345`: `128/128` payloads, `2988160` bytes total, `7.111 s`, `18.0 fps`, about `0.42 MB/s`.

Interpretation: native USB Serial/JTAG is the dominant throughput limit for the current RGB565 live monitor. A `46690` byte frame at 59-60 FPS needs about `2.8 MB/s` before overhead, while the measured transport-only path is about `0.42 MB/s`. The next safe step is not another capture timing tweak; it is an isolated TinyUSB CDC/vendor benchmark that first streams synthetic payloads only, while preserving WCH recovery and the current native USB control path.

2026-05-10: Created and tested isolated TinyUSB CDC benchmark firmware.

Reason: evaluate a possible high-rate data plane without changing the working GBC source-driver firmware or touching any capture GPIO.

Changes:

- Added `experiments/tinyusb_bench/`.
- Added `scripts/build_tinyusb_bench.sh`.
- Added `scripts/flash_tinyusb_bench.sh`.
- Implemented a no-GPIO TinyUSB CDC firmware with:
  - `PING`
  - `GET_VERSION`
  - `USB_BENCH_STREAM_BIN <frame_count> <payload_len>`

Verification:

- Initial sandboxed build failed because ESP-IDF's component manager needed process access through `psutil`.
- Escalated build succeeded.
- Component manager fetched `espressif/esp_tinyusb 1.7.6~2` and `espressif/tinyusb 0.19.0~3` into the experiment directory.
- `scripts/flash_tinyusb_bench.sh /dev/cu.wchusbserial5A470211841` flashed successfully.
- After flashing, visible serial candidates were `/dev/cu.usbmodem14301` and `/dev/cu.usbmodem5A470211841`.
- Neither candidate answered `PING`.
- Normal probe firmware was restored with `scripts/flash_probe_firmware.sh /dev/cu.wchusbserial5A470211841`.
- Normal firmware `PING` on `/dev/cu.usbmodem14301` returned `PONG`.

Interpretation: TinyUSB firmware build/flash is viable, but the current board/cable setup does not expose a responding TinyUSB CDC data path. The likely cause is hardware routing: the two connected cables appear to provide USB Serial/JTAG and WCH UART, while TinyUSB uses ESP32-P4 USB-OTG. Before implementing vendor/bulk transport or moving GBC frames, confirm the board schematic/connector mapping for USB-OTG D+/D- and whether the relevant port is OTG2.0 or OTG1.1.

2026-05-10: Added pipeline proof commands for internal FPS evidence.

Reason: the project needs to prove ESP32-P4 internal capture/processing/output capacity separately from the known USB/browser frame-stream ceiling.

Changes:

- Added `firmware/main/pipeline_bench.c`.
- Added `PIPELINE_BENCH <frame_count> [width] [height] [bytes_per_pixel] [target_fps]`.
- Added `GBC_SOURCE_BENCH <frame_count> [timeout_ms] [RGB332|RGB565] [pclk_invert]`.
- `PIPELINE_BENCH` is synthetic and does not touch GPIO, LCD_CAM, GDMA, or the GBC bus.
- `GBC_SOURCE_BENCH` captures real source frames but returns only counters, not image payloads.

Verification:

- `scripts/build_probe_firmware.sh` passed.
- `scripts/flash_probe_firmware.sh /dev/cu.wchusbserial5A470211841` passed.
- Native USB `PING` passed.
- `PIPELINE_BENCH 300 160 144 2 60` returned `target_met=true`, `dropped_frames=0`, `source_fps=60.124`, `processed_fps=60.124`, `output_fps=60.124`, and `max_frame_us=1833`.
- `PIPELINE_BENCH 300 160 144 2 0` returned about `550.9 fps`, showing internal CPU/memory headroom for a simple 160x144 RGB565 processing loop.
- `GBC_SOURCE_BENCH 5 300 RGB565 0` returned `5/5` captures, `capture_fps=30.801`, `avg_capture_us=32458`, and `max_capture_us=33484`.

Interpretation: internal synthetic processing is not the immediate blocker for 60 FPS at GBC-sized RGB565 frames. The current GBC source compatibility path is also faster than the browser stream, but still not full-rate because it starts/stops LCD_CAM per frame. The next real performance work should be a persistent source-driver capture loop with ring-buffer telemetry before adding panel output or trying to stream every frame to the browser.

2026-05-10: Added real-source pipeline benchmark telemetry.

Reason: prove the full firmware source -> process -> output-sink chain with the actual GBC input, while still avoiding full USB image payloads.

Changes:

- Added `firmware/main/source_pipeline_bench.c`.
- Added `firmware/main/source_pipeline_bench.h`.
- Added `GBC_PIPELINE_BENCH <frame_count> [timeout_ms] [RGB332|RGB565] [pclk_invert] [target_fps]`.
- The command reports `performance_path=per_frame_lcdcam_setup_teardown` and `next_performance_path=persistent_lcdcam_gdma_ring`.

Verification:

- `scripts/build_probe_firmware.sh` passed.
- `scripts/flash_probe_firmware.sh /dev/cu.wchusbserial5A470211841` passed.
- Native USB `PING` passed.
- `GBC_PIPELINE_BENCH 5 300 RGB565 0 60` returned `5/5` captured/processed/output frames, `capture_fps=28.057`, `avg_capture_us=33454`, `avg_process_us=2170`, `target_met=false`, and `budget_miss_frames=5`.
- `GBC_PIPELINE_BENCH 10 300 RGB332 0 60` returned `10/10` captured/processed/output frames, `capture_fps=30.543`, `avg_capture_us=31646`, `avg_process_us=1084`, `target_met=false`, and `budget_miss_frames=10`.

Interpretation: real-source pipeline processing itself is only about `1-2 ms` per frame, but the compatibility capture step is about `31-33 ms` per frame. The next performance step should be a persistent LCD_CAM/GDMA source driver that reuses buffers/descriptors and reports dropped frames, overruns, sync loss, and ring occupancy.

2026-05-10: Added LCD_CAM/GDMA setup-reuse benchmark and checked ESP32-P4 USB Host documentation.

Reason: test whether the real-source FPS limit is mainly per-frame LCD_CAM/GDMA allocation/configuration, and verify whether ESP-IDF USB Host is relevant to high-rate computer streaming.

Changes:

- Added `lcdcam_raw_capture_loop()` to reuse LCD_CAM/GDMA allocation, descriptors, buffer, and routing across a finite capture batch.
- Added `source_pipeline_bench_run_persistent()`.
- Added command `GBC_PIPELINE_BENCH_PERSIST`.
- Checked Espressif's ESP32-P4 USB Host documentation.

Verification:

- `scripts/build_probe_firmware.sh` passed.
- `scripts/flash_probe_firmware.sh /dev/cu.wchusbserial5A470211841` passed.
- Native USB `PING` passed.
- `GBC_PIPELINE_BENCH_PERSIST 3 300 RGB565 0 60` returned `3/3` frames, `capture_fps=30.756`, `avg_capture_us=24889`, and `target_met=false`.
- `GBC_PIPELINE_BENCH_PERSIST 10 300 RGB565 0 60` returned `10/10` frames, `capture_fps=28.939`, `avg_capture_us=27807`, and `target_met=false`.
- `GBC_PIPELINE_BENCH_PERSIST 10 300 RGB332 0 60` returned `10/10` frames, `capture_fps=29.658`, `avg_capture_us=30324`, and `target_met=false`.
- `SAFE_IDLE` returned `mode=lcdcam_detached_gpio_floating_input`.

Interpretation: reusing setup improves the shortest run but does not produce sustained 60 FPS. The remaining limit is likely the current start/wait/byte-count EOF pattern. The next capture implementation should be a true continuous descriptor/ring stream. ESP32-P4 USB Host is useful for future external USB devices, but not for sending frames to the computer; the high-rate computer data plane still needs USB device mode.

2026-05-10: Added ISR rearm raw-chunk benchmark.

Reason: test a lower-level capture path where GDMA EOF immediately rearms the next buffer, closer to Espressif's DVP driver pattern.

Changes:

- Added `lcdcam_raw_rearm_bench()`.
- Added command `GBC_REARM_BENCH <chunk_count> [timeout_ms] [RGB332|RGB565] [pclk_invert]`.
- The command returns raw chunk counters only; chunks are frame-sized but not guaranteed to be frame-aligned.

Verification:

- First run of `GBC_REARM_BENCH 2 300 RGB565 0` caused a `usb_protocol` task stack protection fault because the timing array was stack allocated. The board rebooted cleanly.
- Moved chunk timing telemetry to heap allocation.
- Rebuilt and reflashed successfully.
- `PING` passed.
- `GBC_REARM_BENCH 2 300 RGB565 0` returned `2/2` chunks, `failed_rearms=0`, `chunk_fps=34.813`, `avg_chunk_us=19978`, and `payload_mbytes_per_s=1.938`.
- `GBC_REARM_BENCH 8 300 RGB565 0` returned `8/8` chunks, `failed_rearms=0`, `chunk_fps=44.397`, `avg_chunk_us=19935`, and `payload_mbytes_per_s=2.472`.
- `GBC_REARM_BENCH 8 300 RGB332 0` returned `8/8` chunks, `failed_rearms=0`, `chunk_fps=49.139`, `avg_chunk_us=19914`, and `payload_mbytes_per_s=1.368`.
- `SAFE_IDLE` returned `mode=lcdcam_detached_gpio_floating_input`.

Interpretation: ISR rearm works and improves raw capture throughput over the previous benchmark path, but frame-sized chunk time remains around `20 ms`. The next implementation should keep immediate rearm but add source phase awareness using `SPS` so accepted ring entries represent actual frames rather than arbitrary byte-count chunks.

2026-05-10: Added RGB565-only frame-phase rearm benchmark.

Reason: continue performance work on the color mode that visually works, and avoid repeating RGB332 checks now that RGB332 timing has shown no useful difference.

Changes:

- Added `GBC_FRAME_REARM_BENCH`.
- Restricted the rearm command handler to `RGB565` only.
- Added start-trigger telemetry and comparison against a `59.73 Hz` frame period.
- `GBC_FRAME_REARM_BENCH` starts after `SPS` rising then `SPL` falling.

Verification:

- `scripts/build_probe_firmware.sh` passed.
- `scripts/flash_probe_firmware.sh /dev/cu.wchusbserial5A470211841` passed.
- Native USB `PING` passed.
- `GBC_FRAME_REARM_BENCH 4 300 RGB565 0` returned `4/4` chunks, `start_trigger_seen=true`, `failed_rearms=0`, `chunk_fps=41.538`, `avg_chunk_us=19758`, and `avg_chunk_vs_expected_pct=118.0`.
- `GBC_FRAME_REARM_BENCH 8 300 RGB565 0` returned `8/8` chunks, `start_trigger_seen=true`, `failed_rearms=0`, `chunk_fps=43.953`, `avg_chunk_us=19897`, `max_chunk_us=20767`, and `avg_chunk_vs_expected_pct=118.8`.
- `SAFE_IDLE` returned `mode=lcdcam_detached_gpio_floating_input`.

Interpretation: source-phase start works and immediate rearm remains stable. The frame-sized chunk remains too slow for one 59.73 Hz period. The likely next test is an RGB565 byte-count/window sweep around `46080` bytes visible and `46690` bytes emitted instead of the current full `55680` byte capture window.

2026-05-10: Tested capture-card style streaming over the current USB path.

Reason: answer whether the existing USB connection can be used like a simple GBC capture card stream.

Command:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/gbc_probe.py --port /dev/cu.usbmodem14301 --timeout 30 binary-sequence "GBC_SOURCE_STREAM_BIN 16 300 RGB565 46690 0" --count 16 --output-dir captures/experiments/current_usb_capture_card_smoke
```

Result:

- `16/16` RGB565 payloads received.
- Payload length was `46690` bytes per frame.
- Total payload was `747040` bytes.
- Host delivered rate was `5.975 fps`.
- Captures were written to `captures/experiments/current_usb_capture_card_smoke/`.
- `SAFE_IDLE` returned `mode=lcdcam_detached_gpio_floating_input`.

Interpretation: the current native USB Serial/JTAG connection can do a capture-card-style binary stream, but only at low FPS. Full-rate RGB565 still needs both the faster source path and a better USB-device data plane.

2026-05-10: Tried stripped capture-card blob stream on current USB.

Reason: test whether removing per-frame JSON and sending one compact binary response improves capture-card-style streaming over the USB connection currently available.

Changes:

- Added temporary command `GBC_CAPCARD_STREAM_BIN`.
- The command emits one JSON preamble, then binary frame records with:
  - 4-byte magic `GBCF`
  - frame index
  - ok flag
  - capture time in microseconds
  - checksum
  - RGB565 payload
- Retested after changing payload writes to 1024-byte chunks with per-frame flush/yield.

Results:

- `GBC_CAPCARD_STREAM_BIN 16 300 46690 0` expected `747360` bytes and received `747298` bytes before timeout.
- `GBC_CAPCARD_STREAM_BIN 4 300 46690 0` expected `186840` bytes and repeatedly received `186816` bytes.
- `GBC_CAPCARD_STREAM_BIN 1 300 46690 0` expected `46710` bytes and received `46656` bytes.
- `SAFE_IDLE` returned `mode=lcdcam_detached_gpio_floating_input`.

Interpretation: the current USB Serial/JTAG stdio path is not reliable for this single-large-blob streaming shape. The earlier per-frame binary sequence is slower but robust. Do not treat stdio blob streaming as the final capture-card transport.

2026-05-10: Added first destination GPIO lab controls.

Reason: begin SPI LCD destination research without breaking the working GBC source/live-view path. Destination pins need a professional ownership and control model before panel-driver code is allowed to drive them.

Changes:

- Added firmware module `destination_gpio_lab`.
- Added `DEST_GPIO_STATUS`, `DEST_GPIO_VALIDATE`, `DEST_GPIO_CLAIM`, `DEST_GPIO_SET`, `DEST_GPIO_PULSE`, `DEST_GPIO_RELEASE`, and `DEST_GPIO_RELEASE_ALL`.
- Destination GPIOs are never configured at boot.
- Destination claims reject the current GBC source GPIOs.
- `SAFE_IDLE` and `ELECTRICAL_ISOLATE` now release any destination GPIO claims before applying source-side idle/isolation.
- Added backend endpoints under `/api/destination/gpio/*`.
- Enabled Destination tab controls for validate, claim, low, high, pulse, and release.

Verification:

- `./scripts/build_probe_firmware.sh` passed.
- `cd host/workbench/frontend && npm run build` passed.
- `python3 -m py_compile host/live_lcdcam_stream_viewer.py` passed.

Interpretation: the project now has an explicit lab-mode destination pin-control layer. This is not yet an SPI LCD driver; it is the controlled pin-ownership and probing phase before SPI output experiments.

2026-05-10: Switched first SPI LCD firmware path from ST7789-specific to generic ST7796S/ILI9486-style MIPI-DCS.

Reason: the connected SPI LCD is now believed to be ST7796S or ILI9486, so running an ST7789 panel driver would be a poor experiment and could hide controller-specific failures.

Changes:

- Replaced ST7789 panel-driver dependency with generic `esp_lcd_panel_io_spi()` command/color transfers.
- Added a conservative MIPI-DCS init path: hardware reset, software reset, sleep out, RGB565 color mode, memory access control, inversion off, display on.
- Added address-window and `RAMWR` helpers for RGB565 full-screen fill and color bars.
- Updated the destination profile to 480x320, ST7796S candidate, RGB565, GPIO52/29/53/31/28 for CS/RESET/D-C/SDI/SCK.
- Documented the unresolved ST7796S vs ILI9486 question and the risk that some ILI9486 SPI modules require RGB666/18-bit pixel transfers.

Verification:

- `python3 -m json.tool profiles/spi_lcd_destination.json` passed.
- `./scripts/build_probe_firmware.sh` passed.
- `cd host/workbench/frontend && npm run build` passed.
- `./scripts/flash_probe_firmware.sh /dev/cu.wchusbserial5A470211841` passed.
- Workbench restarted at `http://127.0.0.1:8791/`.
- `/api/status` reported `source_state=live` and about `5.43 fps`.
- `/api/destination/spi/status` reported `state=safe_off`, controller `st7796s_ili9486_mipi_dcs`, resolution `480x320`, SPI2 at `10 MHz`, pins `CS=52`, `RESET=29`, `D/C=53`, `MOSI=31`, `SCLK=28`.

Interpretation: the device is ready for a first controlled SPI LCD destination bring-up. The panel remains safe-off until `DEST_SPI_LCD_INIT` is explicitly triggered from the Destination tab or API.

2026-05-10: Added SPI destination signal-burst command for scope/debug visibility.

Reason: `DEST_SPI_LCD_STATUS` intentionally produces no panel-pin traffic, and a single color-bar transfer can be too brief to observe reliably. The user observed that init caused visible activity but status/color-bar probing did not show changing signals.

Changes:

- Added `DEST_SPI_LCD_SIGNAL_BURST <duration_ms>`.
- Added backend endpoint `/api/destination/spi/signal-burst?duration_ms=5000`.
- Added Destination tab button `SIGNAL_BURST 5s`.
- The burst repeatedly writes alternating RGB565 data for the requested duration, so `SCK`, `SDI/MOSI`, `CS`, and `D/C` should be observable for several seconds after `DEST_SPI_LCD_INIT`.

Verification:

- `./scripts/build_probe_firmware.sh` passed.
- `cd host/workbench/frontend && npm run build` passed.
- `python3 -m py_compile host/live_lcdcam_stream_viewer.py` passed.
- `./scripts/flash_probe_firmware.sh /dev/cu.wchusbserial5A470211841` passed.
- Workbench restarted at `http://127.0.0.1:8791/`.
- `/api/destination/spi/status` reported `state=safe_off` after reset.
- `/api/status` reported live source capture running at about `5.42 fps`.

Interpretation: use `DEST_SPI_LCD_INIT` first, then `SIGNAL_BURST 5s` while probing SPI lines. If the burst returns `ok=true` but no pin activity is visible, the next suspect is board-header GPIO mapping or probing point, not panel-controller initialization.

2026-05-10: First visible SPI LCD RAM-write evidence.

Observation:

- `DEST_SPI_LCD_INIT` produced a visible dim/change effect.
- `DEST_SPI_LCD_CLEAR565BE f800` and `DEST_SPI_LCD_CLEAR666 ff0000` returned `ok=true`.
- The screen showed a thick gray bar on the left side and a blue section on the right side instead of a full red fill.

Interpretation:

- SPI RAM writes are reaching the panel.
- The previous `480x320` address window is likely wrong for the controller memory.
- ST7796S/ILI9486 modules are commonly addressed as `320x480`; landscape should be treated as rotation, not as native 480-column memory.

Change:

- Updated the first destination SPI LCD firmware/profile geometry from `480x320` to native `320x480`.

Follow-up:

- After the `320x480` change, `DEST_SPI_LCD_CLEAR666 ff0000` produced a full blue screen.
- `DEST_SPI_LCD_CLEAR666 0000ff` produced a full red screen.
- This confirms full-frame writes work and the controller/panel color order is BGR under the initial MADCTL setting.
- Updated firmware init to set MADCTL BGR bit (`0x08`) and updated the destination profile color order to `bgr`.
- After the BGR fix, `DEST_SPI_LCD_CLEAR666 ff0000` produced full red.
- Replaced the older RGB565 test-pattern path with RGB666 patterns:
  - `DEST_SPI_LCD_TEST_PATTERN orientation`
  - `DEST_SPI_LCD_TEST_PATTERN color_bars`
- Flashed and ran the orientation pattern successfully; firmware returned `ok=true`.
- Retested RGB565 after geometry and BGR were fixed:
  - `DEST_SPI_LCD_CLEAR565BE f800`, `07e0`, and `001f` returned `ok=true`.
  - The panel still showed the previous orientation pattern, so RGB565 is not a working visible update path with the current init sequence.
  - `DEST_SPI_LCD_CLEAR666 0000ff` returned `ok=true` and is the working pixel path to continue with.
- Updated the destination profile/documentation from RGB565 to RGB666 as the active destination pixel format.
- Added and flashed a dedicated RGB565 color-bar test, `DEST_SPI_LCD_TEST_PATTERN565`.
- `DEST_SPI_LCD_TEST_PATTERN565` returned `ok=true`, but the panel showed only dim gray.
- Sent the RGB666 color-bar pattern afterward; firmware returned `ok=true`, restoring the working display path.
- Conclusion: this module should be treated as RGB666-only for now. RGB565 is not just a color-order problem; it does not render useful pixels with the current command sequence.

2026-05-10: Added first one-shot GBC source-to-SPI LCD destination path.

Reason: prove the ESP32-P4 can act as the whole pipeline without sending the image through USB/browser first.

Implementation:

- Added `DEST_SPI_LCD_SHOW_GBC_FRAME [timeout_ms] [pclk_invert]`.
- The command captures one GBC frame with the existing GBC LCD source driver in RGB565 mode.
- It uses the first `160x144` visible pixels from the `161x145` captured stream.
- It converts RGB565 to RGB666 on the ESP32-P4.
- Initial version scaled the image 2x to `320x288` and drew it centered vertically at `x=0`, `y=96` on the `320x480` SPI LCD.
- The user correctly flagged scaling as processing, so the default command was changed to 1:1 output.
- Current version draws the `160x144` source 1:1 at `x=80`, `y=168`, with only the required RGB565-to-RGB666 color conversion.

Result:

- `DEST_SPI_LCD_INIT` returned `ok=true`.
- First 2x run: `DEST_SPI_LCD_SHOW_GBC_FRAME 300 0` returned `ok=true`, capture completed in `35684 us`, SPI draw completed in `396226 us`.
- 1:1 run: `DEST_SPI_LCD_SHOW_GBC_FRAME 300 0` returned `ok=true`, capture completed in `32296 us`, SPI draw completed in `344246 us`.
- Source was reported as `160x144`, stride `161`, RGB565.
- Destination was reported as `320x480`, RGB666, scale `1`, offset `x=80`, `y=168`.

Interpretation: one-shot source-to-destination drawing works from the firmware perspective. The image is visible but rotated/flipped due to destination orientation, which should be handled as destination controller orientation where possible rather than source image processing. Current draw time is slow because it writes many small SPI transactions at 10 MHz; the next optimization is larger batched line conversion and higher SPI clock testing.

2026-05-10: Resolved SPI LCD destination orientation with controller MADCTL.

Tests:

- `MADCTL=0x28` made the mirror correct but image upside down.
- `MADCTL=0x68` made orientation upright but mirrored.
- `MADCTL=0xA8` was upside down and flipped.
- `MADCTL=0xE8` produced the correct orientation.

Change:

- Set default SPI LCD init MADCTL to `0xE8`.
- Updated the destination profile with `madctl=0xe8`.
- Changed one-shot GBC draw to clear the full destination before drawing the 1:1 frame, so stale pixels from earlier destination tests do not remain visible.

2026-05-10: Started source-straight GBC-to-SPI LCD mirror benchmark.

Reason: estimate the practical ESP32-P4 source-to-destination path when USB/browser live view is removed. For this benchmark the destination must not rotate, mirror, scale, or otherwise fix geometry; only the necessary RGB565 source to RGB666 destination color conversion is allowed.

Changes:

- Added `DEST_SPI_LCD_MIRROR_BENCH [frames] [timeout_ms] [pclk_invert]`.
- Added a no-clear 1:1 draw path so repeated frames do not spend time clearing the full panel.
- Set the benchmark destination MADCTL to `0x08`, meaning BGR color order only. This intentionally removes the earlier `0xE8` orientation fix.
- Tested SPI destination clocks:
  - `10 MHz`: `10/10` frames, elapsed `1173950 us`, about `8.5 fps`, average capture `34234 us`, average draw `83152 us`.
  - `20 MHz`: `10/10` frames, elapsed `845290 us`, about `11.8 fps`, average capture `29563 us`, average draw `54957 us`.
  - `20 MHz` with 40-line batched 1:1 writes: `30/30` frames, elapsed `2001759 us`, about `15.0 fps`, average capture `41764 us`, average draw `24954 us`.
  - `26 MHz`: rejected by the ESP-IDF SPI LCD path during init with `ESP_ERR_INVALID_ARG`.
  - `30 MHz`: rejected by the ESP-IDF SPI LCD path during init with `ESP_ERR_INVALID_ARG`.
  - `40 MHz`: rejected by the ESP-IDF SPI LCD path during init with `ESP_ERR_INVALID_ARG`.

Interpretation: the firmware-only path is already faster than the browser stream. The current GPIO-matrix SPI destination is proven at `20 MHz`; `26/30/40 MHz` are not accepted by this ESP-IDF SPI LCD configuration. Batching reduced draw time from about `55 ms` to about `25 ms`, shifting the dominant cost back toward source capture. This benchmark keeps source geometry straight and performs only RGB565-to-RGB666 conversion.

2026-05-10: Added and flashed early production mirror firmware.

Reason: test what pure firmware can do without the browser workbench or USB command server in the frame path.

Changes:

- Added compile-time mode `GBC_P4_PRODUCTION_MIRROR`.
- Added `production_mirror.c` / `production_mirror.h`.
- Added reusable `destination_spi_lcd_init()` and `destination_spi_lcd_safe_off()` APIs so production code does not need to call command handlers.
- Added `scripts/build_production_mirror.sh`.
- Added `scripts/flash_production_mirror.sh`.
- Production mode boots directly into GBC source capture -> SPI LCD draw.
- Production mode keeps source geometry straight: no rotation, no mirror, no scaling. It only converts RGB565 source pixels to RGB666 destination bytes.

Verification:

- `./scripts/build_probe_firmware.sh` passed for normal lab firmware.
- `./scripts/build_production_mirror.sh` passed for production firmware.
- `./scripts/flash_production_mirror.sh /dev/cu.wchusbserial5A470211841` passed.
- Metrics were read from USB Serial/JTAG `/dev/cu.usbmodem14401`.

Observed production metrics:

```json
{"mode":"production_mirror","frames":15,"fps_x1000":14931,"avg_capture_us":41953,"avg_draw_us":24986,"max_capture_us":41985,"max_draw_us":24997,"capture_failures":0,"draw_failures":0}
```

Interpretation: production mirror mode is stable at about `14.9 fps` with zero failures in the sampled window. This confirms the pure firmware path behaves like the command benchmark. The next performance target is source capture, then destination transport bandwidth.

2026-05-10: Improved production mirror with overlapped source capture and SPI drawing.

Reason: the initial production loop captured one frame and then drew one frame serially. That underused the ESP32-P4 because source capture and destination transfer did not overlap.

Changes:

- Changed production mirror into a producer/consumer pipeline with three DMA-capable frame slots.
- Kept the LCD_CAM/GDMA source capture configured across a window of frames.
- Pinned the draw task separately from the capture task.
- Preserved the production policy: no rotation, no mirror, no scaling, and only RGB565 source to RGB666 destination color conversion.

Best observed metrics after flashing:

```json
{"mode":"production_mirror_overlap","displayed":30,"captured":30,"fps_x1000":29863,"avg_capture_us":23333,"avg_draw_us":25076,"max_capture_us":23389,"max_draw_us":25120,"dropped_frames":0,"capture_failures":0,"draw_failures":0,"capture_error":"none","draw_error":"none"}
```

Interpretation: overlap nearly doubled the production path from about `14.9 fps` to about `29.86 fps` with zero drops. This is real progress, but it is still below the GBC frame rate. Current capture time is about `23.3 ms`, and current destination draw time is about `25.1 ms`; both are longer than the roughly `16.7 ms` native frame period.

2026-05-10: Tested raw write-only SPI destination backend and disabled it as the default.

Reason: determine whether the ESP-IDF LCD panel IO wrapper was the reason higher SPI clocks were rejected on the current SPI LCD wiring.

Changes:

- Added an experimental raw SPI backend inside the SPI LCD destination module.
- It manually drives D/C and uses `SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_NO_DUMMY`.
- It tries `40 MHz`, `30 MHz`, `26 MHz`, then `20 MHz`.

Observed metrics:

```json
{"mode":"production_mirror_overlap","displayed":15,"captured":15,"fps_x1000":14931,"avg_capture_us":26790,"avg_draw_us":32511,"max_capture_us":27033,"max_draw_us":32531,"dropped_frames":0,"capture_failures":0,"draw_failures":0,"capture_error":"none","draw_error":"none"}
```

Interpretation: the raw SPI backend did not improve throughput on the current wiring and reduced practical output to about `14.9 fps`, with draw time increasing to about `32.5 ms`. The known-good production firmware was restored to the ESP LCD DMA wrapper at `20 MHz`, returning the board to about `29.86 fps`.

Bandwidth note: the current SPI LCD path uses RGB666 destination writes, so a `160x144` source frame costs `69120` payload bytes. At `59.7 fps`, that is about `33.0 Mbit/s` before command and transaction overhead. A `20 MHz` one-bit SPI link cannot carry that full-rate stream. The next destination performance path is either native SPI IO_MUX rewiring with a proven higher clock, a working RGB565 panel mode, or a wider destination interface such as parallel RGB/I80/QSPI.

2026-05-10: Corrected production source stride and added on-screen FPS overlay.

Reason: the faster overlapped production path showed duplicated GAME BOY letters on the SPI LCD, while a slower test path did not. The production draw path was interpreting the DMA capture buffer as `161` samples per row even though the LCD_CAM capture buffer is allocated as `192` samples per row. That is a stride mismatch and can create repeated/sheared image content.

Changes:

- Changed production mirror draw calls to use `GBC_LCD_SOURCE_CAPTURE_WIDTH` as the source row stride.
- Added `destination_spi_lcd_draw_fps_overlay()`.
- Production firmware now draws a small `FPS` label in the top-left corner after every frame using the latest one-second metric window.

Verification:

- `./scripts/build_production_mirror.sh` passed.
- `./scripts/flash_production_mirror.sh /dev/cu.wchusbserial5A470211841` passed.
- Serial metrics after flashing remained about `29.86 fps` with zero drops:

```json
{"mode":"production_mirror_overlap","displayed":30,"captured":30,"fps_x1000":29864,"avg_capture_us":23128,"avg_draw_us":29869,"max_capture_us":23204,"max_draw_us":29907,"dropped_frames":0,"capture_failures":0,"draw_failures":0,"capture_error":"none","draw_error":"none"}
```

Interpretation: on-screen FPS costs a few milliseconds of SPI draw time but does not reduce the current displayed frame count because the pipeline remains overlapped and source/destination timing is still around the same effective cadence. Visual confirmation is still required to determine whether the duplicated-letter defect was fully caused by the stride mismatch or whether an additional frame-phase issue remains.

2026-05-11: Restored known-good SPI LCD destination baseline after failed IO_MUX rewire.

Reason: the SPI LCD was rewired from the previous working GPIO-matrix pins to the ESP32-P4 SPI2 IO_MUX pins in an attempt to improve destination bandwidth. The rewire proved that `CS=7`, `MOSI=8`, and `SCK=9` can produce clean SPI waveforms, but the LCD stayed white. Returning to the old physical wiring also stayed white until the firmware routing flag was corrected.

Known-good restored wiring:

- `CS -> GPIO52`
- `SDI/MOSI -> GPIO31`
- `SCK -> GPIO28`
- `RESET -> GPIO29`
- `D/C -> GPIO53`

Firmware baseline restored:

- ESP LCD DMA wrapper path enabled.
- Raw SPI experiment disabled.
- `20 MHz` SPI clock.
- RGB666 destination writes.
- GPIO-matrix SPI routing. `SPICOMMON_BUSFLAG_IOMUX_PINS` must not be used with `GPIO52/31/28`.

Verification:

- `./scripts/build_production_mirror.sh` passed.
- Flash through `/dev/cu.wchusbserial5A470211841` passed after the native USB port briefly failed with a stub checksum error.
- User confirmed color output returned on the SPI LCD.

Interpretation: the old destination wiring remains the recovery baseline. The failed IO_MUX experiment should not be mixed into the production mirror or normal lab firmware until it is isolated as a separate destination profile/test firmware. The exact firmware regression was leaving `SPICOMMON_BUSFLAG_IOMUX_PINS` enabled while using GPIO-matrix pins.

2026-05-11: Ran isolated source-ingress benchmark firmware.

Purpose: test the GBC source capture path without lab-mode interference from browser streaming, SPI LCD destination drawing, TinyUSB, PNG rendering, or frame payload streaming.

Firmware:

- App: `experiments/source_ring_bench/`
- Build script: `./scripts/build_source_ring_bench.sh`
- Flash script: `./scripts/flash_source_ring_bench.sh`
- Flash port used: `/dev/cu.wchusbserial5A470211841`
- Native USB flash attempt reached the chip but failed during stub upload with checksum error; WCH UART flash succeeded.

Representative result:

```json
{"ok":true,"command":"SOURCE_RING_BENCH_AUTO","schema":"esp32_mod_lab.benchmark.source_ring.v1","run_index":2,"source_profile":"gbc_lcd","mode":"isolated_source_ingress_counters_only","performance_path":"lcdcam_gdma_double_buffer_rearm","next_performance_path":"persistent_continuous_descriptor_ring","requested_frames":120,"completed_frames":120,"dropped_frames":0,"partial_frames":0,"sync_loss_count":0,"dma_errors":0,"ring_slots":2,"ring_high_water_mark":1,"data_mode":"RGB565","capture_width":192,"capture_height":145,"bytes_per_sample":2,"frame_bytes":55680,"timeout_ms":300,"pclk_invert":false,"start_trigger":"SPS_RISING_THEN_SPL_FALLING","start_trigger_seen":true,"target_source_fps":59.730,"target_frame_us":16742.0,"target_rate_met":false,"avg_capture_budget_pct":119.3,"elapsed_us":2416658,"completed_fps":49.655,"payload_mbytes_per_s":2.765,"first_frame_us":19788,"avg_capture_us":19980,"max_capture_us":20765,"failure_stage":"none","last_esp_err":0,"run_esp_err":0,"checksum":1119877120}
```

Interpretation: source ingress is clean and repeatable in the isolated image: `120/120` frames completed, no drops, no partial frames, no sync loss, and no DMA errors. The measured rate is about `49.6 fps`, with average capture time around `19.98 ms`, which is `119.3%` of the GBC frame budget. This proves the current below-native source FPS is not caused by browser live view, SPI LCD output, TinyUSB, PNG rendering, or lab command overhead. The next source-performance step is a persistent continuous LCD_CAM/GDMA descriptor-ring path rather than more UI or SPI tuning.

2026-05-11: Extended the isolated source-ingress benchmark with a low-level cyclic LCD_CAM/GDMA descriptor ring and a native visible-size `160x144` test.

Purpose: separate true source ingress from the older browser-compatible programmed capture size. The previous `192x145` RGB565 benchmark captured `55,680` bytes per frame, while native GBC visible RGB565 is `160x144x2 = 46,080` bytes. The byte ratio is `1.2083`, which matches the older `~19.98 ms` capture time versus the `~16.54 ms` native-size result.

Representative low-level cyclic ring results:

```json
{"ok":true,"command":"SOURCE_RING_LOWLEVEL_BENCH_AUTO","schema":"esp32_mod_lab.benchmark.source_ring.v1","run_index":1,"source_profile":"gbc_lcd","mode":"isolated_source_ingress_counters_only","performance_path":"low_level_cyclic_lcdcam_gdma_descriptor_ring","requested_frames":120,"completed_frames":120,"dropped_frames":0,"partial_frames":0,"sync_loss_count":0,"dma_errors":0,"ring_slots":3,"descriptor_count_per_slot":14,"ring_rearms":119,"ring_rearm_failures":0,"unknown_eof_desc":0,"data_mode":"RGB565","capture_width":192,"capture_height":145,"bytes_per_sample":2,"frame_bytes":55680,"timeout_ms":300,"pclk_invert":false,"start_trigger":"SPS_RISING_THEN_SPL_FALLING","start_trigger_seen":true,"target_source_fps":59.730,"target_rate_met":false,"avg_capture_budget_pct":119.3,"elapsed_us":2395879,"completed_fps":50.086,"payload_mbytes_per_s":2.789,"first_frame_us":19791,"avg_capture_us":19967,"max_capture_us":20763,"failure_stage":"none","last_esp_err":0,"run_esp_err":0,"checksum":1119877120}
{"ok":true,"command":"SOURCE_RING_LOWLEVEL_BENCH_AUTO","schema":"esp32_mod_lab.benchmark.source_ring.v1","run_index":2,"source_profile":"gbc_lcd","mode":"isolated_source_ingress_counters_only","performance_path":"low_level_cyclic_lcdcam_gdma_descriptor_ring","requested_frames":120,"completed_frames":120,"dropped_frames":0,"partial_frames":0,"sync_loss_count":0,"dma_errors":0,"ring_slots":3,"descriptor_count_per_slot":12,"ring_rearms":119,"ring_rearm_failures":0,"unknown_eof_desc":0,"data_mode":"RGB565","capture_width":160,"capture_height":144,"bytes_per_sample":2,"frame_bytes":46080,"timeout_ms":300,"pclk_invert":false,"start_trigger":"SPS_RISING_THEN_SPL_FALLING","start_trigger_seen":true,"target_source_fps":59.730,"target_rate_met":true,"avg_capture_budget_pct":98.7,"elapsed_us":1982437,"completed_fps":60.532,"payload_mbytes_per_s":2.789,"first_frame_us":16536,"avg_capture_us":16520,"max_capture_us":16600,"failure_stage":"none","last_esp_err":0,"run_esp_err":0,"checksum":1119877120}
```

Interpretation: isolated ESP32-P4 source ingress can sustain native visible-size GBC RGB565 payload capture at source-rate class with zero drops/errors in this counters-only test. This does not yet prove image-perfect continuous line/frame phase for every boot/game condition, because the benchmark uses byte-count EOF and reports counters rather than decoded pictures. It does prove the MCU/LCD_CAM/GDMA path is not inherently limited to `~30 fps` or `~50 fps`; those earlier numbers came from lab transport/output overhead and from over-capturing the `192x145` compatibility buffer.

2026-05-11: Added host-side source-ring evidence collector.

Purpose: preserve the source-ingress FPS proof as a computer-side artifact instead of relying on a transient terminal monitor session. The collector reads serial JSON records from the isolated `experiments/source_ring_bench/` firmware and writes raw logs, parsed JSONL records, summary JSON, and a Markdown report.

Command:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh
python host/collect_source_ring_bench.py --port /dev/cu.wchusbserial5A470211841 --timeout-s 45 --native-records 1 --echo
```

Artifact:

- `captures/benchmarks/source_ring/20260511T184144Z-source-ring-bench/`

Computer-side summary:

- Records collected: `3`
- Native visible `160x144` low-level records: `1`
- Best native visible FPS: `60.532`
- Native visible target met: `True`
- Native visible drops: `0`
- Native visible DMA errors: `0`
- Native visible average capture: `16528 us`

Interpretation: the computer has now recorded the high-FPS source-ingress result as reproducible evidence. This still proves counters-only ingress, not browser live view FPS.

2026-05-11: Promoted the low-level native source-ring benchmark into the normal lab firmware command set.

Changes:

- Added `SOURCE_RING_LOWLEVEL_BENCH <frame_count> [timeout_ms] [width] [height] [RGB565] [pclk_invert] [frame_sync]`.
- Default geometry is native visible `160x144`.
- The command uses the low-level cyclic LCD_CAM/GDMA descriptor-ring path.
- It emits counters only and explicitly excludes browser frame streaming, SPI LCD drawing, and PNG rendering from the hot path.
- Removed GPIO7/GPIO8/GPIO9 from the GBC source safe-idle/electrical-isolate lists and input-test allowlist. Those pins were part of the abandoned SPI IO_MUX experiment and are no longer current GBC source pins.
- Moved normal lab console/control back to UART on `/dev/cu.wchusbserial5A470211841`, matching the proven isolated benchmark transport. Native USB Serial/JTAG enumerated but did not answer the JSON app protocol during this test.

Verification:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh
python host/gbc_probe.py --port /dev/cu.wchusbserial5A470211841 --timeout 5 command PING
python host/gbc_probe.py --port /dev/cu.wchusbserial5A470211841 --timeout 45 command "SOURCE_RING_LOWLEVEL_BENCH 120 300 160 144 RGB565 0 1"
```

Result:

```json
{"ok":true,"command":"SOURCE_RING_LOWLEVEL_BENCH","schema":"esp32_mod_lab.benchmark.source_ring.v1","source_profile":"gbc_lcd","mode":"source_ingress_counters_only","performance_path":"low_level_cyclic_lcdcam_gdma_descriptor_ring","next_performance_path":"production_source_frame_ring","requested_frames":120,"completed_frames":120,"dropped_frames":0,"partial_frames":0,"sync_loss_count":0,"dma_errors":0,"ring_slots":3,"descriptor_count_per_slot":12,"ring_rearms":119,"ring_rearm_failures":0,"unknown_eof_desc":0,"data_mode":"RGB565","capture_width":160,"capture_height":144,"bytes_per_sample":2,"frame_bytes":46080,"timeout_ms":300,"pclk_invert":false,"start_trigger":"SPS_RISING_THEN_SPL_FALLING","start_trigger_seen":true,"target_source_fps":59.73,"target_frame_us":16742.0,"target_rate_met":true,"avg_capture_budget_pct":98.7,"elapsed_us":1982435,"completed_fps":60.532,"payload_mbytes_per_s":2.789,"first_frame_us":16536,"avg_capture_us":16520,"max_capture_us":16600,"failure_stage":"none","last_esp_err":0,"run_esp_err":0,"checksum":1119877120}
```

Interpretation: the same high-FPS source-ingress path now works from the normal lab firmware over the WCH UART control path. This is the regression gate to use before wiring the low-level source ring into browser or destination features.

2026-05-11: Blocked default flashing of the standalone PPA SRM benchmark after repeated manual recovery.

Purpose: protect the development loop from standalone experiment images that bypass the known-good lab firmware boot, console, and recovery behavior.

Observed issue:

- The PPA SRM benchmark built successfully as an isolated ESP-IDF app.
- Flash attempts and follow-up recovery around this standalone path forced manual board recovery twice.
- This creates too much operational risk for the normal research loop.

Change:

- `scripts/flash_ppa_srm_bench.sh` now refuses to flash unless `ALLOW_EXPERIMENTAL_PPA_FLASH=1` is set.
- `experiments/ppa_srm_bench/README.md` now records that this app is compile evidence only until PPA is tested behind the known-good firmware path.
- `docs/DECISIONS.md` now states that standalone experiment firmware is not the default flash path.

Interpretation: this does not invalidate PPA as a useful ESP32-P4 block. It invalidates the current standalone PPA app as a safe iteration method on this board. The next PPA test should be a lab firmware command that allocates synthetic buffers, runs PPA SRM, prints JSON, and then returns to the normal command loop.

2026-05-11: Added PPA SRM benchmark as a normal lab firmware command.

Purpose: continue PPA investigation without flashing the risky standalone PPA app.

Changes:

- Added `firmware/main/ppa_srm_bench.c` and `firmware/main/ppa_srm_bench.h`.
- Added `PPA_SRM_BENCH [frame_count_1_to_1000]` to the command router.
- The command allocates synthetic DMA-capable PSRAM buffers and benchmarks `160x144 RGB565 -> 320x288 RGB565`.
- The command prints one JSON record for PPA SRM and one JSON record for CPU scaling.
- Added `host/collect_ppa_srm_bench.py` to preserve PPA-vs-CPU benchmark output as computer-side evidence under `captures/benchmarks/ppa_srm/`.
- Safe-recovery firmware blocks this command; normal lab firmware allows it.
- Added `esp_driver_ppa`, `esp_mm`, and `esp_psram` to the firmware component requirements.

Verification:

- `./scripts/build_probe_firmware.sh` passed.
- `python3 -m py_compile host/collect_ppa_srm_bench.py` passed.
- Flash through `/dev/cu.wchusbserial5A470211841` did not write because the board did not enter bootloader: `Failed to connect to ESP32-P4: No serial data received`.

Interpretation: the code path is ready in the normal lab firmware, but no PPA result has been collected yet. Do not keep retrying flash from Codex until the board is known to be in a normal flashable state.

2026-05-11: Collected PPA SRM benchmark evidence from normal lab firmware.

Setup:

- Normal probe firmware was rebuilt with console/control on native USB Serial/JTAG.
- Flash succeeded over `/dev/cu.usbmodem1432201`.
- After manual reset, the app protocol enumerated as `/dev/cu.usbmodem14401`.
- `PING` returned `{"ok":true,"response":"PONG"}`.

Command:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh
python host/collect_ppa_srm_bench.py \
  --port /dev/cu.usbmodem14401 \
  --frames 120 \
  --timeout-s 30 \
  --echo \
  --reset-input
```

Result:

```json
{"command":"PPA_SRM_SCALE2X_BENCH","fps":149.715,"avg_us":6679.3,"target_rate_met":true,"error":"none"}
{"command":"CPU_SCALE2X_BENCH","fps":80.510,"avg_us":12420.8,"target_rate_met":true,"error":"none"}
```

Artifact:

- `captures/benchmarks/ppa_srm/20260511T201252Z-ppa-srm-bench/`

Interpretation: PPA SRM is useful for production scaling. For the synthetic `160x144 RGB565 -> 320x288 RGB565` case, it is about `1.86x` faster than the CPU scaler and comfortably exceeds the GBC frame-rate budget. This removes CPU scaling as the main obstacle for 2x output. The remaining production display bottleneck is still destination bandwidth and integration with the source frame ring.

2026-05-11: Flashed PPA-backed 2x production mirror to the SPI LCD path.

Purpose: move the verified PPA SRM scaler from a lab benchmark into a production firmware path that drives the connected SPI LCD without the browser workbench in the hot path.

Changes:

- Added production compile-time mode `PRODUCTION_MIRROR_MODE=3`.
- The mode captures the GBC source as RGB565, copies the visible `160x144` region into a contiguous DMA-capable PPA source buffer, uses PPA SRM to scale to `320x288`, and draws that buffer using the existing known-good SPI LCD RGB666 destination path.
- The mode is intentionally synchronous for the first integration test. It does not yet overlap capture, PPA scaling, and SPI drawing.

Verification:

- `PRODUCTION_MIRROR_MODE=3 ./scripts/build_production_mirror.sh` passed.
- `PRODUCTION_MIRROR_MODE=3 ./scripts/flash_production_mirror.sh /dev/cu.usbmodem14401` passed.
- A short post-flash serial read while the GBC was off received no production metrics.
- After the GBC was powered on, `/dev/cu.usbmodem14401` produced stable production metrics:

```json
{"mode":"production_mirror_ppa_2x_sync","frames":6,"fps_x1000":5972,"avg_capture_us":41789,"avg_crop_us":1564,"avg_ppa_us":8222,"avg_draw_us":115619,"capture_failures":0,"ppa_failures":0,"draw_failures":0,"capture_error":"none","ppa_error":"none","draw_error":"none"}
```

Interpretation: the production LCD firmware image is on the ESP32-P4 and the PPA integration is running without capture/PPA/draw errors. This test should be judged as PPA integration evidence, not as a fast production destination. The `320x288` SPI draw takes about `115.6 ms`, so destination bandwidth dominates the `5.97 fps` result. PPA scaling itself takes about `8.2 ms`, which is inside the GBC frame budget.

2026-05-11: Measured PPA 1x production pass-through overhead.

Purpose: quantify the cost of running a real captured frame through PPA without changing output size. This is relevant for later hardware mirror/rotate/color processing where the destination may still be `160x144`.

Changes:

- Added production compile-time mode `PRODUCTION_MIRROR_MODE=4`.
- The mode captures RGB565, copies the visible `160x144` region into a contiguous DMA-capable PPA source buffer, runs PPA SRM at `scale_x=1.0` and `scale_y=1.0`, then draws the result through the known-good RGB666 SPI LCD destination path.
- Updated `scripts/build_production_mirror.sh` and `scripts/flash_production_mirror.sh` to run `idf.py reconfigure` before build/flash. This prevents stale CMake cache behavior when changing `PRODUCTION_MIRROR_MODE`.

Verification:

- Initial `PRODUCTION_MIRROR_MODE=4` build in the old build directory still flashed mode `3`, proving the stale-cache risk.
- A dedicated build directory was used for the valid test:

```sh
BUILD_DIR=build_production_mirror_m4 PRODUCTION_MIRROR_MODE=4 ./scripts/build_production_mirror.sh
BUILD_DIR=build_production_mirror_m4 PRODUCTION_MIRROR_MODE=4 ./scripts/flash_production_mirror.sh /dev/cu.usbmodem14401
```

Representative result:

```json
{"mode":"production_mirror_ppa_1x_sync","frames":15,"fps_x1000":14931,"avg_capture_us":34768,"avg_crop_us":1453,"avg_ppa_us":5248,"avg_draw_us":25395,"capture_failures":0,"ppa_failures":0,"draw_failures":0,"capture_error":"none","ppa_error":"none","draw_error":"none"}
```

Interpretation: PPA 1x pass-through costs about `5.25 ms` for a real `160x144 RGB565` frame. This is acceptable for future hardware transforms when needed, but pure 1x output should bypass PPA because it adds one memory-processing stage without improving the image. The current PPA 1x production mode remains limited by the older synchronous capture path and the `~25.4 ms` SPI RGB666 draw.

Follow-up visual observation:

- User reported the PPA 1x output has glitchy pixels.

Updated interpretation: the PPA 1x path is valid as a timing measurement, but it is not yet image-quality validated. Do not use it as the production visual baseline until the artifact source is isolated. Suspects include source crop/stride assumptions, cache synchronization/PSRAM coherency, or PPA SRM behavior at `1.0x`. Restore the known-good no-PPA 1x path after this measurement.

2026-05-11: Added and flashed production source-ring capture + PPA 2x + SPI LCD output.

Purpose: test the architecture the project actually needs next: use the fastest proven source capture path, use PPA for scaling, and avoid CPU geometric scaling before the LCD.

Changes:

- Added `lcdcam_raw_ring_capture_loop()` to expose completed DMA ring frames to production code instead of only reporting source-ring counters.
- Added production compile-time mode `PRODUCTION_MIRROR_MODE=5`.
- The source side captures native visible `160x144 RGB565` frames through `lcdcam_raw_ring_capture_loop`.
- The capture callback only copies the completed DMA slot into a DMA-capable processing buffer and returns the slot to the ring.
- A separate draw task runs PPA SRM `2x` and sends the already-scaled `320x288` output to the existing SPI LCD draw path.
- The metrics explicitly report `"cpu_scaling": false` and `"source_path":"lcdcam_raw_ring_capture_loop"`.

Verification:

```sh
PRODUCTION_MIRROR_MODE=5 ./scripts/build_production_mirror.sh
PRODUCTION_MIRROR_MODE=5 ./scripts/flash_production_mirror.sh /dev/cu.usbmodem14401
```

Build result: passed without warnings after cleaning mode-specific unused functions.

Representative metrics:

```json
{"mode":"production_mirror_ring_ppa_2x","displayed":8,"captured":61,"copied":8,"fps_x1000":7999,"avg_capture_us":16520,"avg_copy_us":3016,"avg_ppa_us":8648,"avg_draw_us":116166,"max_capture_us":16602,"max_copy_us":7048,"max_ppa_us":9992,"max_draw_us":116265,"dropped_frames":53,"capture_failures":0,"ppa_failures":0,"draw_failures":0,"cpu_scaling":false,"source_path":"lcdcam_raw_ring_capture_loop","capture_error":"none","ppa_error":"none","draw_error":"none"}
```

Interpretation: this is the first correct production architecture test combining the fast source-ring capture path with PPA and LCD output. The source capture now runs at GBC frame-rate class, around `16.5 ms` per native visible frame. PPA scaling takes about `8.3-9.0 ms`. The SPI LCD destination still takes about `116 ms` per `320x288` RGB666 draw, so it dominates output FPS and forces frame dropping. There is no CPU geometric scaling in this path; the CPU still performs frame handoff copying and the existing destination color expansion required by the RGB666 SPI panel path.

2026-05-11: Flashed true 1x source-ring + PPA + LCD production mode.

Reason: user observed that the previous source-ring + PPA production test was visibly 2x and showed a slow side/down scrolling drift. A 1x test is needed to separate output scale from source-ring frame phase.

Changes:

- Added `PRODUCTION_MIRROR_MODE=6`.
- Mode name: `production_mirror_ring_ppa_1x`.
- Uses the same `lcdcam_raw_ring_capture_loop` source path as mode `5`.
- Uses PPA SRM at `scale_x=1.0` and `scale_y=1.0`.
- Keeps `"cpu_scaling": false` in the runtime metrics.

Verification:

```sh
PRODUCTION_MIRROR_MODE=6 ./scripts/build_production_mirror.sh
PRODUCTION_MIRROR_MODE=6 ./scripts/flash_production_mirror.sh /dev/cu.usbmodem14401
```

Representative result:

```json
{"mode":"production_mirror_ring_ppa_1x","displayed":33,"captured":62,"copied":33,"fps_x1000":32075,"avg_capture_us":16537,"avg_copy_us":3376,"avg_ppa_us":5716,"avg_draw_us":25392,"max_capture_us":16600,"max_copy_us":6874,"max_ppa_us":7086,"max_draw_us":25459,"dropped_frames":29,"capture_failures":0,"ppa_failures":0,"draw_failures":0,"cpu_scaling":false,"source_path":"lcdcam_raw_ring_capture_loop","capture_error":"none","ppa_error":"none","draw_error":"none"}
```

Interpretation: this is now a 1x source-ring + PPA + LCD test. The source captures near 60 fps, while the SPI LCD 1x destination consumes around 32 fps. If the user still sees slow scrolling in this mode, the problem is likely frame/line phase in the ring image-producing path rather than output scale or CPU scaling.

2026-05-11: Flashed stream-geometry 1x source-ring + PPA + LCD production mode.

Reason: user reported that mode `6` was truly 1x but still slowly scrolled down. Because the browser/lab reconstruction previously used a `161x145` stream model with a `160x144` visible crop, a production mode was added to test that stream geometry directly.

Changes:

- Added `PRODUCTION_MIRROR_MODE=7`.
- Mode name: `production_mirror_ring_stream_ppa_1x`.
- Captures `161x145 RGB565` through `lcdcam_raw_ring_capture_loop`.
- Copies only the visible `160x144` region into the PPA handoff buffer.
- Keeps PPA at `1.0x` and reports `"cpu_scaling": false`.
- Fixed a DMA/cache coherency bug found by this mode: `161x145x2` is not cache-line aligned, so DMA backing buffers now allocate/cache-sync a padded length while descriptors and LCD_CAM byte-count EOF still use the true frame byte count.

Verification:

```sh
PRODUCTION_MIRROR_MODE=7 ./scripts/build_production_mirror.sh
PRODUCTION_MIRROR_MODE=7 ./scripts/flash_production_mirror.sh /dev/cu.usbmodem14401
```

Representative corrected result:

```json
{"mode":"production_mirror_ring_stream_ppa_1x","displayed":33,"captured":61,"copied":33,"fps_x1000":32271,"avg_capture_us":16742,"avg_copy_us":3390,"avg_ppa_us":5544,"avg_draw_us":25370,"dropped_frames":28,"capture_failures":0,"ppa_failures":0,"draw_failures":0,"cpu_scaling":false,"source_path":"lcdcam_raw_ring_capture_loop","capture_width":161,"capture_height":145,"visible_width":160,"visible_height":144}
```

Interpretation: mode `7` removes the cache-sync error and keeps the source at about one frame per `16.74 ms`, but visual validation is still required. If the image still scrolls, the remaining issue is most likely continuous-ring frame/line phase rather than output scaling, CPU scaling, or cache coherency.

2026-05-11: Added a deterministic GBC alignment test ROM.

Reason: the Game Boy boot screen is useful for first proof-of-image, but it is a poor diagnostic pattern once the remaining errors are subtle. Repeated letters, black-pixel noise, row slip, and stride mistakes need a known pixel pattern.

Changes:

- Installed RGBDS `1.0.1` through Homebrew.
- Added `tools/gbc_test_rom/`.
- Added `generate_alignment_rom.py`, which generates RGBDS assembly for a GBC-only test ROM.
- Added a `Makefile` that builds `build/p4_align.gbc`.

Pattern contents:

- Unique colored 2x2 tile corner blocks.
- Full-screen white border.
- Top-row color bars.
- Vertical stripe field.
- Horizontal stripe field.
- Checkerboard field.
- Center crosshair.
- Diagonal reference tiles.
- GBC background palettes for red, green, blue, yellow, cyan, magenta, grayscale, and white.

Verification:

```sh
cd tools/gbc_test_rom
make
```

Result:

```text
build/p4_align.gbc
32768 bytes
```

Interpretation: this ROM should be used for the next GBC-source debugging pass. It gives known visual anchors for orientation, color channel order, row stride, byte/pixel phase, and frame-boundary mistakes without depending on commercial ROM content.

2026-05-11: Promoted ROM-derived GBC source alignment into the source module.

Reason: the alignment ROM showed that the stable RGB565 stream was not random LCD/SPI corruption. The raw captured top rows classified as `12 red + 128 white + 17 green + 4 red`, while the ROM expects `16 red + 128 white + 16 green`. A host-side linear shift of `-4` pixels over the `161x145` stream produced the expected row geometry after the first captured pixels.

Evidence:

- `captures/decoded/rom_alignment/source_frame_rgb565.bin`
- `captures/decoded/rom_alignment/source_frame_161x145.png`
- `captures/decoded/rom_alignment/source_frame_shift_-4_160x144.png`
- Freeze-frame LCD test with the same shift produced a stable, visually correct ROM image.

Implementation:

- Added `GBC_LCD_SOURCE_VISIBLE_LINEAR_SHIFT_PIXELS = -4`.
- Added `gbc_lcd_source_copy_visible_rgb565()`.
- Production mirror output now draws the corrected `160x144` visible frame from the GBC source module instead of using production-only compile flags.
- Host live rendering now applies the same source alignment.

Current understanding:

- Reliable capture window remains `192x145`.
- Exported source stream remains `161x145 RGB565` (`46690` bytes).
- Correct visible frame is `160x144 RGB565` after a `-4` linear stream shift.
- The first four pixels of the first frame are not present in the current per-frame capture; display code clamps those pixels to the first captured pixel. This is acceptable as a documented compatibility fix, but a future persistent capture driver should arm early enough to remove that edge repair.

2026-05-12 update: optimized direct SPI LCD output so the `-4` visible-frame phase is applied inside the existing RGB565-to-panel conversion loop instead of first copying a corrected `160x144` intermediate frame.

Result:

```json
{"mode":"production_mirror_freeze_frame","frames":25,"fps_x1000":25000,"avg_draw_us":39978,"draw_failures":0,"source_stream_width":161,"source_visible_shift_pixels":-4}
```

Interpretation: the direct LCD path keeps the measured source alignment while avoiding a separate full-frame visible-copy pass. The `-4` correction is still a compatibility alignment, but it no longer costs an extra `160x144` RGB565 copy before SPI output. PPA/scaling paths may still need a contiguous corrected source buffer until they are separately optimized.

2026-05-12 update: tested production-oriented source-to-SPI LCD throughput using raw SPI and ring capture.

Reason: the previous production mirror path was limited to about `25 fps`, which was not consistent with the ESP32-P4 peripheral capabilities. The test separated LCD transfer cost from source capture cost.

Changes:

- Added build-time overrides for `DEST_SPI_LCD_PCLK_HZ`, `DEST_SPI_LCD_RAW_SPI`, and `PRODUCTION_CAPTURE_WINDOW_FRAMES`.
- Added production mode `9`, a ring-direct source-to-LCD path.
- Enabled raw SPI destination mode using `SPI_CLK_SRC_SPLL`, because the `esp_lcd` SPI wrapper uses the default ESP32-P4 SPI clock source. With the default XTAL source, the driver rejects `40 MHz` as invalid; raw SPI with SPLL accepts it.
- Switched production 1x LCD output to panel RGB565 mode, keeping the source in RGB565 and avoiding RGB565-to-RGB666 expansion.

Best stable result so far:

```json
{"mode":"production_mirror_ring_direct_1x","displayed":45,"captured":45,"fps_x1000":44634,"avg_capture_us":19950,"avg_draw_us":13255,"max_capture_us":20764,"max_draw_us":13304,"dropped_frames":0,"capture_failures":0,"draw_failures":0,"source_stream_width":161,"source_visible_shift_pixels":-4,"capture_error":"none","draw_error":"none"}
```

Interpretation:

- LCD draw is no longer the main limiter: raw `40 MHz` SPI draws one corrected `160x144` RGB565 frame in about `13.26 ms`.
- The stable live pipeline is currently about `43-44.6 fps`.
- Source capture is now the limiter in the stable path, with about `19.95 ms` between captured frames.
- A `161x145` ring-capture experiment produced frame intervals near `16.8 ms`, matching the expected GBC-class cadence, but the ring stopped after 7 frames and timed out. This is promising but not stable enough to leave flashed.

Next evidence needed:

- Fix continuous ring descriptor rearming or EOF handling so the `161x145` source stream can run indefinitely at the measured `~16.8 ms` cadence.
- Once stable, retest raw SPI draw plus `161x145` source capture. That is the path most likely to reach the console frame rate without scaling.
