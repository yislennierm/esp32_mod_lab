# Risks And Unknowns

## 1. Objective

Track project risks and unresolved technical unknowns.

This matters because reverse engineering work can fail silently if assumptions are not made explicit.

## 2. Current Understanding

Current hypothesis: the largest immediate risk is electrical damage from unsafe voltage or analog LCD rails.

Evidence: Phase 1 of `PROJECT_CHARTER.md` prioritizes electrical safety before timing discovery.

Confidence level: high.

## 3. Unknowns

- Voltage compatibility.
- Exact signal roles.
- Sample edge and timing margins.
- Blanking behavior.
- ESP32-P4 capture peripheral suitability.
- USB throughput for raw captures.
- DMA overrun behavior under real timing.

## 4. Experiment Results

2026-05-07: PCNT measurement reduced uncertainty around GPIO22 activity but introduced a new protocol risk: the measured frequency is about 1.395 MHz, not the initially expected 6-8 MHz. This may indicate a wrong signal assumption, board/header mapping issue, LCD revision difference, or an incorrect external expectation.

2026-05-07: Timing-edge capture supports SPS as a frame marker and LP as a line-related marker, but ISR timestamping still cannot prove pulse width or exact polarity for narrow LP/SPL pulses because some signals are only observed at one post-edge level.

2026-05-08: Polling-based red capture misses most DCLK edges. Risk: any image reconstruction attempted from polling samples would be misleading. Need peripheral-backed capture, likely LCD_CAM/DVP or another DMA-capable input path.

2026-05-08: Generic ESP-IDF DVP allocation requires PSRAM-enabled firmware because its DMA descriptor allocation requests `MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA`. With PSRAM disabled, `DVP_PROBE_ALLOC` failed with `ESP_ERR_NO_MEM`. With PSRAM enabled at 200 MHz HEX mode, allocation succeeds. Risk: boards without working PSRAM cannot use this generic DVP path without driver changes or a different peripheral.

2026-05-08: DVP RAW capture can produce a `160x144` buffer using `SPS` non-inverted and `SPL` non-inverted, but the capture is not yet proven correctly aligned. Risk: the image may be shifted, include blanking data, or have line/pixel phase errors until repeated captures and visual correlation are done.

2026-05-08: User observed that while live DVP capture is running, switching the GBC off/on can sometimes fail to keep power. This is a hardware safety and signal-loading risk. Possible causes include bus loading from many direct ESP32-P4 GPIO connections, back-powering through GPIO protection structures when one side is powered and the other is not, shared-ground/power transient issues, or the LCD bus not tolerating the additional capacitance. Do not expand to full RGB wiring until this is understood or mitigated with series resistors/buffers.

2026-05-08: The GBC power instability happened again while the live DVP browser viewer was running with red and green data lines connected. The live viewer process was stopped and force-killed after it remained bound to port `8766`. Treat continuous DVP capture with direct GPIO wiring as an active hardware risk until proven otherwise. No further live capture should be run without a mitigation or a controlled isolation test.

2026-05-09: The user measured 1.8 V to 2.5 V on connected GBC LCD timing/sync lines while the GBC was unpowered and the ESP32-P4 remained powered. Root-cause hypothesis: direct ESP32-P4 GPIO attachment is biasing or back-powering the unpowered GBC bus through powered input pads, peripheral routing, pad keepers, or protection structures. Existing firmware had disabled internal pulls, so ordinary pull-ups are not the likely primary cause. This is a high-priority electrical risk because it explains the unreliable off/on behavior and can stress the GBC or ESP32-P4.

2026-05-09: Firmware mitigation added: `ELECTRICAL_ISOLATE`/`SAFE_ISOLATE` detach LCD_CAM and disable the connected GPIO input/output path with no pulls. `SAFE_IDLE` remains a floating-input debug/recovery state. Residual risk remains because software cannot guarantee zero leakage through powered silicon pads into an unpowered external circuit.

## 5. Next Steps

- Convert unknowns into specific experiments.
- Verify GPIO22/DCLK frequency independently with external measurement.
- Verify narrow-pulse polarity and width with a logic analyzer or a capture path better than GPIO ISR timestamping.
- Determine whether ESP32-P4 LCD_CAM/DVP can ingest the GBC timing signals directly or whether a custom GPIO/DMA approach is needed.
- Repeat successful DVP captures and compare stability across frames, boot logo, and gameplay.
- Determine whether `SPL` corresponds exactly to visible data enable or only happens to satisfy the DVP frame-size requirement.
- Test GBC power cycling with capture stopped, ESP connected, and ESP disconnected to isolate whether the issue is continuous DVP capture, direct GPIO attachment, or general wiring/power loading.
- Before the next live capture, run a controlled isolation matrix: GBC alone, GBC with ESP connected but firmware idle, GBC with one-shot capture only, and GBC with live capture. Record whether power cycling succeeds in each condition.
- Add input protection/loading mitigation before connecting more data lines: series resistors on each digital signal at minimum, preferably a high-impedance unidirectional buffer/level-shifter stage between the GBC LCD bus and ESP32-P4.
- Specifically test the new `ELECTRICAL_ISOLATE` state with the GBC unpowered. If connected lines still read around 1.8 V or 2.5 V, stop treating direct GPIO wiring as acceptable for power-cycle work and move to hardware isolation with Ioff/partial-power-down-rated devices.
- Update risk status after each measurement or implementation milestone.
- Keep unverified assumptions visible in related docs.
