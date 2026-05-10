# TinyUSB Transport Bench

## Objective

Measure ESP32-P4 USB-OTG/TinyUSB throughput without touching console display bus GPIO, LCD_CAM, GDMA, or target-specific capture code.

This experiment exists because the working GBC RGB565 monitor is limited by the current native USB Serial/JTAG command/data path. The benchmark lets us test a separate USB data plane before moving any source-driver frames onto it.

## Current Understanding

The normal firmware remains the safe working path:

- native USB Serial/JTAG: current app protocol and browser frame stream
- WCH UART: ROM flashing and recovery

This experiment is a separate firmware image. It exposes a TinyUSB CDC ACM serial device and implements a small subset of the probe protocol:

- `PING`
- `GET_VERSION`
- `USB_BENCH_STREAM_BIN <frame_count> <payload_len>`

Confidence level: medium for the firmware build; low for the current board cabling exposing a TinyUSB data port. ESP-IDF includes an ESP32-P4 TinyUSB CDC example, but this project must still verify which board USB-C connector exposes USB-OTG/TinyUSB and what throughput macOS can sustain.

## Unknowns

- Which host device path the TinyUSB CDC interface will enumerate as on this board.
- Whether TinyUSB CDC is materially faster than native USB Serial/JTAG.
- Whether a later vendor/bulk interface is required for 59-60 FPS RGB565.
- Whether both USB-C cables can be used simultaneously while this firmware is running.

## Experiment Results

2026-05-10:

- `scripts/build_tinyusb_bench.sh` built successfully.
- ESP-IDF component manager fetched:
  - `espressif/esp_tinyusb 1.7.6~2`
  - `espressif/tinyusb 0.19.0~3`
- `scripts/flash_tinyusb_bench.sh /dev/cu.wchusbserial5A470211841` flashed successfully through the WCH UART recovery path.
- Serial ports after flashing were:
  - `/dev/cu.wchusbserial5A470211841`
  - `/dev/cu.usbmodem5A470211841`
  - `/dev/cu.usbmodem14301`
- `PING` did not receive JSON on `/dev/cu.usbmodem14301`.
- `PING` did not receive JSON on `/dev/cu.usbmodem5A470211841`.
- The normal probe firmware was reflashed through WCH UART after the experiment.
- Normal firmware `PING` on `/dev/cu.usbmodem14301` returned `PONG`.

Interpretation: the TinyUSB firmware itself builds and flashes, but the currently connected two-cable setup did not expose a responding TinyUSB CDC command port. The likely board-level explanation is that the connected USB-C ports are USB Serial/JTAG and WCH UART, while TinyUSB requires the ESP32-P4 USB-OTG device D+/D- path. This must be confirmed against the exact board schematic or USB connector documentation before more firmware time is spent on TinyUSB.

Baseline comparison from normal firmware:

- `USB_BENCH_STREAM_BIN 64 46690` over native USB Serial/JTAG: about `0.42 MB/s`, `9.032 fps`.
- Required RGB565 59-60 FPS payload rate: about `2.8 MB/s` before overhead.

## Next Steps

1. Identify whether the board exposes ESP32-P4 USB-OTG D+/D- on an available connector or header.
2. Confirm whether TinyUSB should use OTG2.0 or OTG1.1 on this board.
3. If USB-OTG is available, repeat `PING`.
4. Run the same `USB_BENCH_STREAM_BIN` benchmarks used on the normal firmware.
5. Reflash normal probe firmware after each benchmark session.
