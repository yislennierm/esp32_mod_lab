# ESP32-P4 Modder-Style Research Plan

## Objective

Investigate the ESP32-P4 as a console display-bus lab and bridge platform using the same attitude serious mod creators use: start from working evidence, study existing high-performance approaches, isolate bottlenecks, and only then choose the production architecture.

This research is not limited to the Game Boy Color. The GBC is the first source module and a real proving ground for a broader source -> processing -> destination lab system.

## Current Understanding

The current project has proven a complete path:

```text
GBC LCD bus -> ESP32-P4 LCD_CAM capture -> RGB565 source buffer
             -> RGB666 SPI LCD destination -> visible image
```

Confidence: high that the source signal is understood well enough for visible GBC video. Confidence: high that the current SPI LCD destination is the main display bottleneck.

The mod-scene pattern is different from our current temporary bridge:

- Production display mods avoid slow CPU-driven pixel push loops.
- Serious video-out/consolizer projects often use FPGA or dedicated video pipelines.
- Successful screen kits advertise frame-locked output, integer scaling, and very low/no conversion latency.
- Current GBC IPS kits commonly do 4x scaling and visual effects in dedicated hardware, not through a generic software loop.

Relevant public mod references:

- BennVenn/RetroRGB GBC backlight notes reported "4x linear up-scaling", "zero conversion lag", and frame-locked ~59.9 Hz behavior for a GBC backlit solution: https://www.retrorgb.com/simpler-and-improved-bennvenn-game-boy-color-backlight-mod.html
- Gamebox Systems GBHD Color is a GBC HDMI/consolizer project using a donor motherboard and digital video output: https://www.retrorgb.com/gbhd-color-consolizer-by-gamebox-systems.html
- Gamebox Systems GBADVI uses Spartan-6 FPGA hardware and a flex interface for GBA DVI output: https://www.retrorgb.com/gbadvi-by-gamebox-systems.html
- Older GBA video-out hacks used FPGA translation from handheld LCD signals to monitor output: https://hackaday.com/2011/07/24/going-a-long-way-for-game-boy-advanced-video-out/
- Open FPGA handheld designs such as Game Bub separate video-critical FPGA work from MCU support tasks: https://github.com/elipsitz/gamebub

These are not direct ESP32-P4 recipes, but they are strong architectural signals.

## ESP32-P4 Capabilities To Study

Primary official sources:

- ESP32-P4 datasheet: https://documentation.espressif.com/esp32-p4_datasheet_en.html
- ESP32-P4 technical reference manual: https://documentation.espressif.com/esp32-p4_technical_reference_manual_en.pdf
- ESP32-P4 GPIO documentation: https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/gpio.html
- Camera controller / DVP capture: https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/camera_driver.html
- SPI master: https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/api-reference/peripherals/spi_master.html
- SPI LCD: https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/api-reference/peripherals/lcd/spi_lcd.html
- I80 LCD: https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/api-reference/peripherals/lcd/i80_lcd.html
- RGB LCD: https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/lcd/rgb_lcd.html
- MIPI DSI LCD: https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/api-reference/peripherals/lcd/dsi_lcd.html
- PPA: https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/api-reference/peripherals/ppa.html
- JPEG codec: https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/api-reference/peripherals/jpeg.html
- USB device: https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/usb_device.html
- USB Serial/JTAG console: https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/api-guides/usb-serial-jtag-console.html
- Espressif ESP32-P4X-Function-EV-Board user guide, useful as an official multimedia-board reference pattern: https://documentation.espressif.com/esp-dev-kits/en/latest/esp32p4/esp32-p4x-function-ev-board/user_guide.html

Primary local capability source:

- `/Users/nene/esp/v5.5/esp-idf/components/soc/esp32p4/include/soc/soc_caps.h`

Local ESP-IDF examples found in `/Users/nene/esp/v5.5/esp-idf/examples`:

- `peripherals/camera/dvp_isp_dsi`: DVP camera capture displayed through DSI.
- `peripherals/camera/mipi_isp_dsi`: MIPI camera through ISP to DSI.
- `peripherals/lcd/rgb_panel`: RGB LCD output with framebuffers and bounce buffers.
- `peripherals/lcd/i80_controller`: Intel 8080 parallel LCD output.
- `peripherals/lcd/mipi_dsi`: MIPI DSI panel output.
- `peripherals/lcd/spi_lcd_touch`: SPI LCD reference path.
- `peripherals/ppa/ppa_dsi`: PPA scale/rotate/mirror/blend/fill to DSI.
- `peripherals/jpeg/jpeg_encode`: hardware JPEG encoder.
- `peripherals/jpeg/jpeg_decode`: hardware JPEG decoder.
- `peripherals/usb/device/tusb_serial_device`: TinyUSB CDC device.
- `peripherals/usb/device/tusb_ncm`: USB network class.
- `peripherals/parlio/*`: parallel IO and bitscrambler examples.

Detailed capability matrix:

- `docs/platform/esp32p4_capability_matrix.md`
- `docs/platform/esp32p4_internal_dataflow_plan.md`

## Research Completeness Assessment

The earlier research correctly identified LCD_CAM/DVP, SPI LCD, PPA, USB, I80, RGB LCD, and DSI. The missing piece was a formal ESP32-P4 coverage checklist that prevents us from forgetting relevant blocks as the project grows beyond GBC.

The current checklist now explicitly includes:

- source/capture: LCD_CAM DVP, ISP DVP, MIPI CSI, PARLIO RX, GPIO/PCNT/RMT diagnostics
- memory movement: GDMA, AXI GDMA, 2D DMA/DMA2D, PSRAM DMA, cache/PSRAM XIP
- processing: CPU, SIMD, PPA, JPEG codec
- compression research: JPEG codec now, H.264 later if ESP-IDF driver support becomes practical
- destination: SPI, I80, RGB LCD, MIPI DSI, PARLIO TX
- computer link: USB Serial/JTAG, USB OTG device, TinyUSB CDC/vendor, UART recovery
- scheduling/instrumentation: dual HP cores, ETM, GPTimer/PCNT, SDMMC storage

The most important lesson is to qualify every future statement with one of these evidence levels:

1. ESP32-P4 SoC supports it.
2. ESP-IDF driver/example exists.
3. Our board exposes it.
4. Our project measured it.

Only level 4 is allowed to become a production claim.

Hardware-block research must also be isolated from the full lab firmware before it becomes a level-4 claim. The required path is:

```text
official docs / examples
    -> isolated experiment firmware in experiments/
    -> JSON counters and captured evidence
    -> lab workbench integration
    -> production profile
```

This prevents lab-mode code, browser streaming, destination drawing, compatibility commands, and debug behavior from hiding the real behavior of LCD_CAM, GDMA, PPA, USB, I80, RGB, DSI, or other silicon blocks.

Performance research priority:

```text
LCD_CAM/GDMA persistent capture
    -> DMA-capable frame ring
    -> PPA / DMA2D / CPU-SIMD only where benchmarked useful
    -> high-bandwidth sink
```

The current dev board and future custom PCB should be considered support hardware for exposing these blocks. The central research question is whether the ESP32-P4 internal blocks can sustain the source cadence with enough margin.

## Findings So Far

### Current SPI LCD Is A Debug Destination

The current panel appears to require RGB666 payloads in the working mode. At 2x GBC scale:

```text
160 * 144 * 2 * 2 * 3 bytes * 59.7 fps ~= 16.5 MB/s
```

That is about 132 Mbit/s before command/address-window overhead. A 20 MHz one-bit SPI link cannot carry that full-rate stream. Optimizing software can reduce overhead and artifacts, but it cannot defeat the link bandwidth limit.

### RGB565 Source Capture Is Still A Reasonable Internal Format

Capturing RGB666 from the GBC would not automatically improve the current SPI LCD path:

- It would increase source capture payload size.
- The destination already receives RGB666 bytes.
- It might remove some bit expansion work, but the main limit is transport bandwidth.

RGB666 capture should be tested later for fidelity, not assumed to be a performance fix.

### PPA Is Highly Relevant

The ESP32-P4 PPA supports scale, rotation, mirror, blend, and fill. This maps directly to mod features:

- integer scaling
- orientation correction
- LCD shader/pixel-effect experiments
- scanline or overlay work
- potentially CPU-free preprocessing before display output

The first useful PPA benchmark should be:

```text
RGB565 160x144 -> RGB565 or RGB888/RGB666-like 320x288
```

Then compare PPA time against the current software scaler.

New concrete PPA details from the official docs and local example:

- SRM input and output buffers must be different.
- Scale precision is truncated to 1/16 increments.
- Blocking and non-blocking transaction modes exist.
- The local `ppa_dsi` example demonstrates RGB565 SRM operations, including scale, rotate, and mirror.

### RGB/I80/DSI Are Serious Destination Candidates

From ESP-IDF examples:

- RGB LCD supports 8/16/24-bit data width, DMA, PSRAM framebuffers, double buffering, and bounce buffers.
- I80 supports 8/16-bit parallel controller displays.
- DSI is the highest-potential integrated display path if the board/panel exposes usable DSI pins and a compatible panel.

For a production bridge, the destination should likely be one of:

1. RGB parallel LCD.
2. I80 parallel LCD.
3. MIPI DSI panel.
4. USB HS capture output to computer.

SPI LCD remains valuable for lab feedback, but should not define the final performance expectation.

The local `camera/dvp_isp_dsi` example is especially important because it already demonstrates a high-level bridge-like structure:

```text
DVP camera capture -> camera buffer -> DSI panel draw/update loop
```

It uses camera callbacks for new/finished transactions and then repeatedly calls `esp_cam_ctlr_receive()`. This is much closer to a production bridge than our current command-oriented frame capture path.

### USB HS Is A Real Capture-Card Candidate

The ESP-USB documentation states that ESP32-P4 has a USB High-Speed peripheral and that TinyUSB defaults to the high-speed port on ESP32-P4. The vendor-specific interface has configurable endpoint and FIFO sizes; the docs explicitly note that vendor endpoint buffer size has major performance impact and recommend `8192` bytes for best performance.

This matters because native GBC RGB565 capture only needs about:

```text
160 * 144 * 2 bytes * 59.7 fps ~= 2.75 MB/s
```

That should be within USB HS capability if the correct board connector and TinyUSB vendor path are used.

## Unknowns

- Whether our board exposes the required MIPI DSI pins in a usable way.
- Whether a practical RGB or I80 panel can be wired while keeping the GBC source pins connected.
- Whether ESP-IDF's camera DVP driver can be made to continuously capture the GBC bus without frame rearm penalties.
- Whether raw LCD_CAM register control is needed beyond the official camera controller.
- Whether PPA supports the exact color modes and memory layouts we need without extra conversion copies.
- Whether TinyUSB HS on this board is available through the USB connector we are using, or if the current USB port is only Serial/JTAG/full-speed.
- Whether the current GBC source capture can be synchronized continuously at true ~59.7 fps without duplicate/tearing artifacts.

## Experiment Results To Date

- GBC boot image captured and reconstructed in browser and on SPI LCD.
- Current known-good source mode is RGB565.
- Current known-good SPI LCD pins: `CS=52`, `MOSI=31`, `SCK=28`, `RESET=29`, `D/C=53`.
- Current SPI LCD working mode is RGB666 destination writes.
- Panel-corrected orientation uses `MADCTL=0xE8`; source-straight diagnostic mode uses `MADCTL=0x08`.
- 2x nearest-neighbor scaling works visually, but stresses SPI bandwidth.
- Batched 2x scaling improved transaction structure but exposed DMA buffer lifetime artifacts; mitigation is alternating DMA buffers plus `trans_queue_depth=1`.

## Research Phases

### Phase A: Source Capture Like A Video Front End

Goal: prove continuous GBC source capture independent of any display output.

Tasks:

- Study `esp_driver_cam/dvp` and `esp_cam_ctlr_dvp` internals.
- Build and run isolated source-only firmware that captures into source buffers without lab/browser/destination code.
- Count valid frames per second without drawing.
- Detect duplicate frames, dropped frames, partial frames, and sync loss.
- Record `capture_us`, `rearm_us`, `frame_interval_us`, and buffer overrun counters.

Current isolated app:

- `experiments/source_ring_bench/`
- build: `./scripts/build_source_ring_bench.sh`
- flash: `./scripts/flash_source_ring_bench.sh <serial-port>`
- current path: GBC LCD source profile -> LCD_CAM/GDMA -> double-buffer rearm -> JSON counters.
- excluded by design: lab command protocol, browser stream, SPI LCD destination, TinyUSB, PNG rendering, and frame payload streaming.

Success condition:

- Stable ~59.7 fps source capture into memory, or a clear measured reason why the current DVP configuration cannot do it.

### Phase A2: Frame Ring And Block Telemetry

Goal: decouple source capture from processing and sinks.

Tasks:

- Add 2 or 3 DMA-capable frame slots.
- Add sequence numbers and timestamps to each frame.
- Track ring occupancy, drops, late consumers, partial frames, and sync loss.
- Report counters only over the control path while the hot path runs.

Success condition:

- A source producer can run independently from a null or memory sink, with measured ring behavior and no hidden frame loss.

### Phase B: Destination Output Without Source

Goal: measure each destination independent of GBC capture.

Tasks:

- SPI LCD write-only benchmark for 1x and 2x RGB666.
- I80 LCD example build and pin feasibility study.
- RGB LCD example build and pin feasibility study.
- MIPI DSI example build and board/panel feasibility study.
- Optional PARLIO output investigation for custom parallel displays.

Success condition:

- A measured destination path capable of at least the GBC native frame rate for the intended resolution and color depth.

### Phase C: PPA And DMA2D Processing Benchmarks

Goal: determine whether PPA and DMA2D should own scaling, orientation, crop, stride, and copy work.

Tasks:

- Build a PPA-only benchmark with synthetic `160x144` RGB565 frames.
- Test 2x integer scaling to `320x288`.
- Test mirror/rotation separately.
- Test output buffers in internal RAM vs PSRAM.
- Compare blocking and non-blocking transaction modes.
- Build a DMA2D copy benchmark for contiguous copy, source-stride to packed copy, crop extraction, and line-block movement.

Success condition:

- PPA and/or DMA2D complete their assigned work comfortably inside the frame budget, or we document their limitations and keep the CPU path for that operation.

### Phase D: Computer Capture Path

Goal: evaluate whether ESP32-P4 can act like a capture device.

Tasks:

- Identify whether the currently connected USB port is USB Serial/JTAG, USB FS OTG, or USB HS OTG.
- Build TinyUSB throughput benchmark for bulk/vendor or CDC.
- Measure practical host receive throughput on macOS.
- Compare raw RGB565 frame stream requirements:

```text
160 * 144 * 2 bytes * 59.7 fps ~= 2.75 MB/s
```

Success condition:

- Sustained transfer above 3 MB/s for raw GBC RGB565, or a clear decision to use compression/frame dropping.

### Phase E: Integrated Production Candidate

Goal: combine only proven blocks.

Candidate topology:

```text
GBC source DVP capture
    -> frame ring in PSRAM/internal DMA buffers
    -> optional PPA scale/orientation
    -> RGB/I80/DSI destination
```

SPI LCD should remain:

- debug panel
- bring-up display
- visual sanity check
- not the performance target for full-rate scaled video

## Next Steps

1. Add benchmark firmware modes:
   - `source_ring_bench` - implemented as isolated app and lab command baseline
   - `frame_ring_bench`
   - `ppa_srm_bench`
   - `dma2d_copy_bench`
   - `sink_null_bench`
   - `usb_hs_stream_bench`
2. Add a common benchmark result JSON schema.
3. Study and annotate:
   - `examples/peripherals/camera/dvp_isp_dsi/main/dvp_isp_dsi_main.c`
   - `examples/peripherals/lcd/rgb_panel/main/rgb_lcd_example_main.c`
   - `examples/peripherals/lcd/i80_controller/main/i80_controller_example_main.c`
   - `examples/peripherals/ppa/ppa_dsi/main/ppa_dsi_main.c`
   - `examples/peripherals/lcd/mipi_dsi/main/mipi_dsi_lcd_example_main.c`
4. Decide the first fast sink after source/ring benchmarks:
   - RGB parallel LCD,
   - I80 parallel LCD,
   - ESP32-P4 DSI-compatible panel,
   - or USB HS capture focus.
