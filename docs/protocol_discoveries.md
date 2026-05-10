# Protocol Discoveries

## 1. Objective

Record confirmed protocol behavior discovered from measurements and decoded captures.

This matters because implementation must be based on evidence, not guessed LCD semantics.

## 2. Current Understanding

Current hypothesis: the display bus carries RGB666 pixel data with separate timing/control signals. Current timing evidence supports `SPS` as frame sync, with stable `LP` and `SPL` counts per frame under the current test state.

Evidence: this is the current project hypothesis in `PROJECT_CHARTER.md`, and the 2026-05-07 timing relationship capture found stable SPS-to-SPS frame windows.

Confidence level: medium for frame timing, low for pixel/RGB semantics until measured traces and reconstructed frames exist.

## 3. Unknowns

- Pixel bit ordering.
- Active video window.
- Line/frame marker semantics.
- Clock edge and data validity.
- Polarity inversion behavior.
- Whether all RGB bits are directly observable and synchronous.

## 4. Experiment Results

2026-05-07: Timing relationship analysis of a 100 ms capture found five complete frames with:

- SPS rising-to-rising mean frame duration: 16742.8 us, about 59.73 Hz.
- LP count per complete frame: exactly 154.
- SPL count per complete frame: exactly 145.
- First LP after SPS rising: 3-6 us.
- First SPL after SPS rising: 451-453 us.

This is the first stable protocol-level timing discovery. `SPS` is now a strong frame sync candidate. `LP` and `SPL` are both line/scan-related, with `SPL` especially interesting because 145 is close to the expected 144 visible lines.

2026-05-08: First DVP capture success strengthens `SPS` as frame sync and `SPL` as the best current data-enable/visible-region candidate. The successful configuration was generic DVP RAW8 with `DCLK -> PCLK`, `SPS -> VSYNC` non-inverted, and `SPL -> DE` non-inverted. Configurations using inverted VSYNC timed out. This is evidence from peripheral behavior, not yet proof of exact LCD protocol semantics.

2026-05-08: Rendering polarity remains unresolved. The first normal red-only render appeared mostly red with some dark pixels, so an inverted lower-six-bit render was generated from the same raw DVP buffer. Visual comparison should determine whether the red bus appears active-low under the current screen state.

2026-05-08: With red data lines R0-R5 connected, timing-edge snapshots showed red bus variation only on `SPL` events in the tested state. Observed red values were `0x3f`, `0x1f`, `0x0f`, `0x07`, and `0x03`. This is evidence that SPL is close to meaningful red data timing, but it is not yet sufficient for frame reconstruction because samples are not taken at DCLK pixel cadence.

2026-05-08: Red values sampled by polling around DCLK windows showed 16 unique red6 values and 130 transitions in a 512-sample capture. This confirms that the connected red bus carries changing image-related digital data. Polling did not capture every pixel clock edge, so this is not yet a reconstructable pixel stream.

2026-05-08: Green lines `G0-G5` are now wired, but the generic DVP experiment is still RAW8. The current capture packs only upper red and green bits (`R2-R5` and `G2-G5`) into one byte for visual diagnosis. This is not proof of final pixel format; it is a practical test to see whether red/green image structure appears under the known `DCLK/SPS/SPL` timing hypothesis.

2026-05-08: Red/green RAW8 browser output showed a consistent bar pattern rather than random noise, but not a recognizable boot-logo frame. Four DVP variants (`SPL` vs `LP`, normal vs inverted PCLK) produced identical checksum and transition count in one test set, so the visible error is not yet resolved by simply toggling data-enable source or DCLK edge in the current generic DVP configuration.

2026-05-08: Offline stride/offset scans of a saved RAW8 red/green capture did not recover a recognizable image. This weakens the hypothesis that the current error is only a host-side row-stride mistake. Driver behavior remains a strong suspect because the generic DVP path has limited public polarity control and initializes VSYNC inverted by default.

2026-05-08: A no-cartridge post-boot capture, where a dark Nintendo-logo region is expected in the lower middle, still rendered as repeated vertical bars with no recognizable logo region. This is evidence against the current generic DVP `SPS`/`SPL` RAW8 capture being correctly aligned to visible pixel data.

2026-05-08: Reviewed open FPGA Game Boy references for timing context:

- MiSTer `Gameboy_MiSTer` `rtl/lcd.v`: comments that the Game Boy LCD shift register is filled at `4194304` pixels/sec, uses `BLANK_DELAY = 456*154`, and regenerates blank timing with horizontal count `0..455` and vertical count `0..153`.
- MiSTer `Gameboy_MiSTer` `rtl/video.v`: exposes an internal `lcd_clkena` pixel-valid signal and emits `lcd_data` only when its pixel counter has passed the initial hidden pixels. The visible output is still `160x144`.
- VerilogBoy `rtl/ppu.v`: defines internal timing constants `PPU_H_TOTAL=456`, `PPU_H_PIXEL=160`, `PPU_V_ACTIVE=144`, and `PPU_V_TOTAL=154`; it also notes that real pixel data is put on the bus on the clock falling edge and latched on the rising edge, with a valid signal rather than assuming all clock cycles are visible.

Inference from these references plus our measurements: the internal PPU cadence is the standard `456*154*~59.7Hz ~= 4.19MHz`, but our measured external `DCLK` is about `1.395MHz`, which is approximately `152` clocks per `LP` line at `~9198` lines/sec. That strongly suggests the external GBC LCD flex signal we call `DCLK` may be a gated visible-transfer clock rather than the full internal dot clock. Do not treat the physical LCD bus as a normal full-line camera DVP stream until proven.

2026-05-08: Direct DCLK-per-line measurement supports the gated-transfer hypothesis. The ISR-backed `CAPTURE_LINE_CLOCKS` command counted DCLK rising edges between marker interrupts:

- `SPL` falling: 180 samples; after one startup partial sample, all intervals were `160`, `161`, or `162` DCLK rising edges.
- `LP` falling: 180 samples; 168 intervals were `161` DCLK rising edges, with 12 zero-delta observations likely caused by duplicate/narrow LP interrupt behavior.

Discovery: `SPL` falling is currently the best visible-line/burst marker candidate. A working frame reconstruction path should probably capture line bursts of about `160` DCLK samples after `SPL`, synchronized to `SPS`, instead of asking the ESP32 generic camera driver to interpret the bus as a standard continuous DVP stream.

2026-05-08: The first complete custom line-burst capture succeeded. `CAPTURE_RG_LINE_BURSTS 160 144 2000` produced 144 lines with exactly 160 samples per line using `SPS` rising for frame sync, `SPL` falling for line start, and `DCLK` rising for samples. The data payload is upper red/green only (`R2-R5/G2-G5`), so it is not final RGB. The rendered PNG is still distorted, but the capture geometry is now correct enough to support offline hypotheses about sampling edge, pixel phase, bit order, line offset, and active-window selection.

2026-05-08: A fresh marker-aware capture, `captures/decoded/rg_line_bursts/20260508T191242Z-rg_line_bursts_160x144.*`, reproduced the same complete geometry: 144 `SPL`-keyed lines and 160 `DCLK`-rising samples per line. In contrast, private LCD_CAM/GDMA capture `captures/decoded/lcdcam_raw/20260508T190943Z-lcdcam_raw_spl_160x144.*`, even when started after `SPS` rising and the next `SPL` falling, completed after only a short descriptor fragment (`received_size=143`, `1/6` descriptors). Current confidence is high that `SPS`/`SPL`/`DCLK` form a usable frame/line/sample relationship for offline capture, and low that direct camera-style VSYNC/DE frame capture is semantically correct without additional line-window control.

2026-05-08: Offline post-processing and a falling-edge capture suggest the current problem is not only DCLK edge phase. The complete line-burst image contains repeated recognizable fragments of the expected no-cartridge screen. Simple 5x decimation and phase extraction did not collapse those fragments into a clean frame, and falling-edge capture retained the repeated structure. Revised protocol hypothesis: the repeated fragments are timing evidence first. They may be caused by using the wrong line marker, using the right marker with the wrong post-`SPS` offset, or sampling a repeated LCD-driver transfer window. Interleaved subfields remain possible, but they should not be assumed until `LP`, `SPL`, and marker-skip captures are compared systematically.

2026-05-08: Best current visual decode is the `5x5` tile-sheet cell row `3`, column `0` from the rising-edge line-burst capture. It appears as a coherent lower-resolution view rather than a repeated pattern. This weakens the simple phase-plane reconstruction hypothesis: the tiled raw layout may represent separate low-resolution subfields or driver phases, not 25 pieces that can be trivially interleaved into one full-resolution frame.

2026-05-08: Added marker-selection and marker-skip support to the red/green line-burst capture. `LP` captures with `skip_markers=0`, `5`, and `10` all completed at `160x144`, but the repeated fragments moved rather than resolving into a clean frame:

- `20260508T173537Z`: `LP`, `skip_markers=0`, checksum `5346840`, transitions `1388`
- `20260508T173452Z`: `LP`, `skip_markers=5`, checksum `5386365`, transitions `1348`
- `20260508T173752Z`: `LP`, `skip_markers=10`, checksum `5369280`, transitions `1402`

Discovery: changing line marker/offset affects the image coherently. The capture path is seeing real image data, but the logical mapping from LCD bus timing to `160x144` framebuffer rows is not solved. Adding blue data is lower priority than solving marker, offset, and per-line sample-window timing.

2026-05-08: Added DCLK-delay and marker-phase controls to test the repeated low-resolution-frame hypothesis. `LP`, `skip_markers=10` captures with `dclk_delay_edges=0`, `8`, and `32` all completed. The delay variants changed the image content and transition counts, proving the sampling window can be moved inside a marker interval. `marker_stride=5` captures for phases `0..4` also completed, but none produced a clean full-resolution frame. This weakens a simple "one of five marker phases is the real screen" explanation.

Current discovery: the roughly 25 repeated views are more consistent with an unresolved transfer-window/chunking model than with missing blue data. A plausible next hypothesis is that each line marker exposes a smaller useful pixel chunk, or multiple chunks, and the current capture incorrectly stores 160 samples after every selected marker.

2026-05-08: Single-frame stride testing clarified the previous stride artifacts. `marker_stride=5` with full `height=144` was spanning multiple physical frames to fill the output. After adding `--single-frame`, the same style of capture stopped after the next `SPS` and produced only `35` captured rows. Discovery: stride-phase images are not valid full-frame candidates unless the output height reflects the number of lines actually captured within one frame. The normal `marker_stride=1` single-frame capture still shows repeated structure, so the remaining issue is not explained solely by multi-frame stacking.

## 5. Next Steps

- Correlate `LP` and `SPL` against RGB data.
- Explain the extra `SPL` event before treating it as exactly visible-line count.
- Repeat timing capture across boot logo, idle, and gameplay states.
- Capture DCLK-windowed red samples across multiple lines.
- Select and prototype a hardware capture path for DCLK-synchronous data.
- Prototype a custom `SPS` + `SPL falling` + `DCLK` line-burst capture for red/green data.
- Add offline post-processing for line-burst captures: x/y shift, per-line phase shift, bit-order reversal, channel swap, sample-edge comparison, and optional line dropping/reordering.
- Test marker hypotheses in firmware: `SPL` versus `LP`, rising/falling sample edge, marker skips around `0..10`, and capture-only-marker-lines where `line_index % N == phase` for N values around `2..6`.
- Add a programmable DCLK delay after each line marker before storing pixels. This tests whether the good low-resolution tile is a stable transfer window within each marker interval.
- Sweep capture widths and DCLK delays together. Start with `32`, `40`, `80`, and `160` samples per marker to test whether the 5x5-looking artifact is caused by forcing shorter chunks into 160-pixel rows.
- Move toward a hardware-assisted capture path to sample more information faster. The CPU-polled line assembler can validate marker/window hypotheses, but it should not be treated as the final frame reconstruction method.

2026-05-08: Hardware-assisted capture update:

- Generic DVP DMA can complete with `SPS` as VSYNC and `SPL` as DE at both `160x144` and `160x154`.
- `LP` as generic DVP DE times out.
- DVP PCLK inversion does not fix the stripe-dominated render; one `160x144` inverted-PCLK capture was byte-equivalent by checksum/transition count to non-inverted PCLK.
- ISP-DVP was added to test separate `HSYNC` and `DE` semantics. Initial RAW8 bypass attempts failed with `esp_err=259`; later failure-stage instrumentation showed that bypass mode failed at `esp_isp_enable`, then non-bypass setup needed callback-provided transactions before `esp_cam_ctlr_start`.

Discovery: the faster path exists, but the generic DVP driver is still imposing camera-style frame semantics that do not match the GBC LCD bus.

2026-05-08: ISP-DVP now completes full DMA transfers after switching to callback-provided buffers, but the payload is all zero:

- `RAW8`, `HSYNC=LP`, `DE=SPL`: full `23040` byte buffer, checksum `0`.
- `RAW8`, `HSYNC=NC`, `DE=SPL`: full `23040` byte buffer, checksum `0`.
- `RAW8` DE-inverted and PCLK-inverted variants: full buffers, checksum `0`.
- `RGB565`, `HSYNC=LP`, `DE=SPL`: full `46080` byte buffer, checksum `0`.

Discovery: ISP-DVP is not currently a useful pixel source even though it starts cleanly and reports complete buffers. Since RGB565 output also produces zeros, the failure is not simply a RAW8 output-format problem. The next capture architecture should bypass more of the high-level camera/ISP framing and DMA raw PCLK samples/windows directly if possible.

2026-05-08: `DVP_CAPTURE_RAW_LEN` tested whether the LCD_CAM camera block can be made to complete DMA by byte count instead of VSYNC EOF while still using the generic DVP setup. All tested variants timed out, including `SPL` gate with non-inverted VSYNC, `SPL` gate with inverted VSYNC, and byte count programmed as both `N` and `N-1`.

Discovery: byte-count EOF is not reachable through the current high-level DVP start/reset sequence in a useful way. This does not disprove LCD_CAM/GDMA as the right hardware path, but it means the next experiment needs direct ownership of the low-level configuration and DMA descriptors so timeout cases can report partial descriptor lengths instead of only `ESP_ERR_TIMEOUT`.

2026-05-08: The first private LCD_CAM/GDMA sampler can capture and report descriptor progress outside the high-level DVP wrapper. Important observations:

- VSYNC EOF private capture at `160x144` produced non-static data: checksum `5603919`, `2860` byte transitions.
- Byte-count EOF private capture at `160x144` timed out but reported exactly `20460` received bytes, matching five full `4092` byte descriptors out of six.
- Smaller byte-count captures completed cleanly but were static: `160x127` all `0x00`, `160x128` all `0xff`.

Discovery: the ESP32-P4 LCD_CAM/GDMA hardware path is viable enough to move data and expose low-level progress, but the current raw sampler is not yet aligned to a useful GBC active data window. The static all-zero/all-one byte-count captures suggest the sampler can complete on a fixed byte count while looking at an inactive or saturated bus state. The non-static VSYNC-ended capture proves variable data can reach the buffer, so the next protocol question is start/gate ordering and line/window timing, not whether GDMA can move bytes.

2026-05-08: The `161`-byte row model now has a porch-like interpretation.

Evidence from `20260508T212557Z-lcdcam_raw_high_192x145.bin`:

- Decoding rows as `161` bytes with a `160`-pixel visible crop at `x=0` produces a clean boot-screen image.
- Byte `160` of each row is constant background (`0xff`) across all `145` rows.
- Shifting the crop to `x=1` remains readable, but raw-column analysis shows the extra byte is at the trailing edge for the current alignment.

Current hypothesis: the decoded source period is `160` visible pixel transfers plus one trailing blank/dummy transfer. This is analogous to a horizontal porch, but it is not yet proven to be a formal LCD timing porch rather than a capture-alignment byte.

The `145`-row period versus `144` visible rows may indicate one vertical blank/source-driver row. Its position is not yet proven because the current boot-screen capture has background at both the top and bottom.
- Determine why the best coherent view occupies raw region `x=0..31`, `y=84..111`; correlate that region with `LP`, `CLS`, and `PS` timing if possible.

2026-05-09: The cyan sparkle around `GAME BOY` text was traced to the DCLK sample edge.

Observed symptom: in the browser, pixels around the `GAME BOY` text sometimes appeared as approximately `RGB(115, 255, 254)`.

Packed-value analysis:

- The visible sparkle corresponds to raw `RGB332` value `0x7f`.
- `0x7f` decodes to roughly `(109, 255, 255)` in the current viewer.
- Most unstable pixels flipped between `0x6f` and `0x7f`.
- That is a one-bit difference: bit `4`, mapped to `G5` in the current `RGB332` diagnostic packing.

Frame-to-frame comparison:

- With `pclk_invert=true`, 12 repeated captures had `111` changing positions in the GAME BOY text box; `85` of those were G5-only flips.
- With `pclk_invert=false`, the same test had `0` changing positions and no `0x7f` sparkle pixels.
- Batched stream verification with `LCDCAM_RAW_STREAM_BIN 8 0` also had `0` changed positions and `0` `0x7f` pixels across 8 frames.

Conclusion: the sparkle was not a browser rendering artifact and not a stable boot-logo color. It was sampling-edge instability, primarily on `G5`, caused by sampling too close to a data transition. The current best DCLK sampling edge is non-inverted LCD_CAM PCLK routing (`pclk_invert=false`).
