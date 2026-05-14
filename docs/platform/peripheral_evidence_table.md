# ESP32-P4 Peripheral Evidence Table

Purpose: rank ESP32-P4 peripherals by project relevance and evidence quality.

Status: active research decision table.

Last updated: 2026-05-11.

## Objective

Make the next research experiments obvious by showing which peripherals are supported by the chip, exposed by the board, and proven in this project.

This matters because the project should not confuse an ESP32-P4 feature with a production-ready system block.

## Evidence Levels

| Level | Meaning |
|---|---|
| 0 | Not relevant or not supported |
| 1 | ESP32-P4 SoC capability exists |
| 2 | Official ESP-IDF driver/example exists |
| 3 | Current board exposure is known |
| 4 | Project has measured/proven it |

Only level 4 can be used as a production claim.

## Current Understanding

| Block | Role | Level | Evidence | Current Decision |
|---|---|---:|---|---|
| LCD_CAM DVP | Parallel source capture | 4 | GBC visible frames through LCD_CAM/GDMA path | Continue toward persistent 59.7 fps source |
| ISP DVP | Alternate source capture | 4 | Started and returned complete zero buffers | Keep as reference; not current hot path |
| MIPI CSI | Camera/source input | 2 | SoC support and examples | Defer until board connector is known |
| GPIO/PCNT | Timing discovery | 4 | DCLK and marker measurements | Keep as lab instrumentation |
| RMT | Pulse capture/generation | 2 | SoC support with DMA | Potential diagnostic tool, not video path |
| PARLIO RX/TX | Flexible parallel IO | 2 | SoC support and examples | Advanced fallback; not first production path |
| GDMA / AXI GDMA | Memory movement | 4 | Used under LCD_CAM/SPI drivers | Use via drivers first; custom ring if needed |
| DMA2D | 2D memory movement | 2 | SoC support and JPEG internals | Benchmark for stride/layout work |
| PPA | Scale/rotate/mirror/blend/fill | 2 | Official docs and `ppa_dsi` example | Benchmark after source-only capture |
| SIMD | CPU conversion acceleration | 1 | `soc_caps.h` support | Consider only after measuring conversion bottlenecks |
| JPEG codec | Encode/decode snapshots | 2 | Hardware JPEG examples | Useful for evidence export, not live bridge first |
| H.264 encoder | Compressed video stream | 1 | Register/interrupt evidence and Espressif board docs; no local high-level driver/example found | Later research only; not a prerequisite for raw full-rate bridge |
| SPI LCD | Low-pin-count destination | 4 | Current RGB666 panel works | Debug destination only unless a faster panel mode is proven |
| I80 LCD | Parallel destination | 2 | Official driver/example, 24-bit SoC capability | Strong next destination benchmark if pins/panel available |
| RGB LCD | Continuous video destination | 2 | Official driver/example, 24-bit SoC capability | Strong production candidate if pins/panel available |
| MIPI DSI | High-bandwidth display | 2 | Official driver/examples | Best candidate if board exposes connector/PHY power |
| USB Serial/JTAG | Control/log/low-FPS stream | 4 | Current app protocol and browser | Keep control plane; not full-rate data plane |
| USB OTG device | High-speed host stream | 2 | SoC support and TinyUSB docs | Needs board exposure and throughput proof |
| USB host | External USB peripherals | 2 | Official docs | Not the frame-stream-to-computer path |
| WCH UART | Recovery/flashing | 4 | ROM chip-id and flashing | Recovery path only |
| SDMMC | Local capture storage | 2 | SoC support and examples | Optional later evidence recorder |

## Performance Targets

Assumption: GBC active source is `160x144` at approximately `59.7 fps`.

| Target | Required Rate | Why It Matters |
|---|---:|---|
| GBC native RGB565 capture | `2.75 MB/s` | Minimum full-rate source/capture-card payload |
| GBC native RGB666 capture | `4.13 MB/s` | Full source color retention |
| 2x RGB565 output | `11.0 MB/s` | First practical scaled display mode |
| 2x RGB666 output | `16.5 MB/s` | Current SPI panel pixel format at 2x |
| 4x RGB565 output | `44.0 MB/s` | IPS-kit-style integer scaling class |
| 4x RGB666 output | `66.0 MB/s` | High-color scaled output class |

Current implication:

- 20 MHz one-bit SPI has a raw ceiling of `2.5 MB/s`.
- It cannot carry native RGB666 at full rate, and cannot carry 2x output.
- Source capture, destination output, and USB transport must be benchmarked separately.

## Architecture Recommendation

Recommended research ranking:

1. **Source hot path**
   - Persistent LCD_CAM/GDMA source ring.
   - No browser frames, no SPI destination.
   - Prove or reject native `59.7 fps` capture into memory.

2. **Destination hot path**
   - Keep current SPI LCD as visual debug.
   - Research I80/RGB/DSI as production destinations.
   - Do write-only bandwidth tests before connecting them to GBC.

3. **Computer data plane**
   - Keep USB Serial/JTAG for commands.
   - Test TinyUSB device vendor/bulk on the correct USB connector.
   - Target more than `3 MB/s` for native RGB565 capture-card mode.

4. **Processing**
   - Benchmark PPA for 2x/4x scale and orientation.
   - Benchmark DMA2D for copy/stride work.
   - Use SIMD only if CPU conversion becomes a measured bottleneck.

5. **Evidence/export**
   - JPEG encoder is useful for snapshots, reports, and compressed capture artifacts.
   - H.264 may become useful for compressed video streaming later, but it should not distract from the live bridge path until source/destination rates are solved.

The custom PCB should be treated as an enabler for these blocks, not the primary research object. The performance question is whether the internal ESP32-P4 chain can move frames through source ingress, ring buffers, processing accelerators, and sinks at the required cadence.

Detailed internal benchmark plan:

- `docs/platform/esp32p4_internal_dataflow_plan.md`

## Unknowns

- Exact board exposure for USB HS OTG, DSI, RGB LCD, I80, and CSI.
- Whether current GBC source capture can be made truly continuous without per-frame rearm overhead.
- Whether I80/RGB/DSI can coexist with the current GBC source pin usage.
- Whether the current SPI LCD controller has a usable higher-speed or lower-byte-per-pixel mode.
- Whether hardware isolation is needed before more source modules are connected.

## Experiment Results

Known project evidence:

- GBC source visible in browser and SPI LCD.
- Current SPI LCD working mode is RGB666.
- RGB565 writes to current SPI LCD did not produce useful visible pixels with the current init sequence.
- Current production mirror overlap reached about `29.86 fps`, below GBC native frame rate.
- USB Serial/JTAG synthetic RGB565-sized stream reached about `9 fps`, below full-rate capture-card needs.
- Internal synthetic pipeline showed ESP32-P4 CPU/memory can handle native-size frame processing when source/destination transport is removed.

## Next Steps

1. Build a source-only persistent capture benchmark.
2. Add a shared frame-ring benchmark/result schema.
3. Benchmark PPA SRM and DMA2D copies with synthetic frames.
4. Build a TinyUSB device throughput benchmark only after confirming the board's USB OTG connector.
5. Choose the next destination candidate from board exposure:
   - I80 if a parallel TFT panel is practical.
   - RGB LCD if enough pins and a panel are available.
   - DSI if the board exposes the DSI connector and compatible panel path.
6. Keep all experiments isolated by profile so a failed pin/peripheral attempt cannot break the known-good GBC + SPI debug baseline.
