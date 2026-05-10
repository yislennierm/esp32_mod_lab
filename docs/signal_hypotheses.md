# Signal Hypotheses

## 1. Objective

Track hypotheses about LCD bus signal meanings, timing roles, sample edges, and synchronization behavior.

This matters because the project must distinguish measured behavior from assumptions.

## 2. Current Understanding

Current hypothesis:

| Signal | Candidate Meaning |
|---|---|
| DCLK | pixel clock |
| LP | line pulse |
| SPL | horizontal start |
| SPS | frame start |
| MOD | polarity inversion |

Evidence: these candidates are defined in `PROJECT_CHARTER.md`.

Confidence level: low until timing captures confirm frequency, phase, polarity, and relationship to RGB data.

## 3. Unknowns

- Valid RGB sample edge.
- Whether LP, SPL, or another line acts as line start.
- Whether SPS is a frame marker.
- Blanking behavior before, between, and after visible pixels.
- Whether any signals are inverted relative to ESP32-P4 capture polarity.

## 4. Experiment Results

No timing captures have been recorded yet.

2026-05-06: A timing signal was temporarily routed to GPIO33 for input-only GPIO level testing. Later pinout correction identifies this as `SPS -> GPIO33`, not `SPL`.

2026-05-06: Static GPIO read of temporary timing signal on GPIO33 returned level `1`. This is weak evidence only; timing capture is required to identify behavior.

2026-05-06: Added edge-count command for GPIO33 so temporary timing signal activity can be measured as rising/falling edge counts over a fixed duration.

2026-05-06: Initial edge count on temporary timing signal GPIO33 produced 4029 rising edges and 2622 falling edges in 1 second. This does not yet confirm semantics because the GBC power state was not explicitly recorded and edge counts were asymmetric.

2026-05-06: With GBC ON, temporary timing signal GPIO33 produced about 8658 falling edges per second and static level `0`. Later pinout correction identifies this signal as `SPS -> GPIO33`.

2026-05-07: User added temporary wiring: `DCLK -> GPIO22`, `LP -> GPIO21`, and `PS -> GPIO20`. These signals are ready for input-only level and edge-count tests.

2026-05-07: Initial tests showed `DCLK/GPIO22`, `LP/GPIO21`, and `PS/GPIO20` all static low with zero edges over 1000 ms. This does not support active DCLK/LP on the currently wired pins under the current test conditions.

2026-05-07: User provided corrected display bus mapping: `SPL -> GPIO19`, `CLS -> GPIO32`, and `SPS -> GPIO33`. Prior GPIO33 observations now apply to SPS, not SPL.

2026-05-09: User moved `CLS` from ESP32-P4 GPIO32 to GPIO3 because GPIO32 appeared to be associated with the backfeed/power issue. Current firmware and host tooling now treat `CLS -> GPIO3`; older GPIO32 timing measurements are historical only.

2026-05-07: Input-only edge-count baseline with GBC ON:

| Signal | GPIO | Observation | Hypothesis Impact |
|---|---:|---|---|
| SPS | 33 | 60 rising and 60 falling edges per second | Strongly supports frame marker candidate |
| LP | 21 | 9196 falling edges per second, no rising edges classified | Supports line-rate marker candidate, but pulse polarity/width remains unknown |
| CLS | 32 | about 9196 rising and 9195 falling edges per second | Supports line-related or scan-driver clock candidate |
| PS | 20 | about 9195 rising and 9196 falling edges per second | Unexpected 9.2 kHz activity for a simple power-save signal; keep semantics open |
| SPL | 19 | 8656 falling edges per second, no rising edges classified | Active timing signal, but not yet clearly horizontal start |
| DCLK | 22 | activity detected but only about 104k total interrupt edges per second | Confirms toggling/activity only; interrupt method is invalid for measuring MHz pixel clock |

2026-05-07: PCNT-backed rising-edge measurements replaced the GPIO interrupt result for frequency estimation. GPIO22 is stable around 1.39 MHz over 100 ms, 500 ms, and 1000 ms windows. This challenges the `DCLK = 6-8 MHz` expectation and keeps the DCLK hypothesis open. GPIO33 at 60 Hz remains the strongest frame-marker candidate. GPIO19, GPIO20, GPIO21, and GPIO32 remain active around 8.66-9.20 kHz.

2026-05-07: `CAPTURE_TIMING_EDGES 100` produced six SPS pulses over 100 ms. SPS rising intervals averaged 16742.8 us, supporting a 59.73 Hz frame marker hypothesis. LP produced 916 low-level edge observations over 100 ms, implying about 153.3 LP observations per frame interval. This supports LP as a line-related marker but does not yet prove exact visible/blanking semantics. CLS and PS each produced 1833 edge observations over 100 ms and appear tightly coupled to the line cadence. SPL showed 862 events and occasional longer gaps, suggesting it may not be a simple one-pulse-per-line marker or may be affected by narrow-pulse ISR limitations.

2026-05-07: Frame relationship analysis found five complete SPS-to-SPS frames with exactly 154 LP events and 145 SPL events per frame. First LP appears 3-6 us after SPS rising. First SPL appears 451-453 us after SPS rising. `LP -> SPL` offset is usually about 14 us. This strengthens the hypothesis that SPS is frame sync, LP is line/boundary timing, and SPL may mark the visible-line scan region or a source-driver start event. The 145 SPL count is close to the expected 144 visible lines, but the extra event must be explained before treating SPL as a direct visible-line counter.

2026-05-08: Red data line activity check after connecting R0-R5 showed R5-R1 active and R0 static high during a 1000 ms edge-count test. This supports that at least the upper red bits are digital data lines observable by the ESP32-P4. R0 remains unresolved until tested under different screen content or with a synchronized sample capture.

2026-05-08: Synchronized red snapshots on timing-edge capture showed red values changing only at `SPL` events in the tested screen state. Observed values were `0x3f`, `0x1f`, `0x0f`, `0x07`, and `0x03`; R0 and R1 remained high in all timing-edge samples. This strengthens the hypothesis that `SPL` is near visible-line/source-driver timing, but pixel-rate DCLK sampling is still required before reconstructing red image data.

2026-05-08: Exploratory red sampling after `SPL` trigger and on polled `DCLK` rising edges captured 512 samples with 16 unique red values and 130 transitions. This confirms red bus values change in DCLK-adjacent windows. However, sample gaps ranged from 4 us to 82 us, much slower than the measured DCLK period, so firmware polling is missing most pixel-clock edges.

2026-05-08: Green edge-count validation after wiring `G0-G5` to GPIO12-GPIO7 showed active edges on GPIO7-GPIO11 and no edges on GPIO12 in the tested screen state. The upper green bits used by the RAW8 diagnostic (`G2-G5`) are active, so the current green image pattern is not solely caused by floating inputs. G0 static behavior remains content-dependent or unresolved.

2026-05-08: FPGA core review introduced a refined clock hypothesis. Open Game Boy cores model internal PPU timing as `456` dots per line and `154` lines per frame, matching about `4.19MHz`. Our physical `DCLK/GPIO22` measurement was about `1.395MHz`; combined with `LP ~= 9198Hz`, this is about `152` DCLK pulses per LP period. Hypothesis: the GBC LCD connector may expose a gated LCD shift clock for visible-ish pixel transfer, not the full internal PPU dot clock. This would make generic camera-DVP assumptions fragile because there may be no continuous pixel clock through blanking.

2026-05-08: Blue data line activity check after connecting `B0-B5` showed `B5-B1` active and `B0` static during the tested boot-screen state. This supports the upper blue data-line mapping enough for `RGB332` diagnostics using `B5` and `B4`. `B0` remains unresolved and should be retested with different screen content or checked electrically/wiring-wise before treating all six blue bits as validated.

## 5. Next Steps

- Measure DCLK, LP, SPL, SPS, CLS, and MOD frequencies.
- Use all current temporary mappings only for input-level and edge-count tests until timing capture exists.
- Compare edge counts across DCLK/GPIO22, LP/GPIO21, PS/GPIO20, SPL/GPIO19, CLS/GPIO32, and SPS/GPIO33.
- Capture relative timing between DCLK and candidate sync signals.
- Verify whether GPIO22 is true DCLK with independent probing.
- Use saved timing-edge captures to estimate ordering around SPS frame boundaries.
- Treat 154 LP/frame and 145 SPL/frame as measured invariants for the current test state until contradicted by later captures.
- Add synchronized red-bus sampling to correlate R0-R5 values with SPS/LP/SPL timing.
- Add DCLK-windowed red sampling to capture multiple red samples per line, not only red state at timing-control edges.
- Replace polling with a peripheral-backed capture method before attempting red image reconstruction.
- Test whether `DCLK` pulses occur continuously through the full LP period or only during a visible transfer window. A hardware logic analyzer or a firmware capture that counts DCLK pulses per LP interval is needed before configuring camera-style sync assumptions.
- Build host-side hypothesis definitions before decoding RGB frames.
- Retest `B0 -> GPIO36` under different visible content, because the first input-only edge count observed no transitions on that line.
