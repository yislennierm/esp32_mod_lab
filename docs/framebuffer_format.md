# Framebuffer Format

## 1. Objective

Document decoded pixel and framebuffer formats used by host tools and future firmware/display paths.

This matters because reproducible frame reconstruction requires explicit color layout, dimensions, ordering, and conversion rules.

## 2. Current Understanding

Current hypothesis: source data is likely RGB666 on the physical LCD bus, while the active GBC live-view path uses hardware-speed RGB565 packing.

Evidence: the current wiring includes `R0-R5`, `G0-G5`, and `B0-B5`. The repeat-period analysis of the fast LCD_CAM stream found a stable `161`-sample line model and `145`-line frame period. With RGB565 decoded from the upper bits, the boot-logo screen appears coherent in the browser live view.

Confidence level: medium-high for the RGB565 live decode model; medium for RGB666 bit packing because all 18 connected color lines can be preserved in an offline CPU-polled artifact, but low for full-frame true-color timing because that path is not fast enough to capture a complete 160x144 frame inside one source frame.

## 3. Unknowns

- Whether the extra byte in the `161`-byte transfer line is blanking, guard data, or an artifact of capture alignment.
- Exact RGB bit ordering for final full RGB666 after testing varied game content.
- Gamma, inversion, or panel-specific transformations.
- Whether foreground/background should be inverted for final visualization.
- Whether a peripheral-backed capture path can preserve full RGB666 without losing the current stable RGB565 live geometry.

## 4. Experiment Results

2026-05-08: Added named host decode preset `gbc_rg44_fast_v1` in `host/decode_lcdcam_fast.py`.

Preset parameters:

| Field | Value |
|---|---:|
| Stream width | `161` bytes |
| Frame period | `145` rows |
| Visible crop | `160x144` |
| Pixel packing | RAW8 `RG44`, bits `0..3 = R2..R5`, bits `4..7 = G2..G5` |
| Capture mode | LCD_CAM `DE=HIGH`, byte-count EOF, DCLK inverted edge |

Known reproducible decode:

- Input: `captures/decoded/lcdcam_raw/20260508T193122Z-lcdcam_raw_high_320x204.bin`
- Output directory: `captures/decoded/lcdcam_raw/decoded_fast_v1_193122/`
- Inverted comparison: `captures/decoded/lcdcam_raw/decoded_fast_v1_193122_inverted/`

2026-05-08: Added first blue-enabled diagnostic format `RGB332`.

Packing:

| Bits | Signal bits |
|---|---|
| `7..5` | `R5,R4,R3` |
| `4..2` | `G5,G4,G3` |
| `1..0` | `B5,B4` |

This sacrifices lower bits to fit a first full-color diagnostic image into the current 8-bit LCD_CAM raw path. Host rendering support is implemented in `render_dvp_raw.py` as `rgb332`, and future fast captures can be decoded with preset `gbc_rgb332_fast_v1`.

2026-05-08: Confirmed the `gbc_rgb332_fast_v1` preset also decodes the reduced `192x145` LCD_CAM capture window.

Current reduced capture model:

| Field | Value |
|---|---:|
| LCD_CAM programmed size | `192x145` bytes |
| Decoded stream width | `161` bytes |
| Decoded frame period | `145` rows |
| Visible crop | `160x144` |
| Pixel packing | RAW8 `RGB332`, upper color bits |

Artifact:

- `captures/decoded/lcdcam_raw/20260508T212557Z-lcdcam_raw_high_192x145_gbc_rgb332_fast_v1/20260508T212557Z-lcdcam_raw_high_192x145_gbc_rgb332_fast_v1_frame0_x0_y0.png`

The direct `161x145` programmed capture still returns all zeros in the current LCD_CAM mode, so `192x145` is a capture-window workaround rather than proof that LCD_CAM is natively honoring the exact GBC source geometry.

2026-05-09: Added an experimental CPU-polled RGB666 line-burst artifact format.

Packing:

| Byte | Signal bits | GPIO order |
|---|---|---|
| `0` | `R0..R5` | `18,17,16,15,14,13` |
| `1` | `G0..G5` | `12,11,10,9,8,7` |
| `2` | `B0..B5` | `36,45,46,47,48,50` |

Each byte stores one 6-bit channel in bits `0..5`. Host rendering expands each channel with `round(value * 255 / 63)`.

Artifacts:

- One-frame bounded test: `captures/decoded/rgb666_line_bursts/20260509T115056Z-rgb666_line_bursts_160x144.*`
- Complete diagnostic buffer across frames: `captures/decoded/rgb666_line_bursts/20260509T115426Z-rgb666_line_bursts_160x144.*`

Important limitation: the CPU-polled RGB666 path captured only `15` complete `160`-pixel lines before the next `SPS` frame boundary. The complete `160x144` RGB666 PNG was therefore captured with `stop_on_next_frame=false`, so it is a color-packing diagnostic, not proof of a coherent one-frame true-color capture.

2026-05-09: Added experimental hardware-captured `RGB664` format.

Packing:

| Bits | Signal bits |
|---|---|
| `0..5` | `R0..R5` |
| `6..11` | `G0..G5` |
| `12..15` | `B2..B5` |

This preserves all red bits, all green bits, and the upper four blue bits in one 16-bit LCD_CAM/GDMA sample. It intentionally drops `B0` and `B1` for this experiment because the ESP32-P4 camera input path exposes an 8/16-bit stride switch. This is a hardware-speed true-color candidate, not the final source-preserving RGB666 format.

Artifact:

- `captures/decoded/lcdcam_raw/20260509T153425Z-lcdcam_raw_high_192x145.*`

Result: the private LCD_CAM/GDMA path accepted 16-bit input width and returned `55680` bytes for `192x145x2`, with all descriptors complete.

2026-05-09: Added experimental hardware-captured standard `RGB565` format.

Packing:

| Bits | Signal bits |
|---|---|
| `0..4` | `B1..B5` |
| `5..10` | `G0..G5` |
| `11..15` | `R1..R5` |

This is a standard RGB565-style word using the upper five red bits, all six green bits, and the upper five blue bits. It drops only `R0` and `B0`, which is a better match for common display and upscaling pipelines than RGB664.

Artifact:

- `captures/decoded/lcdcam_raw/20260509T154430Z-lcdcam_raw_high_192x145.*`

Result: the private LCD_CAM/GDMA path returned `55680` bytes for `192x145x2`, with all descriptors complete.

## 5. Next Steps

- Keep RGB565 as the active GBC live-view mode while full RGB666 capture is developed separately.
- Use `host/capture_rgb666_line_bursts.py` for offline color-bit validation and metadata-preserving artifacts.
- Treat RGB332 entries in this document as historical experiment records, not the current GBC baseline.
- Compare RGB565 against varied cartridge content and keep RGB666 as an offline/reference goal.
- Promote the preset only after multiple captures reproduce the same visible frame geometry.
- Investigate why exact `161x145` byte-count LCD_CAM capture returns zeros while aligned larger widths return live samples.
- Investigate a peripheral-backed true-color strategy: wider LCD_CAM/GDMA packing, multiple synchronized passes, or a profile-defined raw sample stream.
