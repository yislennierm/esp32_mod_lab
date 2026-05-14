# Host Tools

Host tools are the primary experimentation layer for the GBC LCD bus probe.

## Probe CLI

Use `gbc_probe.py` to communicate with the ESP32-P4 firmware over USB-Serial/JTAG.

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh
python host/gbc_probe.py --port /dev/cu.wchusbserial5A470211841 smoke
python host/gbc_probe.py --port /dev/cu.wchusbserial5A470211841 command GET_VERSION
```

As of 2026-05-11, the normal lab firmware uses UART console/control on the WCH
bridge. Native USB Serial/JTAG may still enumerate, but it is not the current
known-good app protocol port.

The smoke test currently verifies:

- `PING`
- `GET_VERSION`
- `EXPORT_STATS`
- `capture_pin_count == 0` during Phase 1
- required capture commands are blocked with `no_capture_pins_configured`

## Experiment Recorder

Use `record_experiment.py` to create a timestamped experiment folder with metadata,
notes, a Phase 1 voltage template, and an optional smoke-test result.

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh
python host/record_experiment.py --port /dev/cu.usbmodem14201 "baseline smoke"
```

## Phase 1 Measurement Validation

Use `validate_phase1_measurements.py` before any firmware pin map changes.

```sh
python host/validate_phase1_measurements.py \
  captures/experiments/<session>/phase1_voltage_template.csv \
  --report captures/experiments/<session>/phase1_validation_report.md
```

The validator blocks incomplete data, dangerous rails, duplicate GPIOs, unsafe
voltages, and signals that require level shifting.

## Source Ring Benchmark Evidence

Use `collect_source_ring_bench.py` when the isolated
`experiments/source_ring_bench/` firmware is flashed. It reads benchmark JSON
records from the ESP32-P4 serial console and saves a computer-side evidence
bundle with raw logs, parsed records, a summary, and a Markdown report.

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh
python host/collect_source_ring_bench.py \
  --port /dev/cu.wchusbserial5A470211841 \
  --timeout-s 45 \
  --native-records 1 \
  --echo
```

Artifacts are written under `captures/benchmarks/source_ring/`.

The normal lab firmware also exposes the same low-level source-ingress path as a
command:

```sh
python host/gbc_probe.py \
  --port /dev/cu.wchusbserial5A470211841 \
  --timeout 45 \
  command "SOURCE_RING_LOWLEVEL_BENCH 120 300 160 144 RGB565 0 1"
```

## PPA SRM Benchmark Evidence

Use `collect_ppa_srm_bench.py` after flashing normal lab firmware that includes
the `PPA_SRM_BENCH` command. It sends the command, collects both the PPA and CPU
JSON records, and saves raw logs, parsed records, a summary, and a Markdown
report.

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh
python host/collect_ppa_srm_bench.py \
  --port /dev/cu.wchusbserial5A470211841 \
  --frames 120 \
  --timeout-s 30 \
  --echo
```

Artifacts are written under `captures/benchmarks/ppa_srm/`.

This benchmark uses synthetic `160x144 RGB565 -> 320x288 RGB565` buffers. It
does not touch the GBC source pins, SPI LCD destination, browser stream, or USB
frame payload path.

## Fast LCD_CAM Decode Preset

Use `decode_lcdcam_fast.py` to reconstruct PNG frames from `DE=HIGH` LCD_CAM
RAW8 streams using named decode presets.

```sh
python host/decode_lcdcam_fast.py \
  captures/decoded/lcdcam_raw/20260508T193122Z-lcdcam_raw_high_320x204.bin \
  --preset gbc_rg44_fast_v1
```

Current preset `gbc_rg44_fast_v1`:

- `161` bytes per transfer line
- `145` transfer lines per frame period
- `160x144` visible crop
- RAW8 `RG44`: low nibble red upper bits, high nibble green upper bits

Blue-enabled preset `gbc_rgb332_fast_v1`:

- Same `161` byte / `145` line fast-stream geometry
- RAW8 `RGB332`: `R5-R3`, `G5-G3`, and `B5-B4`
- Use with LCD_CAM captures made with `--data-mode RGB332`

## Live LCD_CAM Viewer

Use `live_lcdcam_stream_viewer.py` for browser-based capture review with decode
controls and ESP32-P4 instrument controls.

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh
python host/live_lcdcam_stream_viewer.py \
  --port /dev/cu.usbmodem14401 \
  --listen-port 8776 \
  --no-pclk-invert \
  --data-mode RGB332
```

The viewer is now organized around the project method:

- `Project` shows the source -> ESP32-P4 processing -> destination pipeline and instrument state.
- `Source` contains safety, pin inspection, GPIO timeline, edge scans, timing capture, line clocks, and signal roles for the active source profile.
- `Processing` is the future home for ESP32-P4 blocks such as capture, retiming, buffering, color conversion, scaling, and debug taps. It currently summarizes the active source-capture block.
- `Destination` is the future home for output panel/protocol profiles. It currently shows that no destination is configured.
- `Live` keeps the current decoded frame canvas and existing GBC live controls.
- `Artifacts` lists recent capture folders under `captures/experiments/`.
- `Profile` renders the active target profile, currently `profiles/gbc_lcd.json`.
- `Logs` records browser-side actions and exposes safe probe commands such as `PING`, `GET_VERSION`, and `EXPORT_STATS`.

The browser layout follows the workbench model: the left side is for viewers,
tables, reports, and captured data; the right side is the contextual parameter
panel for the active feature.

The viewer starts in a stopped state. Use `Single` for one capture, `Start` for
continuous capture, and `Stop` before switching the GBC power off or on. This is
intentional because active capture has been observed to coincide with unstable
GBC power behavior.

`Stop` also sends the firmware `SAFE_IDLE` command. `SAFE_IDLE` stops LCD_CAM,
detaches LCD_CAM input signals from the GPIO matrix, and reconfigures the current
GBC bus GPIOs as floating inputs.

Use `Recover` if capture does not resume cleanly after switching the GBC off/on.
Recover stops the background capture loop, closes and reopens the serial port,
drains pending startup output, sends `SAFE_IDLE`, and clears the host-side error
state. `Safe Idle` sends only `SAFE_IDLE`.

The primary debug controls are color and bit operations:

- Channel order: `RGB`, `RBG`, `GRB`, `GBR`, `BRG`, or `BGR`
- Channel enable shortcuts: `Only R`, `Only G`, `Only B`, and `All Channels`
- Per-channel invert and bit-reverse controls
- Packed bit-plane masks `b7` through `b0`

For `RGB332`, the packed bit planes are `b7..b5=R`, `b4..b2=G`, and
`b1..b0=B`. The geometry sliders are still available below the color controls.

Live frame delivery uses `/api/frame.bin`. By default the viewer now requests
`LCDCAM_RAW_CAPTURE_BIN`, which returns a compact JSON header followed by raw
frame bytes from the ESP32-P4. Metadata is sent to the browser in the
`X-Capture-Meta` HTTP header. Pass `--no-firmware-binary` only when testing the
older JSON/hex firmware path.

Current faster live command:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh
python host/live_lcdcam_stream_viewer.py \
  --port /dev/cu.usbmodem14401 \
  --listen-port 8788 \
  --no-pclk-invert \
  --interval-ms 33 \
  --capture-timeout-ms 2500 \
  --width 192 \
  --height 145 \
  --data-mode RGB332 \
  --firmware-binary \
  --no-source-binary \
  --continuous-capture \
  --stream-batch-size 8 \
  --host-crop \
  --crop-offset 0 \
  --crop-width 161 \
  --crop-height 145 \
  --profile profiles/gbc_lcd.json
```

This captures `192x145` bytes in firmware, sends `27840` raw bytes over USB
serial, and forwards the first `161x145` source-period slice to the browser.

Continuous mode adds `/api/start`, `/api/stop`, and `/api/status`. `Start`
begins a server-side capture thread and `/api/frame.bin` returns the latest
completed frame. `--stream-batch-size 8` uses `LCDCAM_RAW_STREAM_BIN 8` to
amortize command overhead across multiple binary frames. Current measured rate
is about `10 fps` on the console transport. The next speed ceiling is persistent
firmware capture setup and a more explicitly binary-safe USB transport.

Workbench endpoints:

- `/api/artifacts/recent`
- `/api/workbench/gpios`
- `/api/workbench/read-gpios`
- `/api/workbench/count-edges-all?duration_ms=1000`
- `/api/workbench/measure-clock?gpio=22&duration_ms=1000`
- `/api/workbench/capture-timing?duration_ms=100`
- `/api/workbench/line-clocks?marker=SPL&edge=falling&line_count=180&timeout_ms=2000`

These endpoints are profile allowlisted and save timing artifacts under
`captures/experiments/`.

Timing captures also export `timing_edges.vcd` for PulseView/sigrok-style
inspection. Manual conversion:

```sh
python host/export_pulseview.py captures/experiments/<session>/raw.json \
  -o captures/experiments/<session>/timing_edges.vcd
```

The VCD is event-based with a `1 us` timescale. It is suitable for inspecting
timing/control relationships, not for full DCLK-rate logic capture.

Use `--no-pclk-invert` for the current RGB332 live view. Earlier inverted-PCLK
captures produced G5 instability around text edges, visible as cyan sparkle.

Manual recovery command:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh
python host/gbc_probe.py --port /dev/cu.usbmodem14401 --timeout 3 command SAFE_IDLE
```
