# PPA SRM Bench

## Objective

Measure ESP32-P4 PPA scale/rotate/mirror throughput without source capture, USB frame streaming, browser rendering, or SPI LCD output in the hot path.

The first target is the GBC production scaling case:

```text
RGB565 160x144 -> RGB565 320x288
```

## Current Understanding

The project has already shown native-size source ingress can run at source-rate class when measured counters-only. The current SPI LCD path remains bandwidth-limited, especially at 2x.

Confidence level: high that PPA is the right block to benchmark next; unknown until measured whether it is materially faster than the current CPU scaler for this small RGB565 frame.

## Unknowns

- PPA SRM latency for `160x144 -> 320x288 RGB565`.
- Whether PSRAM DMA buffers are fast enough for this operation.
- Whether PPA output should feed a future I80/RGB/DSI destination directly.
- Whether non-blocking PPA mode is useful once a frame ring exists.

## Experiment Results

2026-05-11: Build passed, but flashing this standalone app is now blocked by default.

Reason: two attempts to work through this standalone app path forced manual board recovery on the current ESP32-P4 setup. The likely problem is not the PPA operation itself; it is that this experiment is a separate full-device ESP-IDF app with its own `sdkconfig`, console configuration, boot behavior, and transport assumptions. That bypasses the known-good lab/production firmware recovery behavior.

Current policy: do not use this app as the normal PPA test path. The script `scripts/flash_ppa_srm_bench.sh` requires `ALLOW_EXPERIMENTAL_PPA_FLASH=1` before it will flash.

Confidence level: high that standalone experiment flashing is too risky for repeated iteration on this board; unknown whether PPA SRM itself is useful until it is tested inside the known-good firmware harness.

## Next Steps

1. Keep building this experiment as compile evidence only.
2. Move the PPA SRM benchmark into the normal lab firmware as a command that does not touch GBC GPIO, SPI LCD, USB streaming, or the browser hot path.
3. Reuse the known-good firmware console/boot/recovery behavior for the first PPA measurement.
4. Compare PPA SRM against CPU scaling only after the command can be run without manual recovery.
5. Graduate PPA into production scaling only after the lab command produces stable JSON evidence.
