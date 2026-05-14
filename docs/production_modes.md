# Production Modes

Purpose: define firmware modes that use proven lab blocks as direct source-processing-destination implementations.

Status: supporting architecture contract.

Last updated: 2026-05-11.

## 1. Objective

Document the early production firmware paths that run without the browser workbench or USB command server in the hot path.

This matters because the lab instrument proves signals and parameters, while production firmware should measure what the ESP32-P4 can do when it is acting like an embedded bridge rather than a host-controlled instrument.

## 2. Current Understanding

Current hypothesis: every production mode should be a compile-time firmware profile assembled from proven source, processing, and destination modules.

Evidence:

- The GBC source profile can capture recognizable RGB565 frames.
- The SPI LCD destination can receive RGB666 and RGB565 pixel writes.
- The firmware-only source-to-destination benchmark is faster than the browser live view.
- The normal lab firmware can run the native visible `160x144` low-level source-ring benchmark at `60.532 fps` counters-only with zero drops/errors.

Confidence level: high for the first GBC source to SPI LCD production experiment; medium for final architecture because source capture is still not full frame rate.

## 3. Unknowns

- Whether the current GBC source capture path can be reduced below roughly `16.7 ms` per frame.
- Whether a different ESP32-P4 capture peripheral setup can reach the original GBC frame rate.
- Whether destination throughput should stay SPI, move to parallel RGB/I80, or use a different panel.
- Whether higher SPI clocks are possible with native IO_MUX pins or a different ESP-IDF SPI configuration.
- Whether the RGB565 panel write path improves real displayed FPS enough to matter with the current SPI wiring.

## 4. Experiment Results

### `GBC_P4_PRODUCTION_MIRROR`

Build:

```text
./scripts/build_production_mirror.sh
```

Flash:

```text
./scripts/flash_production_mirror.sh /dev/cu.wchusbserial5A470211841
```

Behavior:

- Boots directly into the GBC source to SPI LCD loop.
- Does not start the browser workbench protocol.
- Does not rotate, mirror, scale, crop-adjust, or otherwise fix source geometry.
- Sends source RGB565 to the SPI LCD as RGB565 where supported. Older builds converted to RGB666 and are retained only as a comparison baseline.
- Emits one JSON metrics line per second on the USB Serial/JTAG console.

Measured result on 2026-05-10:

```json
{"mode":"production_mirror","frames":15,"fps_x1000":14931,"avg_capture_us":41953,"avg_draw_us":24986,"max_capture_us":41985,"max_draw_us":24997,"capture_failures":0,"draw_failures":0}
```

After changing production mode to keep LCD_CAM/GDMA capture alive across a capture window and overlap source capture with SPI drawing, the current best measured result is:

```json
{"mode":"production_mirror_overlap","displayed":30,"captured":30,"fps_x1000":29863,"avg_capture_us":23333,"avg_draw_us":25076,"max_capture_us":23389,"max_draw_us":25120,"dropped_frames":0,"capture_failures":0,"draw_failures":0,"capture_error":"none","draw_error":"none"}
```

Raw write-only SPI experiment on the existing pins:

```json
{"mode":"production_mirror_overlap","displayed":15,"captured":15,"fps_x1000":14931,"avg_capture_us":26790,"avg_draw_us":32511,"max_capture_us":27033,"max_draw_us":32531,"dropped_frames":0,"capture_failures":0,"draw_failures":0,"capture_error":"none","draw_error":"none"}
```

Interpretation:

- Early production mode started at about `14.9 fps`.
- The current overlapped production mode is stable at about `29.86 fps`.
- The destination draw path is about `25 ms` per frame at the proven `20 MHz` SPI clock.
- The current source capture window is about `23.3 ms` per frame.
- A raw write-only SPI backend with `SPI_DEVICE_NO_DUMMY` did not improve the current wiring; it reduced measured output back to about `14.9 fps` and is disabled by default.
- Full GBC frame rate cannot be reached with the current `20 MHz` RGB666-over-SPI destination. A `160x144` RGB666 payload requires about `33.0 Mbit/s` before command and transaction overhead at `59.7 fps`. The current panel path sends `3` bytes per pixel, so the destination alone exceeds the available SPI payload rate.
- The known-good SPI LCD destination baseline is `CS=52`, `SDI/MOSI=31`, `SCK=28`, `RESET=29`, `D/C=53`, ESP LCD DMA wrapper, RGB666 writes, `20 MHz`, and GPIO-matrix routing.
- A native SPI2 IO_MUX rewire to `CS=7`, `MOSI=8`, `SCK=9` proved clean SPI electrical output with a 100 kHz scope test, but did not bring up the LCD. Restoring the old pins initially still failed because the code still forced `SPICOMMON_BUSFLAG_IOMUX_PINS`; removing that flag restored color output on the old wiring.
- Destination orientation is controlled with the SPI LCD controller `MADCTL` register, not by rewriting the captured source pixels. `0x08` is the source-straight diagnostic value, while `0xE8` is the panel-corrected value that made the GBC image visually upright on the current LCD module. Production defaults to `0xE8` because it costs no ESP32 per-pixel processing.
- Current test build draws the `160x144` GBC visible source at `2x` as `320x288`, centered on the `320x480` SPI LCD. This is destination-side nearest-neighbor scaling for visual testing; it is expected to reduce FPS compared with the 1x path because it sends four times as many destination pixels.
- The 2x scaler batches multiple expanded source rows into a DMA-capable RGB666 block before each SPI write. This reduces command/address-window transaction overhead compared with sending one scaled source row at a time, but it does not change the fundamental SPI bandwidth limit.
- First batched 2x test showed intermittent repeated-row artifacts. Working hypothesis: the ESP LCD SPI path can still be transmitting a DMA buffer after `esp_lcd_panel_io_tx_color()` returns, so rewriting the same buffer for the next chunk can corrupt in-flight transfers. The mitigation is two alternating DMA buffers plus `trans_queue_depth=1` for this destination path.

2026-05-11 update:

- Added a production RGB565 panel write path so the source `RGB565` frame is sent to the SPI LCD as `COLMOD=0x55` instead of expanding every pixel to RGB666.
- This reduces SPI pixel payload from `3` bytes/pixel to `2` bytes/pixel before command/address overhead.
- Rebuilt and flashed `GBC_P4_PRODUCTION_MIRROR` successfully through `/dev/cu.wchusbserial5A470211841`.
- User-visible result: LCD dimmed but did not show the GBC image. The production mirror was restored to the known-good RGB666 draw path and reflashed through `/dev/cu.wchusbserial5A470211841`.
- The RGB565 panel write helper remains in the destination module for isolated testing, but production must not depend on it until the panel behavior is explained.
- Switched production mirror from 2x scaled output to 1x RGB666 output and reflashed through `/dev/cu.wchusbserial5A470211841`. Purpose: reduce destination SPI payload by 4x and establish a clearer lower-bandwidth LCD baseline.
- UART monitor sampling from this environment failed with `Operation not permitted`, so the flashed build still needs visual/user confirmation and metrics capture from a permitted terminal.
- TinyUSB output remains blocked: the isolated TinyUSB firmware boots and reports CDC ACM ready on UART, but macOS only enumerates the WCH serial bridge and does not expose a responding TinyUSB CDC device for the current cabling.

2026-05-11 PPA-backed 2x production test:

- Added compile-time production mode `PRODUCTION_MIRROR_MODE=3`.
- Build command: `PRODUCTION_MIRROR_MODE=3 ./scripts/build_production_mirror.sh`.
- Flash command used successfully: `PRODUCTION_MIRROR_MODE=3 ./scripts/flash_production_mirror.sh /dev/cu.usbmodem14401`.
- Pipeline: GBC LCD source capture as `RGB565`, copy visible `160x144` area into a contiguous DMA-capable buffer, PPA SRM nearest-neighbor scale to `320x288 RGB565`, then send the scaled image through the existing known-good SPI LCD RGB666 draw path.
- Purpose: prove the PPA scaler can be integrated into production firmware without the browser workbench, while preserving the known-good SPI LCD wiring and panel write path.
- Status: build and flash succeeded. After the GBC was powered on, production metrics were captured from `/dev/cu.usbmodem14401`.
- Expected limitation: this mode removes most CPU scaling cost, but it still sends a `320x288` image over the current `20 MHz` SPI destination path. It is a hardware-block integration test, not a full-rate destination solution.

Representative metrics:

```json
{"mode":"production_mirror_ppa_2x_sync","frames":6,"fps_x1000":5972,"avg_capture_us":41789,"avg_crop_us":1564,"avg_ppa_us":8222,"avg_draw_us":115619,"capture_failures":0,"ppa_failures":0,"draw_failures":0}
```

Interpretation:

- Effective frame rate is about `5.97 fps`.
- PPA 2x scaling is about `8.2 ms`, which is comfortably below one GBC frame period.
- SPI LCD drawing is about `115.6 ms`, so this mode is destination-bandwidth limited.
- The capture step is about `41.8 ms` because this first PPA production mode uses the older synchronous frame capture path. The previously proven source-ring path remains the better production source candidate.

2026-05-11 PPA-backed 1x production test:

- Added compile-time production mode `PRODUCTION_MIRROR_MODE=4`.
- Build command used: `BUILD_DIR=build_production_mirror_m4 PRODUCTION_MIRROR_MODE=4 ./scripts/build_production_mirror.sh`.
- Flash command used successfully: `BUILD_DIR=build_production_mirror_m4 PRODUCTION_MIRROR_MODE=4 ./scripts/flash_production_mirror.sh /dev/cu.usbmodem14401`.
- Pipeline: GBC LCD source capture as `RGB565`, copy visible `160x144` area into a contiguous DMA-capable buffer, PPA SRM pass-through at `1.0x`, then send the `160x144` output through the existing known-good SPI LCD RGB666 draw path.
- Purpose: measure PPA 1x overhead for future cases where a frame may need hardware mirror/rotate/color processing without changing resolution.

Representative metrics:

```json
{"mode":"production_mirror_ppa_1x_sync","frames":15,"fps_x1000":14931,"avg_capture_us":34768,"avg_crop_us":1453,"avg_ppa_us":5248,"avg_draw_us":25395,"capture_failures":0,"ppa_failures":0,"draw_failures":0}
```

Interpretation:

- Effective frame rate is about `14.93 fps`.
- PPA 1x pass-through costs about `5.25 ms`.
- SPI LCD drawing at 1x is about `25.4 ms`, similar to the previous 1x RGB666 destination baseline.
- The PPA 1x pass is useful as a measured baseline for future hardware transforms, but it should be skipped when the production path needs a pure unmodified 1x image.
- User visual feedback: this mode showed glitchy pixels. Treat the numbers as timing evidence only until the artifact source is isolated.
- The production build/flash scripts now run `idf.py reconfigure` before build/flash so `PRODUCTION_MIRROR_MODE` changes cannot silently reuse the previous CMake cache.

2026-05-11 source-ring + PPA + LCD production test:

- Added compile-time production mode `PRODUCTION_MIRROR_MODE=5`.
- Pipeline: `lcdcam_raw_ring_capture_loop` native visible `160x144 RGB565` source capture, CPU copy only into a DMA-capable handoff buffer, PPA SRM scale to `320x288 RGB565`, then existing SPI LCD RGB666 draw at `1x` over the already-scaled PPA output.
- Explicitly not present: CPU scaling. The firmware reports `"cpu_scaling": false`.
- Note: the SPI LCD draw path still performs RGB565-to-RGB666 color expansion for this panel. That is color packing, not geometric scaling.

Representative metrics:

```json
{"mode":"production_mirror_ring_ppa_2x","displayed":8,"captured":61,"copied":8,"fps_x1000":7999,"avg_capture_us":16520,"avg_copy_us":3016,"avg_ppa_us":8648,"avg_draw_us":116166,"dropped_frames":53,"capture_failures":0,"ppa_failures":0,"draw_failures":0,"cpu_scaling":false,"source_path":"lcdcam_raw_ring_capture_loop"}
```

Interpretation:

- Source capture is now using the fast native visible source-ring path, with average capture around `16.5 ms`, matching the GBC frame-rate class.
- PPA scaling is around `8.3-9.0 ms`.
- SPI LCD draw of the `320x288` RGB666 payload is still around `116 ms`, so the final displayed FPS is about `8 fps`.
- Dropped frames are expected in this mode because the source side now produces frames much faster than the current SPI LCD destination can consume them.
- This is the correct architecture test for source-ring + PPA + LCD, but it proves again that the current SPI display destination is the bottleneck.

2026-05-11 source-ring + PPA 1x + LCD production test:

- Added compile-time production mode `PRODUCTION_MIRROR_MODE=6`.
- Pipeline: `lcdcam_raw_ring_capture_loop` native visible `160x144 RGB565` source capture, CPU copy-only handoff buffer, PPA SRM `1.0x`, then existing SPI LCD RGB666 draw.
- Explicitly not present: CPU scaling. The firmware reports `"cpu_scaling": false`.
- Purpose: verify a true 1x version of the fast source-ring + PPA + LCD path after the user observed that mode `5` was visibly 2x and slowly scrolling out of sync.

Representative metrics:

```json
{"mode":"production_mirror_ring_ppa_1x","displayed":33,"captured":62,"copied":33,"fps_x1000":32075,"avg_capture_us":16537,"avg_copy_us":3376,"avg_ppa_us":5716,"avg_draw_us":25392,"dropped_frames":29,"capture_failures":0,"ppa_failures":0,"draw_failures":0,"cpu_scaling":false,"source_path":"lcdcam_raw_ring_capture_loop"}
```

Interpretation:

- Source capture remains at GBC frame-rate class, around `16.5 ms` per native visible frame.
- PPA 1x pass-through takes about `5.7-6.2 ms`.
- SPI LCD 1x draw takes about `25.4 ms`.
- Display rate is about `31.5-32.1 fps`.
- Dropped frames are expected because source capture is still faster than the current SPI LCD destination path.
- If visual scrolling persists in this 1x mode, the next suspect is frame/line phase in the image-producing ring path, not 2x scaling.

2026-05-11 source-ring stream geometry + PPA 1x + LCD production test:

- Added compile-time production mode `PRODUCTION_MIRROR_MODE=7`.
- Pipeline: `lcdcam_raw_ring_capture_loop` captures the solved stream geometry as `161x145 RGB565`, the callback copies the visible `160x144` region into a DMA-capable handoff buffer, PPA SRM runs at `1.0x`, then the existing SPI LCD RGB666 draw path displays the result.
- Explicitly not present: CPU scaling. The firmware reports `"cpu_scaling": false`.
- Purpose: test whether the slow scrolling seen in the `160x144` ring modes came from capturing too few stream samples per period rather than from PPA or destination scaling.
- During first flash, `esp_cache_msync` reported repeated alignment errors because `161x145x2 = 46690` bytes is not cache-line aligned. The backing DMA allocation and cache-sync length were padded to the ESP-IDF cache alignment while the hardware capture byte count stayed at `46690`.

Representative corrected metrics:

```json
{"mode":"production_mirror_ring_stream_ppa_1x","displayed":33,"captured":61,"copied":33,"fps_x1000":32271,"avg_capture_us":16742,"avg_copy_us":3390,"avg_ppa_us":5544,"avg_draw_us":25370,"dropped_frames":28,"capture_failures":0,"ppa_failures":0,"draw_failures":0,"cpu_scaling":false,"source_path":"lcdcam_raw_ring_capture_loop","capture_width":161,"capture_height":145,"visible_width":160,"visible_height":144}
```

Interpretation:

- Source capture remains at GBC frame-rate class, around `16.74 ms` per `161x145 RGB565` stream frame.
- SPI LCD 1x draw remains the displayed-frame limiter at about `25.37 ms`.
- Display rate is about `32 fps`; source capture still produces about `60 fps`, so drops are expected.
- The cache-alignment fault is fixed. If visual scrolling persists in mode `7`, the next suspect is start-of-frame/line phase in the continuous ring path rather than cache coherency, PPA scaling, or destination scale.

## 5. Next Steps

- Keep lab firmware and production firmware as separate flashable modes.
- Add a production profile registry before adding more modes.
- Optimize source capture and destination bandwidth together; both are now above one native GBC frame period.
- Treat native SPI IO_MUX rewiring as a separate, isolated experiment. ESP32-P4 normal SPI2 IO_MUX pins are `CLK=9`, `MOSI=8`, and `CS=7`; the current known-good `SCK=28` and `MOSI=31` use the GPIO matrix for normal single-SPI and must remain the recovery baseline.
- Treat parallel RGB/I80/QSPI destination panels as the likely path for full-rate production display output.
- Preserve the current production mirror as a reproducible baseline before adding orientation fixes, scaling, or panel-specific polish.
