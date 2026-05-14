# ESP32-P4 Board Capability Audit

Purpose: separate what the ESP32-P4 chip can do from what this specific board exposes safely.

Status: active research document. The exact board model and schematic are not yet recorded in the repo, so board exposure is treated as unknown unless proven by wiring, scope, or successful firmware use.

Last updated: 2026-05-11.

## Objective

Identify which ESP32-P4 capabilities are physically usable on the current board for the console signal lab.

This matters because ESP32-P4 has strong video and DMA peripherals, but a board can make a capable peripheral unusable by not exposing pins, tying pins to onboard functions, omitting the connector, or using reset/default states that are unsafe for connected console buses.

## Current Understanding

Known board facts from project evidence:

| Area | Current Understanding | Evidence | Confidence |
|---|---|---|---|
| MCU | ESP32-P4 revision v1.0 | `esptool chip_id` succeeded during recovery workflow | High |
| Native USB Serial/JTAG | Used for app command/control and browser live view | `/dev/cu.usbmodem...` ports and `PING` history | High |
| WCH UART bridge | Usable for ROM recovery/flashing, not app protocol | `/dev/cu.wchusbserial5A470211841` chip-id/flash success | High |
| GBC source wiring | Current profile captures visible GBC video | `profiles/gbc_lcd.json`, `pinmap_current.h`, browser/SPI proof | High |
| SPI LCD debug output | Current GPIO-matrix SPI profile works | RGB666 fills and GBC image on panel | High |
| USB HS OTG connector | Not confirmed | TinyUSB experiment did not prove accessible HS data plane | Low |
| DSI connector/pins | Not confirmed | No board schematic/model in repo | Low |
| RGB LCD/I80 pin availability | Not confirmed | SoC supports them; board exposure unresolved | Low |

Exact board identity:

| Field | Value |
|---|---|
| Board model | Unknown |
| Vendor | Unknown |
| Schematic URL/file | Unknown |
| Header pinout source | User observation and current wiring only |
| Assumption currently in use | Board header GPIO labels are treated as ESP32-P4 GPIO numbers unless proven otherwise |

Possible reference pattern:

| Candidate / Reference | Why It Matters | Status |
|---|---|---|
| Espressif `ESP32-P4X-Function-EV-Board` / `ESP32-P4-Function-EV-Board` family | Official docs describe separate USB Full-speed, USB Serial/JTAG, USB 2.0 High-Speed OTG Type-C, USB 2.0 Type-A, MIPI CSI, MIPI DSI, and J1 GPIO header exposure. This is the closest official reference pattern for a multimedia ESP32-P4 board. | Not confirmed as the user's board |

Official reference notes:

- ESP-IDF USB Serial/JTAG docs state the USB Serial/JTAG controller is fixed-function CDC/JTAG, not a configurable TinyUSB data plane.
- ESP-IDF USB Serial/JTAG docs list USB Serial/JTAG pins as `D+ GPIO25/27` and `D- GPIO24/26`, depending on the selected USB channel.
- Espressif `ESP32-P4X-Function-EV-Board` docs describe a separate USB 2.0 High-Speed OTG Type-C port for ESP32-P4 device-mode work, and a separate USB Serial/JTAG Type-C port for flashing/control/debug.
- If our board follows that pattern, the port we used successfully for the app protocol is probably not the same connector needed for a TinyUSB HS capture-card stream.

## Current Pin Ownership

These pins are actively allocated by the current working setup. Future destination/source experiments must not reuse them without updating the profile and firmware.

### GBC Source

| Function | GPIOs |
|---|---|
| Timing/control | `DCLK=22`, `LP=21`, `PS=20`, `SPL=19`, `CLS=3`, `SPS=33` |
| Red bus | `R5=13`, `R4=14`, `R3=15`, `R2=16`, `R1=17`, `R0=18` |
| Green bus | `G5=6`, `G4=5`, `G3=4`, `G2=10`, `G1=11`, `G0=12` |
| Blue bus | `B5=50`, `B4=48`, `B3=47`, `B2=46`, `B1=45`, `B0=36` |

Known avoidance:

- `GPIO32` is historical for `CLS` and should not be reused casually. It was associated with power-cycle/backfeed trouble.
- GPIOs connected to an unpowered target can bias or back-power the target. Software isolation is not a substitute for hardware isolation.

### SPI LCD Debug Destination

| LCD Signal | GPIO | Status |
|---|---:|---|
| `CS` | `52` | Known-good GPIO-matrix wiring |
| `MOSI/SDI` | `31` | Known-good GPIO-matrix wiring |
| `SCK` | `28` | Known-good GPIO-matrix wiring |
| `RESET` | `29` | Known-good control pin |
| `D/C` | `53` | Known-good control pin |

The failed SPI2 IO_MUX experiment used `CS=7`, `MOSI=8`, `SCK=9`. The pins produced clean scope activity but did not produce LCD pixels. Keep that experiment isolated from the known-good profile.

## Peripheral Exposure Audit

| Capability | ESP32-P4 SoC Support | Board Exposure | Project Evidence | Research Action |
|---|---|---|---|---|
| LCD_CAM DVP input | Yes | Header GPIOs are usable for current GBC source | Visible GBC frames | Improve continuous source capture |
| ISP DVP input | Yes | Same physical source pins may be routable | Earlier full zero buffers | Keep as secondary reference |
| MIPI CSI | Yes | Unknown | None | Needs board schematic/connector check |
| SPI LCD via GPIO matrix | Yes | Proven | Current debug LCD works | Keep as debug destination |
| SPI2 IO_MUX | Yes, `CS0=7`, `MOSI=8`, `SCLK=9` | Header pins accessible enough to pulse | LCD stayed white | Retest only in isolated test firmware/profile |
| I80 LCD | Yes, up to 24-bit bus by SoC capability | Unknown | None | Pin feasibility audit before wiring |
| RGB LCD | Yes, up to 24 data lines by SoC capability | Unknown | None | Pin feasibility audit before wiring |
| MIPI DSI | Yes | Unknown | None | Identify board connector and DPHY power path |
| PARLIO | Yes, 16-bit RX/TX by SoC capability | Unknown | None | Defer until standard paths are evaluated |
| USB Serial/JTAG | Yes | Proven | App/control/live view | Keep for control and recovery logs |
| USB OTG HS device | Yes | Unknown on board | TinyUSB not proven on accessible connector | Identify connector and enumerate HS device |
| WCH UART | Board feature | Proven | ROM flashing and chip-id | Recovery/flashing only |
| SDMMC | Yes | Unknown | None | Optional capture logging after pin audit |

## Unknowns

- Exact board model, vendor, schematic, and official pinout.
- Whether the USB-C connector used for native USB is only USB Serial/JTAG or also exposes USB OTG HS.
- Whether the board exposes MIPI DSI lanes and DPHY power.
- Which pins are tied to onboard peripherals, boot straps, flash/PSRAM, USB, or power-management circuits.
- Whether any currently free pins have unsafe reset/default pull states for console-bus attachment.
- Whether RGB LCD or I80 can be wired while preserving the current GBC source wiring.

## Experiment Results

2026-05-11:

- Current board can run the lab firmware and production mirror firmware.
- Current board can capture GBC LCD bus enough to show visible frames.
- Current board can drive the SPI LCD debug panel with RGB666 writes.
- Current board can flash through WCH UART recovery path.
- SPI2 IO_MUX pins `7/8/9` were electrically active on scope, but did not produce a working LCD image in the attempted wiring/profile.
- macOS USB scan during this audit showed no connected ESP32-P4 serial devices at that moment; only Bluetooth/Jabra serial ports were listed under `/dev/cu.*`. Board identification could not be derived from USB descriptors in that state.
- Local repo search found no schematic, official board model, or board-specific pinout file beyond the current hand-recorded wiring.

## Next Steps

1. Record the exact board model and photograph or link the pinout/schematic.
2. Create a board pin table with these columns:
   - GPIO number
   - header label
   - current owner
   - reset/default concern
   - SoC peripheral IO_MUX role
   - GPIO-matrix usable
   - safe for source input
   - safe for destination output
3. Confirm whether a USB HS OTG connector is available and enumerable from macOS.
4. Check whether RGB LCD, I80, DSI, or CSI pins are physically exposed.
5. Do not rewire major destination paths until the pin table shows the conflicts against current GBC source ownership.
