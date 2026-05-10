# Timing Notes

## 1. Objective

Record measured timing characteristics of the Game Boy Color LCD bus.

This matters because timing evidence drives capture configuration, decoder hypotheses, and eventual real-time bridge feasibility.

## 2. Current Understanding

Current hypothesis: DCLK is the pixel clock and the bus may resemble a DVP-style parallel video interface.

Evidence: project references identify ESP32-P4 LCD_CAM/DVP capture APIs as potentially relevant.

Confidence level: low until measured.

## 3. Unknowns

- DCLK frequency.
- Frame rate.
- Line rate.
- Sync pulse widths.
- RGB setup and hold timing relative to DCLK.
- Whether timing is stable across boot, gameplay, and LCD state changes.

## 4. Experiment Results

No timing measurements have been recorded yet.

2026-05-06: Temporary timing signal on GPIO33 edge-count measurements with GBC ON. Later pinout correction identifies this as `SPS -> GPIO33`:

| Duration | Rising Edges | Falling Edges | Falling Hz | Static Level After |
|---:|---:|---:|---:|---:|
| 1000 ms | 0 | 8656 | 8656.0 | not measured in same command |
| 5000 ms | 0 | 43297 | 8659.4 | 0 |

These are edge-count observations only. The ISR-based method classifies edge polarity by reading current level inside the interrupt, so narrow pulses may be misclassified. A timestamped capture is required before deriving pulse width or polarity.

2026-05-07: Temporary wiring tests for additional signals before the full pinout correction:

| Signal | GPIO | Static Level | Duration | Rising Edges | Falling Edges | Notes |
|---|---:|---:|---:|---:|---:|---|
| PS | 20 | 0 | 1000 ms | 0 | 0 | Static low during test |
| LP | 21 | 0 | 1000 ms | 0 | 0 | Static low during test |
| DCLK | 22 | 0 | 1000 ms | 0 | 0 | Static low during test |

This conflicts with the expectation that DCLK should be active when the LCD bus is running. Recheck connector pin numbering, solder continuity, GBC power/display state, and whether these names match the specific LCD revision. Later pinout correction added SPL/GPIO19, CLS/GPIO32, and SPS/GPIO33 for additional timing checks.

2026-05-07: Rebuilt and flashed firmware with input-only allowlist for all currently connected timing/control lines. With GBC ON, 1000 ms GPIO interrupt edge counts returned:

| Signal | GPIO | Static Level | Duration | Rising Edges | Falling Edges | Notes |
|---|---:|---:|---:|---:|---:|---|
| SPL | 19 | 0 | 1000 ms | 0 | 8656 | Activity near 8.656 kHz; polarity classification may be distorted by narrow pulses |
| PS | 20 | 1 | 1000 ms | 9195 | 9196 | Activity near 9.196 kHz |
| LP | 21 | 0 | 1000 ms | 0 | 9196 | Activity near 9.196 kHz; polarity classification may be distorted by narrow pulses |
| DCLK | 22 | 1 | 1000 ms | 46003 | 57970 | Activity detected, but ISR edge counting cannot measure a 6-8 MHz pixel clock reliably |
| CLS | 32 | 1 | 1000 ms | 9196 | 9195 | Activity near 9.196 kHz |
| SPS | 33 | 1 | 1000 ms | 60 | 60 | Strong 60 Hz frame-marker candidate |

The DCLK count must not be interpreted as the DCLK frequency. The current firmware uses GPIO interrupts, which are useful for slow line/frame/control activity but are expected to miss most edges on a MHz pixel clock.

2026-05-09: `CLS` wiring changed from GPIO32 to GPIO3 after GPIO32 was suspected of contributing to target backfeed while the GBC was off. Existing GPIO32 CLS measurements remain historical; repeat CLS timing measurements on GPIO3 before drawing new timing conclusions.

2026-05-07: Added PCNT-backed rising-edge measurement command `MEASURE_DCLK <gpio> <duration_ms>`. ESP-IDF local headers confirm ESP32-P4 supports PCNT with four units. Test firmware uses the modern `driver/pulse_cnt.h` API with overflow accumulation and leaves the measured GPIO input-only/floating.

PCNT measurements with GBC ON:

| Signal | GPIO | Duration | Rising Edges | Rising Edge Hz |
|---|---:|---:|---:|---:|
| DCLK | 22 | 100 ms | 138299 | 1382990 |
| DCLK | 22 | 500 ms | 698096 | 1396192 |
| DCLK | 22 | 1000 ms | 1393728 | 1393728 |
| SPL | 19 | 1000 ms | 8659 | 8659 |
| PS | 20 | 1000 ms | 9197 | 9197 |
| LP | 21 | 1000 ms | 9197 | 9197 |
| DCLK | 22 | 1000 ms | 1395226 | 1395226 |
| CLS | 32 | 1000 ms | 9197 | 9197 |
| SPS | 33 | 1000 ms | 60 | 60 |

The GPIO22 signal is consistently around 1.39 MHz by PCNT. This is much lower than the earlier expected 6-8 MHz note, so either the expectation is wrong for this bus/revision/state, GPIO22 is not actually DCLK, the board/header mapping is not one-to-one, or the signal path is attenuated/conditioned before reaching the ESP32-P4. Treat `DCLK = 1.39 MHz` as measured behavior, not final protocol truth, until confirmed with an oscilloscope or alternate capture method.

2026-05-08 FPGA reference comparison: MiSTer and VerilogBoy model the internal Game Boy PPU line as `456` dots and `154` lines, which yields about `4.19MHz` at `~59.7Hz`. Our measured `DCLK ~= 1.395MHz` divided by measured `LP ~= 9198Hz` is about `152` DCLK pulses per LP period. This is close to the visible width scale (`160`) and far from the full internal line length (`456`). Current working hypothesis: the physical GBC LCD flex `DCLK` is a gated LCD transfer clock, not the raw PPU dot clock.

2026-05-08: Added ISR-backed `CAPTURE_LINE_CLOCKS <LP|SPL> <falling|rising> <line_count> <timeout_ms>`. This uses PCNT to count `DCLK` rising edges continuously and snapshots the cumulative count inside an LP/SPL GPIO ISR. ESP-IDF documents `pcnt_unit_get_count()` as callable from ISR context, so this is a better fit than polling for line-rate markers.

First line-clock artifacts:

- LP falling raw JSON: `captures/decoded/line_clocks/20260508T161502Z-line_clocks_lp_falling.json`
- LP falling CSV: `captures/decoded/line_clocks/20260508T161502Z-line_clocks_lp_falling.csv`
- SPL falling raw JSON: `captures/decoded/line_clocks/20260508T161523Z-line_clocks_spl_falling.json`
- SPL falling CSV: `captures/decoded/line_clocks/20260508T161523Z-line_clocks_spl_falling.csv`

Summary:

| Marker | Edge | Samples | DCLK Delta Values | Distribution | Interval Notes |
|---|---|---:|---|---|---|
| LP | falling | 180 | `0`, `161` | `161` occurred 168 times; `0` occurred 12 times | Intervals about 108.6 us; zero deltas likely duplicate/narrow LP interrupt observations |
| SPL | falling | 180 | `11`, `160`, `161`, `162` | `161` occurred 134 times; `160` 22 times; `162` 23 times; one startup partial `11` | Mostly stable line intervals, with one long gap consistent with non-visible/frame boundary behavior |

The SPL result is the cleanest DCLK-per-marker evidence so far: after the first partial interval, SPL falling-to-falling spans almost exactly `160..162` DCLK rising edges. This strongly supports treating physical `DCLK` as a visible/burst pixel-transfer clock rather than the full `456`-dot internal PPU clock. However, later red/green image captures show repeated recognizable fragments when keyed from `SPL`, so `SPL` should not yet be treated as proven one-to-one visible row start. It is a strong burst-timing marker candidate, not a solved framebuffer line marker.

2026-05-08: `CAPTURE_RG_LINE_BURSTS 160 144 2000` confirmed that firmware can collect 144 SPL-keyed line bursts with 160 DCLK-rising samples per line when the inner sampling loop uses direct GPIO register reads and avoids timer calls. This is timing evidence as well as capture evidence: under the tested state, a 160-sample burst fits reliably after each SPL falling marker.

2026-05-08: LP-keyed red/green line-burst captures with marker skips `0`, `5`, and `10` also produced complete `160x144` buffers. The image fragments move coherently with marker skip, but do not collapse into a clean frame. Timing implication: `LP` likely brackets a related line/burst cadence, but the active visible transfer window still needs a marker/offset/delay model. The extra `LP` events per frame (`154`) are compatible with blanking plus 144 visible rows, but current data does not prove which LP events correspond to visible row starts.

2026-05-07: Added `CAPTURE_TIMING_EDGES <duration_ms>` for bounded input-only timestamp capture on `SPL`, `PS`, `LP`, `CLS`, and `SPS`. DCLK is intentionally excluded because it is too fast for ISR timestamp capture.

First 100 ms analyzer run:

- Raw JSON: `captures/decoded/timing_edges/20260507T214244Z-timing_edges_100ms.json`
- CSV: `captures/decoded/timing_edges/20260507T214244Z-timing_edges_100ms.csv`
- Summary JSON: `captures/decoded/timing_edges/20260507T214244Z-timing_edges_100ms_summary.json`

Summary:

| Signal | Events / 100 ms | Event Rate | Notes |
|---|---:|---:|---|
| CLS | 1833 | 18.330 kHz | Both levels captured; about two edges per 9.16 kHz cycle |
| LP | 916 | 9.160 kHz | Only low-level edge observations in ISR; pulse width/polarity still unresolved |
| PS | 1833 | 18.330 kHz | Both levels captured; closely aligned with CLS/LP line cadence |
| SPL | 862 | 8.620 kHz | Only low-level edge observations; includes longer gaps up to 1088 us |
| SPS | 12 | 120 Hz edge rate | Six complete pulses in 100 ms |

SPS rising-to-rising intervals were `16743`, `16745`, `16743`, `16740`, and `16743` us. This supports about 59.73 Hz frame cadence. LP had 916 low-level events in 100 ms, implying about 153.3 LP events per SPS frame interval under this capture condition. This is close to, but not yet proven as, a plausible line count for a 144-line display plus blanking.

2026-05-07: Added `host/analyze_timing_relationships.py` and ran it against the 100 ms timing-edge capture. Report artifacts:

- JSON: `captures/decoded/timing_relationships/20260507T214244Z-timing_relationships_100ms.json`
- Markdown: `captures/decoded/timing_relationships/20260507T214244Z-timing_relationships_100ms.md`

Frame relationship summary:

| Frame | Duration us | LP Count | SPL Count | CLS Events | PS Events | First LP After SPS | First SPL After SPS |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 0 | 16743 | 154 | 145 | 308 | 308 | 6 us | 453 us |
| 1 | 16745 | 154 | 145 | 308 | 309 | 6 us | 453 us |
| 2 | 16743 | 154 | 145 | 308 | 308 | 4 us | 451 us |
| 3 | 16740 | 154 | 145 | 308 | 307 | 3 us | 451 us |
| 4 | 16743 | 154 | 145 | 308 | 308 | 6 us | 453 us |

This is strong evidence that the frame contains 154 LP events and 145 SPL events under current conditions. Since the GBC visible height is expected to be 144 lines, `SPL` may be closely related to visible line timing while `LP` may include blanking or an extra boundary pulse. This remains a hypothesis until correlated with RGB data.

2026-05-08: Porches/blanking interpretation from reduced RGB332 capture.

Using `captures/decoded/lcdcam_raw/20260508T212557Z-lcdcam_raw_high_192x145.bin`, decoded as `161` stream bytes by `145` rows:

- The visible crop at `x=0`, width `160`, reconstructs a readable boot screen.
- The extra byte at column `160` is constant `0xff` for all `145` decoded rows.
- First and last visible columns are also background in this static boot-screen state, so this does not prove an electrical blanking value by itself.
- Logo/text activity is contained inside columns `0..159`.

Interpretation: for the current capture alignment, the `+1` byte per decoded line behaves like a trailing horizontal blank/dummy byte after the 160 visible pixels. This is porch-like, but the term remains provisional because it may also be an LCD_CAM alignment artifact.

The vertical `+1` row remains less certain. Row `144` in the current static boot screen is also background, which is consistent with a trailing blank/source-driver row, but the static screen has large background areas and cannot prove whether the extra row is before or after visible data. A boot-animation capture or another screen with non-background content near the top/bottom is needed to locate the vertical extra row confidently.

2026-05-09: DCLK edge comparison resolved a visible color instability.

The previous fast-capture preset used inverted PCLK routing. Repeated captures showed unstable `G5` samples around the `GAME BOY` text, visible as cyan sparkle. Switching to non-inverted PCLK routing removed the instability in repeated tests:

| PCLK route | GAME BOY box changed positions across repeated captures | `0x7f` sparkle count |
|---|---:|---:|
| Inverted | `111` in one 12-frame test | `45..62` per frame |
| Non-inverted | `0` in one 12-frame test | `0` per frame |

Current timing hypothesis update: the valid sample edge for the current LCD_CAM path is non-inverted DCLK/PCLK. This supersedes earlier captures that used `--pclk-invert` as the default for live viewing.

## 5. Next Steps

- Measure candidate clock and sync frequencies using high impedance probing.
- Refine GPIO33 SPS measurement to timestamp edges or capture intervals instead of only counting ISR edge callbacks.
- Replace GPIO interrupt DCLK counting with a timer/peripheral-backed method suitable for MHz-rate clocks.
- Confirm GPIO22 frequency with an oscilloscope or logic analyzer at both the GBC connector and ESP32-P4 header.
- Measure PCNT stability over longer windows and during boot/gameplay transitions.
- Capture timing edges over one controlled boot interval and compare against steady-state gameplay.
- Correlate `SPL` count of 145 with RGB data once a minimal RGB subset is connected.
- Compare `SPL` and `LP` as line marker hypotheses instead of assuming either is solved. Treat the first line-clock sample after arming as possibly partial.
- Add a programmable DCLK-edge delay after each marker in the custom line assembler to test whether useful pixels begin after a fixed phase offset.
- Stop prioritizing generic DVP polarity tweaks until a line-burst capture design has been tested.
- For line-burst captures, do not call timer/logging APIs inside the pixel loop; direct GPIO register reads are required at the current DCLK rate.
- Record measurement equipment, probe settings, and test conditions.
- Add raw captures or screenshots under `captures/oscilloscope/`.
