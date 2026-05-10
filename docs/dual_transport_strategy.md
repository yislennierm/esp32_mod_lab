# Dual Transport Strategy

## 1. Objective

Define how to use the ESP32-P4 native USB port and the board UART bridge together while continuing capture development safely.

This matters because the project needs two things that compete with each other:

- maximum practical frame throughput from ESP32-P4 to the computer
- reliable recovery, logging, and control while firmware is changing

## 2. Current Understanding

Current physical ports on the bench:

| Port | Current role | Evidence |
|---|---|---|
| `/dev/cu.usbmodem14401` | Native ESP32-P4 USB Serial/JTAG app/control/data port | Probe `PING` returns `PONG`; browser live backend works here after the 2026-05-10 native-USB restore. |
| `/dev/cu.wchusbserial5A470211841` | WCH UART ROM/recovery path | `esptool chip_id` reaches ESP32-P4 ROM, uploads stub, reads MAC, and normal firmware flashing works at `115200`. |
| `/dev/cu.usbmodem5A470211841` | Companion device node from the second USB interface | Not assigned to a project role yet. |

Current firmware configuration:

- ESP-IDF console is on USB Serial/JTAG.
- Secondary console is disabled.
- The probe JSON command protocol uses `stdin`/`stdout`, so it currently lives on native USB.
- WCH UART does not currently answer probe `PING`.
- `TRANSPORT_STATUS` is the firmware command that host tools should use to verify the intended transport split before high-rate capture tests.

Confidence level: high for native USB as the application/lab port; high for WCH as a ROM flashing/recovery path; low for WCH as a project app/control port until firmware explicitly supports it.

## 3. Unknowns

- Whether UART0 pins on the board are safely available for project logs without disturbing boot mode.
- Whether native USB Serial/JTAG can sustain the required binary frame rate once firmware avoids per-frame setup/teardown.
- Whether USB-OTG/TinyUSB should replace USB Serial/JTAG for a future high-throughput product mode.

## 4. Experiment Results

2026-05-10:

- Regression correction: routing the live RGB565 viewer over WCH UART reduced the browser frame rate to about `0.24 fps`. That was a recovery-path misuse, not meaningful FPS progress. The lab firmware was restored to native USB Serial/JTAG as the app data plane and WCH UART as the recovery flashing path.

- After reflashing through WCH UART, native USB `/dev/cu.usbmodem14401` answered:

```sh
python host/gbc_probe.py --port /dev/cu.usbmodem14401 --timeout 3 command PING
```

with:

```json
{"ok": true, "response": "PONG"}
```

- The browser backend was restarted on native USB with:

```sh
scripts/start_gbc_live_view_usb.sh
```

and `/api/status` reported `source_state=live`, no consecutive errors, last capture around `189 ms`, and about `5.4 fps`.

- Host-side serial locking was added to `host/gbc_probe.py`. A second probe process now fails before opening an already-owned serial port:

```text
error: serial port /dev/cu.usbmodem14401 is already in use by another probe process; stop the live backend or existing command before opening it again
```

Interpretation: only one process may own the ESP32-P4 app command/data port at a time. The browser backend, CLI probe, flashing tool, and monitor are mutually exclusive on the same port.

- Native USB app command:

```sh
python host/gbc_probe.py --port /dev/cu.usbmodem14301 --timeout 2 command PING
```

returned:

```json
{"ok": true, "response": "PONG"}
```

- WCH UART ROM query:

```sh
python -m esptool --chip esp32p4 --port /dev/cu.wchusbserial5A470211841 --baud 115200 chip_id
```

identified ESP32-P4 revision v1.0, uploaded and ran the stub, and read MAC `30:ed:a0:e0:fc:c4`.

- WCH UART app command:

```sh
python host/gbc_probe.py --port /dev/cu.wchusbserial5A470211841 --timeout 2 command PING
```

returned no JSON response.

- WCH UART normal firmware flashing succeeded at `115200` with:

```sh
scripts/flash_probe_firmware.sh /dev/cu.wchusbserial5A470211841
```

After reset, native USB `/dev/cu.usbmodem14301` again answered `PING`.

- The current GBC source-driver binary command succeeded over native USB:

```sh
python host/gbc_probe.py --port /dev/cu.usbmodem14301 --timeout 10 binary 'GBC_SOURCE_FRAME_BIN 300 RGB565 0 0' -o /tmp/gbc_source_frame.bin
```

Result: `binary_len=46690`, `payload_len=46690`, `data_mode=RGB565`, `capture_us` about `37 ms`.

- The browser backend was restarted on the source-driver path at `http://127.0.0.1:8791/`. `/api/status` reported `source_state=live` and about `5.4 fps`; `/api/frame.bin` returned `46690` bytes.

- Added `GBC_SOURCE_STREAM_BIN` and benchmarked `32` RGB565 frames over native USB Serial/JTAG. Result: `32/32` frames, `46690` bytes per frame, `4.83 s` elapsed, `6.626 fps`.

- Browser backend using `--gbc-source-driver --stream-batch-size 8` reports about `7.1 fps` with fresh `46690` byte frames.

- Added `USB_BENCH_STREAM_BIN` as a capture-free synthetic native USB Serial/JTAG benchmark. This command does not touch GPIO, LCD_CAM, DMA, or the GBC bus; it only emits deterministic binary payloads.

Synthetic RGB565-sized benchmark:

```sh
python host/gbc_probe.py --port /dev/cu.usbmodem14301 --timeout 30 binary-sequence 'USB_BENCH_STREAM_BIN 64 46690' --count 64
```

Result: `64/64` frames, `2988160` total payload bytes, `7.086 s` elapsed, `9.032 fps`, about `0.42 MB/s`.

Synthetic half-frame benchmark:

```sh
python host/gbc_probe.py --port /dev/cu.usbmodem14301 --timeout 30 binary-sequence 'USB_BENCH_STREAM_BIN 128 23345' --count 128
```

Result: `128/128` frames, `2988160` total payload bytes, `7.111 s` elapsed, `18.0 fps`, again about `0.42 MB/s`.

Interpretation: the native USB Serial/JTAG command/data path is byte-throughput limited. Halving the payload doubles apparent FPS while total bytes per second stays essentially unchanged. The transport-only ceiling for the current `46690` byte RGB565 payload is about `9 fps`, so a 59-60 FPS live monitor cannot be reached on this data plane alone.

- Created an isolated TinyUSB CDC benchmark firmware under `experiments/tinyusb_bench/`.
- Build succeeded and fetched the managed `esp_tinyusb`/`tinyusb` components into that experiment only.
- Flash through WCH UART succeeded.
- With the current two USB cables connected, no visible `/dev/cu.usbmodem...` port answered the benchmark `PING`.
- Normal probe firmware was restored and native USB `PING` worked again.

Interpretation: TinyUSB is not yet blocked by code generation or build tooling; it is blocked by board/connector certainty. The currently used native USB Serial/JTAG port is not the same thing as the ESP32-P4 USB-OTG TinyUSB device path. Before deeper TinyUSB work, identify the board's USB-OTG D+/D- routing and whether the exposed connector is OTG2.0, OTG1.1, or not populated.

## 5. Target Transport Model

### Stage A: Current Safe Split

Use:

- native USB for browser UI, app commands, and frame data
- WCH UART for ROM recovery testing only

Do not run the browser backend while flashing.

### Stage B: Safe Development Split

Use:

- native USB for app commands and binary frame stream
- WCH UART for ROM recovery and optional firmware logs

Requirement: logs must not be interleaved with binary frame payloads on native USB. Binary transport must stay parseable under errors.

### Stage C: Maximum-FPS Lab Split

Use:

- native USB as the high-rate data plane
- WCH UART as a low-rate control/log/recovery plane

Requirement: firmware must expose an intentional UART control/log task or log sink. Do not rely on accidental console behavior.

### Stage D: Future Product/Instrument Split

Evaluate:

- USB Serial/JTAG for development
- USB-OTG/TinyUSB bulk or vendor interface for sustained high-rate streaming
- UART as service/debug/recovery

This stage is now justified by measurement. It must still be introduced as an isolated experimental data plane so the current native USB app protocol and WCH recovery path remain available.

## 6. FPS Strategy

UART is not the path for high FPS. It is too slow for full-frame video.

FPS must improve by:

1. Keeping LCD_CAM/GDMA configured across frames instead of starting/stopping capture every frame.
2. Streaming binary frames with a compact frame header and no JSON/hex payload in the hot path.
3. Avoiding logs on the data stream.
4. Using larger USB writes and fewer command round trips.
5. Measuring where time is spent: capture time, USB write time, host read time, browser render time.
6. Moving the high-rate data plane to USB bulk/vendor transport if USB Serial/JTAG cannot sustain full-rate frames.

The first useful target is stable GBC frame-rate capture near the source rate. The GBC source is approximately 60 Hz, so the lab should aim for 59-60 FPS for the working 160x144 view before adding processing blocks.

Current RGB565 payload size is `46690` bytes per frame. At 60 fps that is about `2.8 MB/s` before headers and framing overhead. The measured USB Serial/JTAG source stream is far below that, and the synthetic transport-only benchmark is about `0.42 MB/s`. Transport throughput is confirmed as a first-class performance blocker.

TinyUSB migration rule: do not replace the working app protocol in one step. First add a synthetic TinyUSB throughput benchmark that has no capture dependency. Only after it proves materially higher throughput should the GBC source driver publish frames through that data plane. Current blocker: the benchmark firmware builds and flashes, but the board's accessible USB-OTG path has not been confirmed.

USB Host note, checked against Espressif's ESP32-P4 USB Host documentation on 2026-05-10:

- The USB Host Library is for the ESP32-P4 acting as a host for external USB devices.
- It supports high-speed, full-speed, and low-speed devices, and control, bulk, interrupt, and isochronous transfer types.
- ESP32-P4 has two USB 2.0 OTG peripherals, one high-speed and one full-speed; Espressif documents a current software limitation where only one can operate as USB Host at a time.
- This is useful if the lab tool later needs to read a USB device, such as a USB storage device, USB capture accessory, HID controller, or another instrument.
- This is not the right API for sending captured frames from ESP32-P4 to the computer. For that direction the ESP32-P4 must act as a USB device, so the relevant path remains USB Serial/JTAG for the current working protocol or TinyUSB USB-device bulk/vendor/CDC for a future high-rate data plane.

2026-05-10 current-USB capture-card smoke test:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log && python host/gbc_probe.py --port /dev/cu.usbmodem14301 --timeout 30 binary-sequence "GBC_SOURCE_STREAM_BIN 16 300 RGB565 46690 0" --count 16 --output-dir captures/experiments/current_usb_capture_card_smoke
```

Result:

- `16/16` RGB565 payloads received.
- Payload per frame: `46690` bytes.
- Total payload: `747040` bytes.
- End-to-end delivered rate: `5.975 fps`.
- Frames were saved to `captures/experiments/current_usb_capture_card_smoke/`.

Interpretation: a simple capture-card style stream works over the current native USB Serial/JTAG path, but only at about `6 fps` for the RGB565 useful-window payload. This confirms the current USB path can be used for low-FPS preview/recording, but not final full-rate capture-card operation.

## 7. Safety Rules

- Keep WCH ROM recovery available before risky FPS firmware.
- Build or flash safe-recovery firmware before aggressive streaming changes.
- Stop browser/backend before flashing.
- Do not run multiple host clients against the same app serial port. Parallel serial clients corrupt JSON and binary frame boundaries.
- Start the known-good RGB565 live monitor through `scripts/start_gbc_live_view_usb.sh` unless deliberately testing another path.
- Treat WCH UART frame streaming as a regression unless the test is explicitly a recovery-bandwidth experiment.
- Do not mix logs and binary frame payloads on the same stream without framing.
- Do not move the app protocol to WCH until firmware support is explicit and tested.
- Always keep a known-good native USB `PING` path after flashing.

## 8. Next Steps

1. Keep WCH flashing/recovery available before all transport experiments.
2. Use `TRANSPORT_STATUS` in host/UI startup checks before enabling live frame streaming.
3. Add firmware timing counters around capture and binary output.
4. Identify the exact board USB-OTG connector/header route for TinyUSB.
5. Re-run the isolated TinyUSB CDC benchmark once the correct USB-OTG path is connected.
6. If TinyUSB materially exceeds native USB Serial/JTAG throughput, move the high-rate frame data plane there.
7. Implement persistent LCD_CAM/GDMA streaming with compact frame headers after the data plane is proven.
8. Decide whether WCH should receive logs only or a secondary control protocol.
