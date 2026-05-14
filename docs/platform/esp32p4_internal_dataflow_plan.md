# ESP32-P4 Internal Dataflow Plan

Purpose: define the research path for using ESP32-P4 internal peripherals and accelerators to reach real-time console picture processing.

Status: active performance research plan.

Last updated: 2026-05-11.

## Objective

Use ESP32-P4 hardware blocks as the main speed path:

```text
source bus
    -> LCD_CAM/GDMA or equivalent capture
    -> DMA-capable frame ring
    -> PPA / DMA2D / CPU-SIMD processing where useful
    -> high-bandwidth sink
```

This matters because the current visible GBC path proves the concept, but it still spends too much time in compatibility-oriented capture and low-bandwidth debug output. The next performance work must measure silicon blocks directly, not judge the chip through SPI LCD or browser FPS.

## Current Understanding

Known measured facts:

| Area | Current Evidence | Interpretation |
|---|---|---|
| Source visibility | GBC image is visible in browser and SPI LCD | Source timing/color are understood enough to continue |
| Internal synthetic processing | Native-size RGB565 synthetic pipeline can exceed 60 fps when frames are already in RAM | CPU/memory are not the first proven bottleneck |
| Real-source compatibility capture | Around 30 fps class before full-rate destination | Per-frame setup/rearm or current LCD_CAM transaction model is a bottleneck |
| Production overlap | About 29.86 fps with source and SPI draw overlapped | Overlap helps, but source and SPI are both above one frame budget |
| USB Serial/JTAG transport | Around 9 fps for RGB565-sized synthetic payloads | Current control/data path is not the final frame stream |
| SPI LCD output | 20 MHz one-bit RGB666 debug path | Useful visual sink, not a full-rate production sink |

Confidence: high that the next major speed gains must come from persistent capture, frame rings, and hardware-assisted processing/output.

## Internal Blocks To Benchmark

| Block | Role | Why It Matters | Benchmark Goal |
|---|---|---|---|
| LCD_CAM DVP | Parallel source ingress | Captures console-like digital buses | Continuous frame cadence near source FPS |
| GDMA / AXI GDMA | Peripheral-memory movement | Avoids CPU polling/copying | Sustained ring-buffer capture without drops |
| Frame ring | Decouples producer/consumer timing | Lets capture, processing, and output overlap | Measured occupancy, drops, latency |
| PPA SRM | Scale/rotate/mirror | Moves image transforms off CPU | 2x and 4x RGB565 scaling inside frame budget |
| DMA2D | Stride-aware block movement | Helps copy/crop/pack without CPU loops | Fast line/block copies with source/destination stride |
| CPU + SIMD | Fallback conversion | Useful for custom packing or color math | Determine if conversion is a real bottleneck |
| USB OTG device | High-rate computer sink | Needed for capture-card mode | Synthetic payload > native RGB565 requirement |
| I80/RGB/DSI sink | High-rate display output | Needed for production display bridge | Write/display full frames at target FPS |

## Benchmark Rules

- Benchmark one block at a time.
- Start hardware-block benchmarks in isolated ESP-IDF apps under `experiments/` before integrating them into the full lab firmware.
- Use synthetic data when testing processing/output.
- Do not stream full images to the browser when measuring source FPS.
- Report counters only over the control path for hot-path benchmarks.
- Keep the SPI LCD as a visual sanity sink, not the performance metric.
- Keep the known-good GBC/SPI debug path intact as a regression baseline.
- Every benchmark must report enough counters to explain failure, not only FPS.

Required counters:

| Counter | Purpose |
|---|---|
| frames attempted | Denominator for reliability |
| frames completed | Useful throughput |
| frame interval min/avg/max | Cadence and jitter |
| capture time min/avg/max | Source bottleneck |
| processing time min/avg/max | Middle-block bottleneck |
| sink time min/avg/max | Output/transport bottleneck |
| dropped frames | Ring or consumer pressure |
| partial frames | Capture or sync failure |
| sync loss count | Source timing failure |
| DMA errors | Descriptor/peripheral failure |
| ring high-water mark | Buffer sizing evidence |

## Target Budgets

Assumption: first target is GBC active `160x144` at about `59.7 fps`.

| Operation | Target Budget |
|---|---:|
| One complete frame period | `~16.75 ms` |
| Native RGB565 source payload | `46,080 bytes/frame`, `2.75 MB/s` |
| Native RGB666 source payload | `69,120 bytes/frame`, `4.13 MB/s` |
| 2x RGB565 framebuffer | `184,320 bytes/frame`, `11.0 MB/s` |
| 2x RGB666 framebuffer | `276,480 bytes/frame`, `16.5 MB/s` |

For a block to be useful in a real-time chain, it should leave margin for the other blocks. A block that consumes nearly the full `16.75 ms` budget by itself is not good enough unless the pipeline is deeply overlapped and the destination has its own cadence.

## Phase 1: Persistent Source Capture

Goal: prove whether ESP32-P4 can capture the source into memory at source frame rate.

Test shape:

```text
GBC source
    -> LCD_CAM/GDMA
    -> 2 or 3 DMA-capable frame slots
    -> counters only
```

No SPI LCD. No browser frame transfer. No PNG rendering.

Initial command:

```text
SOURCE_RING_BENCH <frame_count_1_to_512> [timeout_ms] [RGB565] [pclk_invert_0_or_1] [frame_sync_0_or_1]
```

Current implementation status:

- `SOURCE_RING_BENCH` is the generic lab-method command name.
- The current backend is the existing GBC LCD source profile using LCD_CAM/GDMA with double-buffer rearm.
- The command intentionally emits JSON counters only; it does not transfer frame pixels to the browser and does not draw to the SPI LCD.
- The command currently restricts the solved source format to `RGB565` so benchmark results are not mixed with older diagnostic RGB332/RG44 paths.
- `experiments/source_ring_bench/` is the isolated firmware version of this benchmark. It boots directly into the source-ingress test and excludes the lab protocol, browser stream, SPI LCD destination, TinyUSB, PNG rendering, and frame streaming.
- Treat the isolated app result as higher-confidence hardware-block evidence than the lab command result. The lab command is useful for convenient regression checks after the isolated path is understood.

Important result fields:

| Field | Meaning |
|---|---|
| `schema` | Stable result schema name for host/UI parsing. |
| `source_profile` | Active source profile; currently `gbc_lcd`. |
| `performance_path` | Actual backend tested now. |
| `next_performance_path` | Intended next backend if this result shows rearm cost. |
| `hot_path_excludes` | Confirms browser frames, SPI draw, and PNG rendering are outside the measured path. |
| `completed_fps` | Source ingress chunk/frame completion rate. |
| `target_rate_met` | True only when completed rate is within 98% of the current GBC source target and no chunks are missing. |
| `partial_frames` | Requested minus completed chunks. |
| `sync_loss_count` | One if the start trigger was not seen. |
| `dma_errors` | Failed rearm count. |
| `avg_capture_budget_pct` | Average capture time as a percentage of the `~16.75 ms` GBC frame budget. |

Questions:

- Can the source driver keep LCD_CAM/GDMA configured across frames?
- Does `esp_cam_ctlr_receive()` with callback handoff avoid the current rearm cost?
- If the official DVP driver cannot do it, what low-level LCD_CAM/GDMA state prevents it?
- Is the current `SPS/SPL/DCLK` mapping enough for continuous capture?
- Are duplicate rows/frames caused by capture timing, source stride, or destination draw?

Success condition:

- Stable source-rate capture into memory, or a clear measured reason why the current LCD_CAM/DVP configuration cannot do it.

## Phase 2: Frame Ring And Overlap

Goal: make source, processing, and sink independent tasks sharing a bounded frame ring.

Required behavior:

- producer never blocks on debug output
- consumer may drop old frames if it falls behind
- ring occupancy is observable
- latency from capture to sink is measurable
- every frame has sequence number and timestamp

Recommended initial policy:

- 3 frame slots
- newest-frame display policy for visual sinks
- lossless mode only for benchmarks where the sink is expected to keep up

## Phase 3: PPA Processing

Goal: decide whether PPA owns image transforms in production firmware.

Benchmarks:

| Benchmark | Input | Output | Notes |
|---|---|---|---|
| 2x scale | RGB565 `160x144` | RGB565 `320x288` | First real scaler test |
| 4x scale | RGB565 `160x144` | RGB565 `640x576` | IPS-class payload test |
| mirror X/Y | RGB565 native | RGB565 native | Compare PPA vs panel MADCTL vs CPU |
| rotate | RGB565 native | RGB565 rotated | Useful for panel orientation |
| non-blocking SRM | RGB565 native/2x | RGB565 out | Measure callback/ring behavior |

Notes:

- PPA SRM input and output buffers must be different.
- Scale precision is truncated to 1/16 steps.
- Test internal RAM and PSRAM buffers separately.
- Do not add shader/look effects until basic scaling is proven.

## Phase 4: DMA2D / Async Copy

Goal: measure whether DMA2D should own copies, crops, stride conversion, or line packing.

Benchmarks:

- contiguous RGB565 copy
- source-stride to packed output copy
- crop window extraction
- line-block copy into display buffer
- PSRAM-to-internal and internal-to-PSRAM variants

Use DMA2D only where it removes CPU cost or enables overlap. Do not add it to a simple path without measurement.

## Phase 5: Fast Sink Benchmarks

Goal: evaluate sinks without source capture in the hot path.

Sink order:

1. null sink, counters only
2. memory sink, frame ring only
3. USB OTG device synthetic payload
4. I80/RGB/DSI write-only if hardware is available
5. SPI debug sink for visual sanity only

Each sink benchmark should take synthetic frames and report payload bytes/sec, FPS, errors, and buffer ownership timing.

## Phase 6: Integrated Candidate

Only combine blocks that passed isolated benchmarks.

Candidate chains:

```text
GBC source -> frame ring -> null sink
GBC source -> frame ring -> USB HS device stream
GBC source -> frame ring -> PPA 2x -> high-bandwidth display
GBC source -> frame ring -> DMA2D crop/copy -> high-bandwidth display
```

The current SPI LCD can remain connected for debug, but it should not define whether the ESP32-P4 platform is fast enough.

## Unknowns

- Whether the official camera controller API can be driven as a true continuous LCD bus sampler for GBC semantics.
- Whether low-level LCD_CAM/GDMA descriptors are required to avoid frame rearm.
- Whether PPA supports the exact buffer/color combinations without conversion copies.
- Whether DMA2D can usefully handle our stride/crop patterns.
- Whether USB HS device is available on current hardware or must wait for custom PCB.
- Whether destination display output should be RGB LCD, I80, or DSI for first full-rate product proof.

## Experiment Results

Current relevant results:

- Browser and SPI LCD show recognizable GBC image.
- Current SPI LCD debug output is RGB666 and bandwidth-limited.
- RGB565 is the current source baseline.
- Current overlapped production path reached about `29.86 fps`.
- Internal synthetic pipeline exceeded native 60 fps.
- Native USB Serial/JTAG stream is too slow for full-rate frame data.
- 2026-05-11: added firmware command `SOURCE_RING_BENCH` as the generic counters-only source-ingress benchmark entry point. It currently wraps the proven GBC LCD source profile and LCD_CAM/GDMA double-buffer rearm path, preserving existing GBC-specific benchmark commands for compatibility.
- 2026-05-11: added isolated ESP-IDF app `experiments/source_ring_bench/` and scripts `scripts/build_source_ring_bench.sh` / `scripts/flash_source_ring_bench.sh`. The app builds successfully and is intended to test source ingress without lab-mode interference.
- 2026-05-11: flashed and ran `experiments/source_ring_bench/` through the WCH UART port. Representative result: `120/120` RGB565 source captures completed, `0` drops, `0` partial frames, `0` sync loss, `0` DMA errors, `completed_fps ~= 49.65`, `avg_capture_us ~= 19980`, `max_capture_us ~= 20766`, `avg_capture_budget_pct ~= 119.3`. This confirms the current source-ingress limit remains when lab/browser/SPI/TinyUSB are removed from the image.
- 2026-05-11: added and ran a low-level cyclic LCD_CAM/GDMA descriptor-ring benchmark in the isolated app. At `192x145 RGB565`, it remained around `50.09 fps`, matching the older oversized compatibility-buffer byte count. At native visible `160x144 RGB565`, it completed `120/120` frames with `0` drops/errors, `completed_fps ~= 60.53`, `avg_capture_us ~= 16520`, and `target_rate_met=true`. This proves native visible-size source ingress is source-rate capable in counters-only mode.

## Next Steps

1. Define firmware benchmark modes:
   - `source_ring_bench` / `SOURCE_RING_BENCH` - implemented as double-buffer rearm plus low-level cyclic descriptor-ring benchmark; isolated app is the primary proof path
   - `frame_ring_bench`
   - `ppa_srm_bench`
   - `dma2d_copy_bench`
   - `sink_null_bench`
   - `sink_usb_bench`
2. Promote the proven low-level native-size cyclic descriptor-ring path into a production source frame-ring module.
3. Add a common benchmark result JSON schema document after the first result is captured.
4. Validate image phase/line geometry on top of the native-size source ring, because the current benchmark proves payload cadence but not decoded picture alignment.
5. Keep browser live view and SPI LCD debug output as regression tools, not benchmark gates.
