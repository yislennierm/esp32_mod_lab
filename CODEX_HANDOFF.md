# Codex Handoff

Last updated: 2026-05-14

## Project

This repository is evolving into an ESP32-P4 based signal lab for reverse engineering and reusing console display buses. The current concrete source module is the Game Boy Color LCD bus. The current destination module under test is a SPI LCD panel, likely ST7796S/ILI9486 class, connected as a lab output.

The project goal is not only a GBC screen mod. The larger architecture is:

Source display bus -> ESP32-P4 capture/processing/instrumentation -> Destination display/protocol

The same ESP32-P4 should work as both an investigation instrument and, later, a production firmware target.

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
- `docs/architecture.md`
- `docs/capture_pipeline.md`
- `docs/destination_spi_lcd_lab.md`
- `docs/production_modes.md`
- `docs/platform/esp32p4_internal_dataflow_plan.md`
- `docs/experiment_log.md`

