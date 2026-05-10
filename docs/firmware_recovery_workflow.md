# Firmware Recovery Workflow

## 1. Objective

Define a repeatable way to recover, verify, and flash the ESP32-P4 without disturbing the console bus.

This matters because failed or interrupted flashes can leave the app partition incomplete even though the ROM bootloader still exists.

## 2. Current Understanding

Current working recovery paths:

- Native ESP32-P4 USB/JTAG/serial port has successfully flashed at `115200`.
- Working recovered port on 2026-05-10: `/dev/cu.usbmodem14301`.
- The WCH UART bridge appears as `/dev/cu.wchusbserial5A470211841` and has successfully answered an ESP32-P4 ROM `chip_id` query.
- Normal firmware flashing through the WCH UART bridge works at `115200`.
- The WCH UART bridge does not currently speak the probe JSON application protocol.
- Stop the live backend before flashing. The backend holds the serial port.

Confidence level: high for native USB at low baud as the current app/control path; high for WCH UART as a ROM/recovery/flashing path; low for WCH UART as an app protocol path.

## 3. Unknowns

- Whether board switches or jumpers improve WCH bridge flashing.
- Whether the board exposes different ports depending on boot/reset state.
- Whether GBC wiring contributes to USB or reset instability during flash.

## 4. Experiment Results

2026-05-10:

- Native USB high/normal speed flashing failed during experimental FPS work and left the app nonresponsive.
- User flashed an example project from VS Code to restore board access.
- Native USB later succeeded at `115200` with:

```sh
idf.py -B build_safe_recovery_p4 -p /dev/cu.usbmodem14301 -b 115200 flash
```

- After the successful flash, `PING` responded on `/dev/cu.usbmodem14301`.
- `GET_VERSION` reported normal firmware: `0.1.0-phase1`.
- WCH UART `/dev/cu.wchusbserial5A470211841` responded to:

```sh
python -m esptool --chip esp32p4 --port /dev/cu.wchusbserial5A470211841 --baud 115200 chip_id
```

- Result: ESP32-P4 revision v1.0, MAC `30:ed:a0:e0:fc:c4`, stub upload/run succeeded, hard reset via RTS succeeded.
- `PING` over `/dev/cu.wchusbserial5A470211841` returned no JSON response, so the app protocol remains on native USB.
- Normal probe firmware flashed successfully through `/dev/cu.wchusbserial5A470211841` at `115200` using:

```sh
scripts/flash_probe_firmware.sh /dev/cu.wchusbserial5A470211841
```

- After WCH flashing, `PING` responded on native USB `/dev/cu.usbmodem14301`.

## 5. Standard Procedure

Before flashing:

```sh
scripts/stop_lab_processes.sh
```

List ports:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log
python host/gbc_probe.py ports
```

Build normal probe firmware:

```sh
scripts/build_probe_firmware.sh
```

Flash normal probe firmware through native USB when it is stable:

```sh
scripts/flash_probe_firmware.sh /dev/cu.usbmodem14301
```

Flash normal probe firmware through WCH UART when native USB flashing is unstable or occupied:

```sh
scripts/flash_probe_firmware.sh /dev/cu.wchusbserial5A470211841
```

Build safe recovery firmware:

```sh
scripts/build_safe_recovery.sh
```

Flash safe recovery firmware:

```sh
scripts/flash_safe_recovery.sh /dev/cu.usbmodem14301
```

Verify firmware:

```sh
source /Users/nene/esp/v5.5/esp-idf/export.sh >/tmp/idf_export.log
python host/gbc_probe.py --port /dev/cu.usbmodem14301 --timeout 3 command PING
python host/gbc_probe.py --port /dev/cu.usbmodem14301 --timeout 3 command GET_VERSION
```

## 6. Safety Rules

- Do not flash while `host/live_lcdcam_stream_viewer.py` is running.
- Prefer GBC off or disconnected while flashing.
- Use `115200` until a faster flash path is proven stable.
- Treat WCH UART as ROM/recovery/flashing only until app protocol support is intentionally added.
- Keep the app JSON protocol and browser backend on native USB unless firmware is intentionally changed to expose the protocol on UART.
- Do not combine firmware transport changes and capture timing changes in one flash.

## 7. Next Steps

- Save known-good `.bin` images outside build directories.
- Add an automated post-flash smoke test.
- Consider A/B partitions before aggressive streaming firmware work.
