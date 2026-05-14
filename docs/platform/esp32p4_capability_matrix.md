# ESP32-P4 Capability Matrix

Purpose: map ESP32-P4 hardware/software blocks to the console signal lab pipeline.

Status: working research document. Use this to choose benchmark order and production architecture candidates.

Last updated: 2026-05-11.

## Coverage Assessment

This matrix separates four evidence levels:

| Level | Meaning | How To Use It |
|---|---|---|
| SoC capability | Present in ESP-IDF `soc_caps.h` for ESP32-P4 | Valid reason to study or benchmark a block |
| Official driver/example | ESP-IDF has a driver or example using the block | Preferred starting point for implementation |
| Board exposure | The board exposes the required pins, power rails, or connector | Required before rewiring or buying a panel |
| Project proof | We have measured it in this project | Required before calling a path production-ready |

Important correction: **SoC capability is not the same as board capability**. ESP32-P4 has several video-oriented blocks, but this board may not expose the needed pins, connectors, PHY power, or pin groups in a usable way.

Performance correction: the immediate research focus is **internal ESP32-P4 dataflow**, not the custom PCB. Board work should expose the right blocks cleanly, but the speed question is answered by isolated benchmarks of LCD_CAM/GDMA, frame rings, PPA, DMA2D, USB device, and display sinks.

## ESP32-P4 Blocks We Must Not Miss

This list is derived from `/Users/nene/esp/v5.5/esp-idf/components/soc/esp32p4/include/soc/soc_caps.h` plus local ESP-IDF examples.

| Block | SoC Evidence | Relevance | Current Project Status |
|---|---|---|---|
| LCD_CAM DVP camera | `SOC_LCDCAM_SUPPORTED`, `SOC_LCDCAM_CAM_SUPPORTED`, `SOC_LCDCAM_CAM_DATA_WIDTH_MAX=16` | Primary candidate for parallel console source capture | Proven enough for GBC visible frames, not yet proven as continuous 59.7 fps source |
| ISP DVP | `SOC_ISP_SUPPORTED`, `SOC_ISP_DVP_SUPPORTED`, `SOC_ISP_DVP_DATA_WIDTH_MAX=16` | Alternate capture path, useful when ISP format conversion helps | Tested earlier and produced full zero buffers; keep as reference, not current source path |
| MIPI CSI | `SOC_MIPI_CSI_SUPPORTED` | Future camera/source capture, not GBC LCD flex capture | Not tested |
| RGB LCD output | `SOC_LCDCAM_RGB_LCD_SUPPORTED`, `SOC_LCD_RGB_SUPPORTED`, `SOC_LCDCAM_RGB_DATA_WIDTH=24` | Serious production display path | Not tested; needs pin/panel feasibility |
| I80 LCD output | `SOC_LCDCAM_I80_LCD_SUPPORTED`, `SOC_LCD_I80_SUPPORTED`, `SOC_LCDCAM_I80_BUS_WIDTH=24` | Strong parallel display target for MCU-friendly TFT panels | Not tested; likely more realistic than SPI for high FPS |
| MIPI DSI output | `SOC_MIPI_DSI_SUPPORTED` | Highest-potential integrated display path if board/panel support exists | Not tested; board connector/PHY power unknown |
| SPI master / SPI LCD | `SOC_GPSPI_SUPPORTED`, `SOC_SPI_PERIPH_NUM=3`, multiline support | Debug LCDs, low-pin-count panels, QSPI experiments | Working with current RGB666 SPI panel at 20 MHz; bandwidth-limited |
| PARLIO | `SOC_PARLIO_SUPPORTED`, TX/RX max data width 16, SPI/I80 LCD support | Flexible custom source/destination experiments and logic-analyzer style tools | Not tested; advanced fallback after standard LCD paths |
| GDMA / AXI GDMA | `SOC_GDMA_SUPPORTED`, `SOC_AHB_GDMA_SUPPORTED`, `SOC_AXI_GDMA_SUPPORTED`, PSRAM support | Source capture and display transfer backbone | Used indirectly through drivers; custom continuous ring still open |
| 2D DMA / DMA2D | `SOC_DMA2D_SUPPORTED`, 3 TX and 2 RX channels | Async copies, stride-aware moves, JPEG support, possible frame layout work | Not directly benchmarked |
| PPA | `SOC_PPA_SUPPORTED` | Hardware scale, rotate, mirror, blend, fill | Not yet benchmarked; strong candidate for scaling/orientation |
| JPEG codec | `SOC_JPEG_CODEC_SUPPORTED`, encode and decode supported | Optional compressed capture/logging path, not zero-latency display path | Not tested; useful for archival/browser snapshots, not first-line bridge |
| H.264 encoder | Register/interrupt evidence exists in ESP-IDF for ESP32-P4 and Espressif board docs mention H264 Encoder | Optional compressed video transport/research path | No high-level ESP-IDF driver/example found locally; low priority until raw source/display paths are solved |
| USB OTG | `SOC_USB_OTG_SUPPORTED`, two OTG peripherals, one UTMI PHY | High-speed device stream or host tests | Current live view uses USB Serial/JTAG, not a proven HS bulk data plane |
| USB Serial/JTAG | `SOC_USB_SERIAL_JTAG_SUPPORTED` | Flash/log/control and low-FPS lab stream | Proven reliable for control; measured as too slow for full-rate RGB565 frame stream |
| PSRAM DMA | `SOC_SPIRAM_SUPPORTED`, `SOC_PSRAM_DMA_CAPABLE` | Frame buffers and large DMA descriptors | Enabled and necessary for DVP buffer allocation |
| Cache / PSRAM XIP | shared I/D cache, writeback cache, `SOC_SPIRAM_XIP_SUPPORTED` | Performance tuning when code/data pressure affects video DMA | Not benchmarked |
| Dual HP cores | `SOC_CPU_CORES_NUM=2`, multiple HP cores | Split capture/control/display tasks | Command task pinned earlier; production overlap uses tasks |
| SIMD | `SOC_SIMD_INSTRUCTION_SUPPORTED`, preferred alignment 16 | Fast software color conversion if hardware path is not available | Not benchmarked; possible later optimization |
| PCNT / GPTimer / ETM | PCNT, timer, ETM support | Timing discovery, edge counting, hardware-triggered experiments | PCNT-style timing is useful for lab diagnostics; ETM not yet used |
| RMT | RX/TX with DMA support | Pulse-level diagnostics, low-width capture experiments | Not tested; not primary video path |
| SDMMC | SDMMC with PSRAM DMA and UHS-I support | Local capture recording when USB is not enough | Not tested; requires storage wiring and power planning |

Low-priority ESP32-P4 blocks for this project: crypto accelerators, touch, analog comparators, LP peripherals, radio/network features, and general wireless examples. They should not distract from source capture, memory movement, display output, and USB/device transport.

## Pipeline View

```text
console source bus
    -> capture front end
    -> LCD_CAM/GDMA or equivalent source ingress
    -> DMA-capable frame ring
    -> PPA / DMA2D / CPU-SIMD processing where measured useful
    -> high-bandwidth destination output or USB device transport
```

Detailed internal speed plan:

- `docs/platform/esp32p4_internal_dataflow_plan.md`

## Source Capture Blocks

| Block | Evidence | Project Use | Current Assessment | Next Test |
|---|---|---|---|---|
| LCD_CAM DVP camera controller | Official camera driver supports LCD_CAM DVP via `esp_cam_new_lcd_cam_ctlr()` | Direct digital source capture | Strong candidate for GBC-like LCD buses if sync mapping works | Build source-only continuous receive benchmark |
| ISP DVP camera controller | Local `camera/dvp_isp_dsi` example uses `esp_cam_new_isp_dvp_ctlr()` | DVP capture plus ISP conversion | Useful reference, but ISP path may be unnecessary for already-digital RGB bus | Compare against current LCD_CAM path |
| Raw LCD_CAM register path | TRM exposes LCD_CAM data-format and camera-control registers | Escape hatch for non-camera LCD semantics | High complexity, should follow official driver tests | Only use after driver limits are proven |
| GPIO polling / PCNT | Existing lab features | Timing discovery, safety, low-rate diagnostics | Useful for investigation, not video capture | Keep as lab instrumentation |

Important local source files:

- `/Users/nene/esp/v5.5/esp-idf/components/esp_driver_cam/dvp/src/esp_cam_ctlr_dvp_cam.c`
- `/Users/nene/esp/v5.5/esp-idf/components/esp_driver_cam/dvp/src/esp_cam_ctlr_dvp_gdma.c`
- `/Users/nene/esp/v5.5/esp-idf/components/esp_driver_cam/isp_dvp/src/esp_cam_ctlr_isp_dvp.c`
- `/Users/nene/esp/v5.5/esp-idf/examples/peripherals/camera/dvp_isp_dsi/main/dvp_isp_dsi_main.c`

Concrete API names to target:

- `esp_cam_new_dvp_ctlr()` for LCD_CAM DVP.
- `esp_cam_new_isp_dvp_ctlr()` for ISP DVP.
- `esp_cam_ctlr_receive()` for continuous receive loops.
- `esp_cam_ctlr_register_event_callbacks()` with `on_get_new_trans` and `on_trans_finished`.
- `esp_cam_ctlr_trans_t` for buffer handoff.

Key research clue from `dvp_isp_dsi_main.c`:

- The example allocates a camera buffer, registers callbacks, starts the camera controller, and then loops on `esp_cam_ctlr_receive()`.
- Display update is decoupled through DSI panel callbacks. This is closer to a real production bridge than the current command/rearm capture path.

## Processing Blocks

| Block | Evidence | Project Use | Current Assessment | Next Test |
|---|---|---|---|---|
| CPU C loops | Existing production bridge | Simple color expansion and debug transforms | Works, but wastes CPU on scaling/color packing | Keep simple conversion only |
| PPA SRM | Official PPA docs and `ppa_dsi` example | Scale, rotate, mirror | Strong candidate for 2x/4x scaling and orientation | Benchmark `160x144 RGB565 -> 320x288 RGB565` |
| PPA blend/fill | Official PPA docs and example | Overlays, masks, borders, pixel effects | Useful for polished display modes later | Defer until scaling bench passes |
| 2D-DMA / DMA2D | MIPI DSI example has DMA2D copy option | Async framebuffer copies | Likely useful for full display pipeline | Study DSI example and `esp_lcd` internals |
| GDMA | Datasheet/TRM and camera/SPI drivers | Peripheral-to-memory and memory-to-peripheral transfer | Already under driver abstractions | Avoid custom GDMA until official driver limits are known |

PPA constraints found:

- SRM input and output buffers must be different.
- SRM scale precision is truncated to 1/16 steps.
- PPA depends heavily on memory bandwidth, especially if buffers are in PSRAM.
- Non-blocking PPA mode exists; callbacks run in interrupt context.

Concrete API names to target:

- `ppa_register_client()`
- `ppa_do_scale_rotate_mirror()`
- `ppa_srm_oper_config_t`
- `PPA_SRM_COLOR_MODE_RGB565`
- `PPA_TRANS_MODE_BLOCKING`
- `PPA_TRANS_MODE_NON_BLOCKING`

Primary local PPA reference:

- `/Users/nene/esp/v5.5/esp-idf/examples/peripherals/ppa/ppa_dsi/main/ppa_dsi_main.c`
- `/Users/nene/esp/v5.5/esp-idf/components/esp_driver_ppa/include/driver/ppa.h`

## Destination Blocks

| Destination | Evidence | Bandwidth Shape | Project Use | Current Assessment | Next Test |
|---|---|---|---|---|---|
| SPI LCD RGB666 | Current working hardware | 1-bit serial, currently 20 MHz | Debug display | Proven but bandwidth-limited | Keep as debug destination |
| I80 LCD 8/16-bit | Official docs and local `i80_controller` example | Parallel WR clock, 8/16 data lines | Practical higher-bandwidth display target | Strong candidate if pins are available | Pin feasibility and write-only benchmark |
| RGB LCD 8/16/24-bit | Official docs and local `rgb_panel` example | Continuous pixel clock DMA | Serious video output | Strong production candidate if panel/pins available | Pin feasibility and minimal RGB panel build |
| MIPI DSI | Official docs and local `mipi_dsi`, `dvp_isp_dsi`, `ppa_dsi` examples | High-bandwidth 2-lane serial video | Best integrated display candidate | Strong if board exposes DSI and compatible panel exists | Board connector/panel feasibility |
| PARLIO | Local examples and driver | Flexible parallel output with bitscrambler | Custom bridge/destination experiments | Possible advanced route | Defer until standard LCD paths tested |

Current SPI LCD limits:

- Working mode sends RGB666, 3 bytes per destination pixel.
- 2x GBC scale at full frame rate needs about `16.5 MB/s` payload before overhead.
- Current 20 MHz one-bit SPI cannot reach that.
- Software optimization can reduce overhead/artifacts, not overcome link bandwidth.
- ESP-IDF's ESP32-P4 SPI master documentation says GPIO matrix routing behaves the same as IO_MUX at `40 MHz` or lower, and lists SPI2 IO_MUX pins as `CS0=7`, `MOSI=8`, `SCLK=9`, `MISO=10`, `QUADWP=11`, `QUADHD=6`.
- Our failed IO_MUX test proved `GPIO7/8/9` can toggle cleanly at the scope, but did not produce LCD pixels. It must remain an isolated experiment, not a replacement for the known-good GPIO-matrix profile.

Important local destination files:

- `/Users/nene/esp/v5.5/esp-idf/examples/peripherals/lcd/i80_controller/main/i80_controller_example_main.c`
- `/Users/nene/esp/v5.5/esp-idf/examples/peripherals/lcd/rgb_panel/main/rgb_lcd_example_main.c`
- `/Users/nene/esp/v5.5/esp-idf/examples/peripherals/lcd/mipi_dsi/main/mipi_dsi_lcd_example_main.c`
- `/Users/nene/esp/v5.5/esp-idf/examples/peripherals/lcd/parlio_simulate/main/parlio_simulate_example_main.c`

## Bandwidth Budget

These are payload-only estimates before command overhead, blanking, DMA descriptor overhead, cache effects, and driver latency.

Assumption: GBC active image is `160x144` at about `59.7 fps`.

| Path | Payload | Bytes/Frame | Payload Rate | Meaning |
|---|---:|---:|---:|---|
| Native RGB565 source | `160*144*2` | `46,080` | `2.75 MB/s` | Minimum useful full-rate source/capture-card stream |
| Native RGB666 source | `160*144*3` | `69,120` | `4.13 MB/s` | Full source color if all 18 bits are retained |
| 2x RGB565 output | `320*288*2` | `184,320` | `11.0 MB/s` | Practical scaled framebuffer payload |
| 2x RGB666 output | `320*288*3` | `276,480` | `16.5 MB/s` | Current SPI panel format at 2x |
| 4x RGB565 output | `640*576*2` | `737,280` | `44.0 MB/s` | Typical IPS-kit style integer scale payload |
| 4x RGB666 output | `640*576*3` | `1,105,920` | `66.0 MB/s` | Too high for one-bit SPI; needs RGB/I80/DSI-style output |

Raw one-bit SPI ceilings:

| SPI Clock | Raw Ceiling | Practical Conclusion |
|---:|---:|---|
| `20 MHz` | `2.5 MB/s` | Cannot carry native RGB666 at full rate, and cannot carry 2x RGB565/RGB666 |
| `40 MHz` | `5.0 MB/s` | Could theoretically carry native RGB666, but not 2x output |
| `80 MHz` | `10.0 MB/s` | Still below 2x RGB565 full-rate payload before overhead |

This is why the current SPI LCD is a debug destination. Full-rate scaled production output should move to RGB, I80, DSI, or a different high-speed panel interface.

## Computer Transport Blocks

| Transport | Evidence | Project Use | Assessment | Next Test |
|---|---|---|---|---|
| USB Serial/JTAG | Current lab command path | Control, logs, low/medium frame stream | Useful but not final high-FPS data plane | Keep for control |
| TinyUSB CDC | Local `tusb_serial_device` example | Easier USB device stream | Better than JSON protocol, still class overhead | Throughput benchmark |
| TinyUSB vendor class | ESP-USB docs mention vendor class and endpoint buffer tuning | High-speed raw frame stream | Strong candidate for capture-card mode | Build vendor bulk benchmark |
| USB HS OTG | ESP-USB docs: default TinyUSB port is HS on ESP32-P4; vendor EP size 8192 recommended for performance | Computer-side raw capture | Strong if board USB connector exposes HS OTG | Confirm connector and measured throughput |
| UART/WCH | Current recovery flash path | Recovery/log fallback | Not video data plane | Keep for flashing/recovery |

USB facts to verify on our board:

- Which connector maps to USB Serial/JTAG.
- Which connector maps to USB HS OTG dedicated pins.
- Whether both can be connected at once for control + high-speed data.
- Whether macOS sees the TinyUSB HS device as high-speed.
- USB host documentation is useful only for plugging USB devices into the ESP32-P4. For streaming frames to the computer, the relevant path is USB **device** mode, most likely TinyUSB vendor/bulk or UVC-style class work.

USB device research details:

- ESP-USB documentation says ESP32-P4 TinyUSB defaults to the high-speed port.
- Vendor-specific TinyUSB is likely a better capture-card data plane than CDC or USB Serial/JTAG.
- `CONFIG_TINYUSB_VENDOR_EPSIZE=8192` is documented as the best-performance endpoint buffer target.
- Vendor RX/TX software buffers can be configured; zero-sized buffering changes callback behavior and may matter for maximum throughput tests.

## Current Evidence Summary

| Claim | Status | Evidence |
|---|---|---|
| GBC timing/source can produce recognizable frames | Proven | Browser and SPI LCD show boot image |
| Source capture path is full-rate | Not proven | Current compatibility/overlap paths are around 30 fps before full-rate destination |
| SPI LCD destination works | Proven | RGB666 fills, bars, one-shot GBC frame, production mirror |
| SPI LCD destination can be full-rate 2x | Disproven for current 20 MHz one-bit path | Payload budget exceeds raw link capacity |
| ESP32-P4 CPU can process native GBC frames at 60 fps | Proven synthetically | Internal pipeline bench exceeded the target when frames are already in RAM |
| USB Serial/JTAG can be capture-card data plane | Disproven for full-rate RGB565 | Measured around 9 fps for RGB565-sized synthetic payloads |
| TinyUSB HS bulk/vendor can carry full-rate RGB565 | Not proven | SoC and docs support the idea; needs benchmark on the board connector |
| RGB/I80/DSI output can solve destination bandwidth | Not proven | SoC and examples support these blocks; requires hardware feasibility and benchmarks |

## Missing Research Checks

These are now the main ESP32-P4 gaps:

1. **Board pin exposure**
   - Map which board headers expose RGB LCD, I80, DSI, USB HS, and clean SPI IO_MUX options.
   - Confirm default/reset states for every pin that can back-power a target.

2. **Continuous source capture**
   - Replace per-frame/rearm assumptions with a persistent LCD_CAM/GDMA source ring.
   - Report frame cadence, descriptor EOF timing, dropped frames, and sync loss without USB frame streaming.

3. **Destination class benchmark**
   - Keep SPI as a known-good debug output.
   - Build write-only benchmarks for I80/RGB/DSI before combining with GBC.

4. **USB device data plane**
   - Build TinyUSB vendor/bulk synthetic payload benchmark.
   - Verify macOS enumerates the correct ESP32-P4 USB port at high speed.

5. **PPA/DMA2D/JPEG**
   - Benchmark PPA for 2x/4x scaling and orientation.
   - Benchmark DMA2D for stride-aware copies.
   - Treat JPEG as optional compressed evidence/export, not a live display bridge unless later measurements justify it.
   - Treat H.264 as later video-compression research only; this ESP-IDF checkout exposes low-level H.264 register/interrupt definitions but no obvious high-level driver/example.

6. **Electrical isolation**
   - Software `SAFE_IDLE`/`ELECTRICAL_ISOLATE` helped, but direct GPIO still caused back-power observations.
   - Future source modules need hardware isolation guidance: series resistors at minimum, preferably bus switches/buffers with partial-power-down behavior.

## Benchmark Priority

1. **Persistent source capture**
   - Goal: prove or disprove ~59.7 fps GBC capture without any display.
   - Metrics: frame interval, captured frames, failed receives, duplicate/partial frame detection, DMA errors.

2. **Frame ring benchmark**
   - Goal: decouple source, processing, and sink timing.
   - Metrics: ring occupancy, drops, latency, producer/consumer cadence.

3. **PPA scaler benchmark**
   - RGB565 160x144 -> RGB565 320x288.
   - Internal RAM vs PSRAM.
   - Blocking vs non-blocking.

4. **DMA2D / async-copy benchmark**
   - Stride-aware copies, crop extraction, line-block moves.
   - Internal RAM vs PSRAM.

5. **Sink benchmarks**
   - Null sink first.
   - USB HS vendor/bulk when available.
   - I80/RGB/DSI write-only when hardware is available.
   - SPI debug sink only for visual sanity.

6. **USB HS vendor benchmark**
   - Sustained host receive speed on macOS.
   - Target: >3 MB/s for raw GBC RGB565 at native FPS.

7. **Integrated candidate**
   - Only combine blocks that have passed isolated benchmarks.

## Architecture Implications

- The current SPI LCD should remain a lab/debug destination.
- Production display should move toward RGB, I80, or DSI.
- PPA should own scaling/orientation if it benchmarks well.
- The source module should remain factual and not hide orientation or display-specific transforms.
- The lab firmware and production firmware should share source/destination modules but run different orchestration policies.
