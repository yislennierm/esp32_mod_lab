# SPI LCD Destination Lab Module

Purpose: define the first destination-output research module for SPI-connected LCD/IPS panels.

Status: early lab implementation. Destination GPIO validation, manual output claims, and a first generic MIPI-DCS SPI panel path exist.

Last updated: 2026-05-10.

## 1. Objective

Investigate a serial peripheral interface display as the first physical destination for the ESP32-P4 signal lab.

This matters because the project method needs a concrete destination stage that can prove:

- captured or synthetic framebuffers can be displayed locally
- destination output can be tested without disturbing source capture
- future product pipelines can reuse the same destination block
- the browser/AI workbench can control source, processing, and destination blocks separately

The panel connector described by the user is:

| Panel pin | Role | Initial note |
|---|---|---|
| `VCC` | Power | Already powered externally at 5 V. Do not drive from ESP32-P4 GPIO. |
| `GND` | Ground | Must share ground with ESP32-P4. |
| `CS` | SPI chip select | ESP32-P4 output, 3.3 V logic unless level shifting is required. |
| `RESET` | Panel reset | ESP32-P4 output, 3.3 V logic unless level shifting is required. |
| `D/C` | Data/command select | ESP32-P4 output, 3.3 V logic unless level shifting is required. |
| `SDI` | SPI MOSI | ESP32-P4 output, 3.3 V logic unless level shifting is required. |
| `SCK` | SPI clock | ESP32-P4 output, 3.3 V logic unless level shifting is required. |
| `LED` | Backlight power | Already powered externally at 5 V. Do not drive from ESP32-P4 GPIO. |

## 2. Current Understanding

Current hypothesis: the panel is a controller-based SPI LCD, likely ST7796S or ILI9486 based on the user's module identification. The first firmware path uses generic MIPI-DCS commands over SPI rather than an ST7789-specific driver.

Evidence:

- The exposed pins match the common 4-wire SPI LCD pattern: `CS`, `RESET`, `D/C`, `SDI`, `SCK`.
- Separate `LED` power suggests a backlight-driven TFT/IPS module rather than a raw RGB panel.
- ESP-IDF supports SPI LCD panel IO through `esp_lcd_new_panel_io_spi()` and DMA-capable SPI bus setup.

Confidence level: medium that this is a controller-based 320x480 SPI LCD; medium-low for ST7796S vs ILI9486, reset/init sequence, and maximum SPI clock. Color order is currently understood as BGR. RGB666/18-bit is the working pixel transfer mode; RGB565 transactions complete but do not visibly update the panel.

Important distinction:

```text
Lab destination module:
  test patterns -> SPI LCD
  last captured frame -> SPI LCD
  optional low-FPS mirror -> SPI LCD

Not yet:
  real-time GBC pass-through -> SPI LCD at source frame rate
```

## 3. Unknowns

- Exact LCD controller IC: ST7796S vs ILI9486 is not confirmed.
- Native controller memory is currently treated as 320x480. Landscape display should be handled later with controller rotation or framebuffer rotation, not by addressing 480 columns directly.
- Panel orientation and memory-scan direction.
- RGB/BGR color order: observed BGR. Sending RGB666 red (`ff0000`) showed blue, and sending RGB666 blue (`0000ff`) showed red before enabling the controller BGR bit.
- Required initialization command sequence. This module currently behaves like an RGB666/18-bit SPI panel; RGB565 writes are not a working display path with the current init sequence.
- Whether the panel accepts 3.3 V logic when powered at 5 V.
- Whether `RESET`, `CS`, and `D/C` have pullups on the panel module.
- Maximum reliable SPI clock with the chosen wires and board.
- Whether backlight `LED` is current-limited on the module.
- Which ESP32-P4 pins should be allocated without conflicting with current GBC source capture.
- Whether the current source capture path and SPI LCD DMA can run concurrently without resource contention.

## 4. Experiment Results

2026-05-10: Added the first destination GPIO lab layer.

Implemented firmware commands:

```text
DEST_GPIO_STATUS
DEST_GPIO_VALIDATE <signal> <gpio>
DEST_GPIO_CLAIM <signal> <gpio>
DEST_GPIO_SET <signal> <0|1>
DEST_GPIO_PULSE <signal> <0|1> <duration_ms>
DEST_GPIO_RELEASE <signal>
DEST_GPIO_RELEASE ALL
DEST_GPIO_RELEASE_ALL
```

Safety behavior:

- No destination GPIO is configured at boot.
- `DEST_GPIO_VALIDATE` is read-only.
- `DEST_GPIO_CLAIM` rejects GPIOs already owned by the active GBC source profile.
- Claimed pins are configured as output with internal pullups/pulldowns disabled.
- `CS` and `RESET` idle high; other signals idle low.
- `DEST_GPIO_RELEASE`, `SAFE_IDLE`, and `ELECTRICAL_ISOLATE` release destination claims back to disabled/no-pull pads.

Browser support:

- The Destination tab can edit the draft pin map.
- Source-owned GPIOs are disabled in the destination GPIO dropdown.
- The Live Pin Manipulation table can validate, claim, drive low/high, pulse, and release mapped output pins.

Verification:

- Firmware build passed on 2026-05-10.
- Frontend production build passed on 2026-05-10.
- Python backend syntax check passed on 2026-05-10.

2026-05-10: Added first firmware SPI LCD lab implementation.

Implementation:

- Uses `esp_lcd_new_panel_io_spi()` and SPI DMA.
- Avoids ST7789-specific panel driver code.
- Sends a minimal MIPI-DCS init sequence: reset, sleep-out, RGB565 color mode, memory access control, inversion off, display on.
- Draws full-screen RGB565 fills and color-bar test patterns using `CASET`, `RASET`, and `RAMWR`.
- `DEST_SPI_LCD_SAFE_OFF` tears down panel IO, frees the SPI bus, releases destination GPIO claims, and disables the reset GPIO.

No successful SPI LCD image output has been observed yet.

Planned evidence artifacts:

- Panel photo and markings.
- Controller identification notes.
- Wiring table with ESP32-P4 GPIOs.
- Logic-level measurements on `CS`, `RESET`, `D/C`, `SDI`, and `SCK`.
- First test-pattern screenshot.
- SPI clock sweep results.
- Color-order test screenshots.
- Orientation/mirroring test screenshots.
- `DEST_STATUS` telemetry logs.

## 5. Next Steps

### Phase D0 - Identify The Destination

Before firmware drives the panel:

1. Record panel/module name, seller link, markings, or controller IC.
2. Treat current candidate controller as ST7796S/ILI9486 and verify against observed behavior.
3. Confirm whether module logic is 3.3 V compatible when `VCC` is 5 V.
4. Confirm backlight `LED` is externally powered/current-limited.
5. Choose ESP32-P4 output pins that do not overlap the current GBC source wiring.

### Phase D1 - Output Electrical Safety

Rules:

- No destination pins enabled at boot.
- No destination outputs until the browser or host explicitly claims a GPIO through `DEST_GPIO_CLAIM`.
- No SPI panel initialization until `DEST_SPI_LCD_INIT` is explicitly called.
- `DEST_SPI_LCD_SAFE_OFF`, `SAFE_IDLE`, and `ELECTRICAL_ISOLATE` must release destination GPIOs to high impedance or known-safe inactive state.
- Never connect `VCC` or `LED` to ESP32-P4 GPIO.
- Verify shared ground before enabling SPI.

### Phase D1.5 - Manual Destination Pin Control

Goal: make destination pin assignment and oscilloscope probing a controlled browser workflow before any SPI driver is enabled.

Workflow:

1. Add or edit a destination pin in the Destination tab.
2. Select an ESP32-P4 GPIO that is not owned by the active source profile.
3. Press `Validate`.
4. Press `Claim`.
5. Probe the line while using `LOW`, `HIGH`, or `Pulse 100 ms`.
6. Press `Release` before rewiring or changing modules.

This phase is intentionally lower level than the SPI LCD panel driver. It proves the control path and pin ownership model first.

### Phase D2 - Standalone Destination Bring-Up

Goal: prove the panel can be driven without using the GBC source.

Initial commands:

```text
DEST_SPI_LCD_STATUS
DEST_SPI_LCD_INIT
DEST_SPI_LCD_TEST_PATTERN color_bars
DEST_SPI_LCD_TEST_PATTERN checker
DEST_SPI_LCD_TEST_PATTERN rgb565
DEST_SPI_LCD_SAFE_OFF
```

Firmware should report:

- configured GPIOs
- controller driver selected
- resolution
- color format
- SPI clock
- DMA transfer size
- last error
- initialized/enabled state

### Phase D3 - Framebuffer Destination

Goal: display a known framebuffer on the SPI panel.

Inputs:

- synthetic RGB565 framebuffer generated in firmware
- host-sent RGB565 frame
- last source frame from `GBC_SOURCE_FRAME_BIN`, only after standalone test patterns work

Initial commands:

```text
DEST_SPI_LCD_DRAW_RGB565 <width> <height> <binary_payload>
DEST_SPI_LCD_SHOW_LAST_SOURCE_FRAME
DEST_SPI_LCD_CLEAR <rgb565_hex>
```

### Phase D4 - Lab Mirror Mode

Goal: low-risk mirror path from current source driver to SPI LCD.

Pipeline:

```text
GBC source capture
  -> current RGB565 frame buffer
  -> optional crop/scale/letterbox
  -> SPI LCD DMA transfer
```

This must be opt-in:

```text
DEST_SPI_LCD_MIRROR_START
DEST_SPI_LCD_MIRROR_STOP
DEST_SPI_LCD_STATUS
```

Mirror mode is not a product pass-through guarantee. It is a lab output tap.

## Proposed Firmware Module

Files:

```text
firmware/main/destination_spi_lcd.h
firmware/main/destination_spi_lcd.c
```

Responsibilities:

- Own destination SPI bus configuration.
- Own panel IO and generic MIPI-DCS command transport.
- Expose safe init/status/test-pattern/draw/shutdown functions.
- Never configure pins unless explicitly initialized.
- Keep all destination state separate from GBC source code.
- Support no-panel builds without breaking source capture.

Initial C API shape:

```c
typedef enum {
    DEST_SPI_LCD_STATE_UNCONFIGURED,
    DEST_SPI_LCD_STATE_SAFE_OFF,
    DEST_SPI_LCD_STATE_INITIALIZED,
    DEST_SPI_LCD_STATE_ERROR,
} dest_spi_lcd_state_t;

typedef struct {
    int gpio_sclk;
    int gpio_mosi;
    int gpio_cs;
    int gpio_dc;
    int gpio_reset;
    int h_res;
    int v_res;
    int pclk_hz;
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
    bool invert_color;
    bool bgr_order;
} dest_spi_lcd_config_t;
```

Proposed command contract:

| Command | Purpose |
|---|---|
| `DEST_SPI_LCD_STATUS` | Report state and configured pins without changing hardware. |
| `DEST_SPI_LCD_CONFIG ...` | Optional runtime config or profile-loaded config. |
| `DEST_SPI_LCD_INIT` | Initialize GPIO/SPI/panel, then leave display enabled. |
| `DEST_SPI_LCD_SAFE_OFF` | Disable destination and put output pins in safe state. |
| `DEST_SPI_LCD_TEST_PATTERN <name>` | Draw known test patterns. |
| `DEST_SPI_LCD_CLEAR <rgb565>` | Fill screen with one color. |
| `DEST_SPI_LCD_SHOW_LAST_SOURCE_FRAME` | Draw the last captured source frame once. |
| `DEST_SPI_LCD_MIRROR_START` | Start low-FPS lab mirror, if enabled. |
| `DEST_SPI_LCD_MIRROR_STOP` | Stop mirror mode. |

## Proposed Browser UI

Destination tab layout:

Left side:

- destination status
- pin map
- panel state
- last test screenshot/artifact
- transfer telemetry

Right side:

- panel profile selector
- GPIO assignments
- controller driver selection
- SPI clock
- init/safe-off controls
- test pattern controls
- mirror controls

## Integration Rules

- Destination code must not modify source capture pins.
- Destination code must not start automatically at boot.
- Source live browser stream must remain available with destination disabled.
- SPI LCD test patterns must work without the GBC connected.
- Any mirror mode must be stoppable independently of source capture.
- Failed destination init must not leave source capture broken.

## Reference Notes

Official ESP-IDF references to use during implementation:

- SPI LCD panel IO: `esp_lcd_new_panel_io_spi()`
- SPI bus DMA setup: `spi_bus_initialize(..., SPI_DMA_CH_AUTO)`
- RGB LCD output, for future parallel IPS panels: `esp_lcd_new_rgb_panel()`
