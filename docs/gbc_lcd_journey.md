# Game Boy Color LCD Journey Report

Web-ready visual version: [gbc_lcd_journey.html](gbc_lcd_journey.html).

## 1. Objective

Tell the story of the Game Boy Color LCD bus investigation using the local artifacts saved during the project.

This matters because the image captures, timing logs, debug failures, and hardware lessons are evidence. They should be preserved in a human-readable narrative before any cleanup, backup, refactor, or archive work moves files around.

This document is intentionally a curated story, not a full dump of every artifact. The complete raw and decoded data remains under `captures/`.

## 2. Current Understanding

Current hypothesis: the GBC LCD work is target module 001 for the broader ESP32-P4 console signal lab.

The journey established these major points:

- The ESP32-P4 can be used as a programmable investigation instrument for the GBC LCD bus.
- The GBC LCD bus does not behave like a simple standard VGA/DVP source where signal names can be blindly mapped to `VSYNC`, `HSYNC`, `DE`, and `PCLK`.
- Useful progress came from preserving raw captures, trying many hypotheses offline, and comparing images visually.
- A stable live RGB565/RGB332-style browser view is now possible.
- Power-cycle stability required a hardware lesson: `CLS` moved from GPIO32 to GPIO3 because GPIO32 appeared to backfeed or disturb the target.

Evidence comes from timing captures, line-clock sessions, red/green/blue data experiments, raw LCD_CAM captures, rendered PNGs, browser workbench tests, and the final power-cycle behavior.

Confidence level: high for the workflow lessons and current working wiring; medium for exact protocol semantics until more cartridge/gameplay captures are studied.

## 3. Unknowns

- Exact electrical reason GPIO32 caused or contributed to backfeed.
- Whether `LP`, `SPL`, `CLS`, and `PS` roles are fully understood or only useful capture aids.
- Whether the current 161-ish byte line model represents a visible width plus a blank/dummy transfer, porch-like behavior, or a capture artifact.
- Final true color packing from the source beyond the currently useful live modes.
- How the bus behaves under cartridge/gameplay content instead of only boot/no-cartridge screens.
- Whether future hardware should use series resistors, buffers, or bus switches even when the current wiring works.

## 4. Experiment Results

### 4.1 Safety And First Contact

The project started with a strict input-only posture. Dangerous analog LCD rails were identified and kept out of ESP32-P4 GPIO:

- `V0-V9`
- `VCOM`
- `VEE`
- `VSHA`
- `VSHD`

Early firmware focused on `PING`, `GET_VERSION`, `GET_PINMAP`, `READ_GPIO`, and edge counting before any display reconstruction was attempted.

Relevant documentation:

- [Project charter](../PROJECT_CHARTER.md)
- [GBC LCD pinout](gbc_lcd_pinout.md)
- [Hardware notes](hardware_notes.md)
- [ESP32-P4 GPIO inventory](esp32p4_gpio_inventory.md)

### 4.2 Timing Discovery

The first strong timing discoveries were made through GPIO edge counts and timestamped timing captures:

- `SPS -> GPIO33` behaved like a stable frame-marker candidate at about 60 Hz.
- `LP`, `PS`, and `CLS` were around line cadence.
- `SPL` was active but did not immediately behave like a simple standard horizontal sync.
- GPIO interrupt edge counting was useful for slow signals, but not valid for measuring a MHz-class pixel clock.

Representative timing artifacts:

- [Timing relationship report](../captures/decoded/timing_relationships/20260507T214244Z-timing_relationships_100ms.md)
- [Steady-state timing session](../captures/experiments/timing_sessions/20260507T215223Z-steady_state_test/session_report.md)
- [Boot power-on timing session](../captures/experiments/timing_sessions/20260507T215311Z-boot_power_on/session_report.md)

Key lesson: slow edge counts and timing relationships are excellent for discovering candidates, but sample-rate and capture-peripheral limits must be documented. Do not pretend a coarse measurement proves a fast clock.

### 4.3 Red-Only And Timing-Edge Data

The first data-bus work used red bits before full color. This lowered risk and made it easier to prove that pixel data was changing relative to timing markers.

Representative artifact:

![Red DCLK diagnostic](../captures/decoded/red_dclk/20260507T222037Z-red_dclk_512samples.png)

Important observation: red data changed in meaningful windows, but timing-edge snapshots were not enough for full image reconstruction. This pushed the project toward line-burst and peripheral-backed capture.

### 4.4 Red/Green Line Bursts And The First Recognizable Shapes

With red and green connected, line-burst captures began producing consistent patterns. They were not correct full frames yet, but they were no longer random noise.

Early line burst:

![RG line burst early](../captures/decoded/rg_line_bursts/20260508T162521Z-rg_line_bursts_160x144.png)

The breakthrough was recognizing that some outputs contained repeated low-resolution views of the boot screen. The image looked "wrong", but it was wrong in a structured way.

Tile/contact-sheet evidence:

![RG 5x5 tile sheet](../captures/decoded/rg_line_bursts/20260508T162521Z-rg_line_bursts_tile_5x5_sheet_large.png)

One extracted tile showed the expected dark boot text shape at low resolution:

![Best low-resolution tile](../captures/decoded/rg_line_bursts/20260508T162521Z-rg_line_bursts_160x144_tile_r3_c0_scaled.png)

Later line-burst captures still showed tiling, proving the issue was systematic:

![Later RG line burst](../captures/decoded/rg_line_bursts/20260508T174921Z-rg_line_bursts_160x144.png)

Key lesson: a "bad" image can still be rich evidence. Repeated/tiled mini-screens suggested the capture was undersampling, packing the stream incorrectly, or using a timing marker incorrectly.

### 4.5 LCD_CAM Raw Capture And Width Sweeps

Moving to LCD_CAM/raw capture made the image much clearer, but the data still needed interpretation. Width sweeps showed multiple possible stream interpretations.

Raw high-resolution stream view:

![Raw high 320x204 stream](../captures/decoded/lcdcam_raw/width_sweeps/20260508T193122Z-lcdcam_raw_high_320x204_stream_w320_h204.png)

The straight, readable but repeated mosaic supported the idea that the data stream contained useful frames but needed correct framing and crop/stride interpretation.

Deskew experiment:

![Deskew p0p00](../captures/decoded/lcdcam_raw/deskew_193122_w320/20260508T193122Z-lcdcam_raw_high_320x204_w320_h204_skewp0p00.png)

Frame extraction experiments made the boot logo nearly visible:

![Frame extract near success](../captures/decoded/lcdcam_raw/frame_extract_193122_sw160/20260508T193122Z-lcdcam_raw_high_320x204_sw160_x0_y240_160x144.png)

Interactive hypothesis sliders produced near-correct alignments:

![Interactive grid near correct](../captures/decoded/lcdcam_raw/interactive_states_193122/grid_x86_y49_line1_finem0p10.png)

Key lesson: once raw capture is coherent, host-side post-processing is the right place to explore row stride, x/y offset, line skew, inversion, and bit operations. Firmware should remain generic as long as possible.

### 4.6 Color Depth Experiments

The project explored red-only, red/green, RGB666 attempts, RGB332 live display, and RGB565-style capture modes.

RGB666 line-burst attempts could reintroduce tiling because the capture packing and throughput assumptions changed:

![RGB666 line burst](../captures/decoded/rgb666_line_bursts/20260509T115056Z-rgb666_line_bursts_160x144.png)

A practical live mode emerged around packed color capture and stable host decoding. The boot screen looked like black text over a yellowish background, matching expectations for the no-cartridge boot state.

Representative later decoded frame:

![RGB332 fast decode](../captures/decoded/lcdcam_raw/porch_test_192x145_x1/20260508T212557Z-lcdcam_raw_high_192x145_gbc_rgb332_fast_v1_frame0_x1_y0.png)

Boot capture frames:

![Boot capture frame 0](../captures/experiments/20260509T161053Z-boot_capture_rgb565/frames/frame_0000.png)

![Boot capture frame 1](../captures/experiments/20260509T161053Z-boot_capture_rgb565/frames/frame_0001.png)

Key lesson: visually plausible color is not enough. The sample edge and bit packing must still be validated with varied content. The current live view is a working diagnostic baseline, not the final protocol proof.

### 4.7 Browser Workbench

The browser evolved from a viewer into an investigation workbench:

- live frame view
- start/stop/recover behavior
- pin readout
- pin-level timeline
- edge scans
- timing capture
- VCD/PulseView export
- profile-aware safety information

Relevant documentation:

- [Investigation workbench](investigation_workbench.md)
- [Universal signal lab](universal_signal_lab.md)

Key lesson: the browser should not only show "the answer". It should expose the investigation process: pin state, timing evidence, hypotheses, logs, and safe recovery.

### 4.8 Power-Cycle And GPIO32 Lesson

The hardest hardware issue was that the GBC sometimes failed to switch back on while connected to the ESP32-P4. Measurements showed the unpowered GBC bus could be biased by the powered ESP32-P4.

Software mitigations were added:

- `SAFE_IDLE`
- `ELECTRICAL_ISOLATE`
- startup isolation
- browser Stop isolation

These improved control but did not fully explain the issue. The decisive practical fix was moving `CLS` from GPIO32 to GPIO3. After this change, the user confirmed the GBC could be switched off and on consistently while capture was running.

Power-cycle monitor artifacts:

- [Power-cycle monitor raw](../captures/experiments/20260509T163645Z-power_cycle_monitor/raw.json)
- [Power-cycle monitor summary](../captures/experiments/20260509T163645Z-power_cycle_monitor/summary.json)

Current working timing/control wiring:

| Signal | ESP32-P4 GPIO | Notes |
|---|---:|---|
| DCLK | 22 | capture clock candidate |
| LP | 21 | line-related candidate |
| PS | 20 | power/blanking/control candidate |
| SPL | 19 | line/start/data-enable candidate |
| CLS | 3 | moved from GPIO32; current working baseline |
| SPS | 33 | frame-marker candidate |

Key lesson: electrical behavior is part of reverse engineering. A pin can be logically valid but physically problematic on the board or target wiring.

## 5. Next Steps

### Preserve This Journey

Recommended backup set:

- `docs/`
- `profiles/`
- `firmware/main/`
- `host/`
- selected capture artifacts referenced in this document
- full `captures/experiments/`
- full `captures/decoded/` if storage allows

Minimum narrative backup:

- This document.
- `docs/project_maintenance.md`.
- `profiles/gbc_lcd.json`.
- `captures/decoded/rg_line_bursts/20260508T162521Z-rg_line_bursts_tile_5x5_sheet_large.png`.
- `captures/decoded/lcdcam_raw/width_sweeps/20260508T193122Z-lcdcam_raw_high_320x204_stream_w320_h204.png`.
- `captures/decoded/lcdcam_raw/frame_extract_193122_sw160/20260508T193122Z-lcdcam_raw_high_320x204_sw160_x0_y240_160x144.png`.
- `captures/decoded/lcdcam_raw/interactive_states_193122/grid_x86_y49_line1_finem0p10.png`.
- `captures/decoded/lcdcam_raw/porch_test_192x145_x1/20260508T212557Z-lcdcam_raw_high_192x145_gbc_rgb332_fast_v1_frame0_x1_y0.png`.
- `captures/experiments/20260509T161053Z-boot_capture_rgb565/frames/frame_0000.png`.

### Continue Technical Work

- Repeat timing measurements with `CLS -> GPIO3` and mark GPIO32 results as historical.
- Capture cartridge/gameplay content to validate color and timing against more varied images.
- Preserve raw captures with manifests for each major hypothesis test.
- Promote the current live capture settings into the GBC target profile as a named working preset.
- Start separating generic platform tools from GBC-specific scripts only after compatibility wrappers exist.

### Open Technical Questions To Carry Forward

- What exact timing role does `CLS` play?
- Is the extra line/byte behavior porch-like blanking, dummy transfer, or capture framing?
- What is the true source color packing?
- Which sample edge is stable across all colors and content?
- Does the original LCD panel provide termination or strap behavior that should be emulated?
- What hardware protection should be added before this becomes a reusable lab fixture?
