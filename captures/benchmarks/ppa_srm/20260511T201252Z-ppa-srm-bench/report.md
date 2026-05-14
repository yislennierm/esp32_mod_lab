# PPA SRM Benchmark Evidence

## Metadata

- Collected at: `20260511T201252Z`
- Port: `/dev/cu.usbmodem14401`
- Baud: `115200`
- Command: `PPA_SRM_BENCH 120`
- Timeout: `30.0 s`

## Summary

- Records collected: `2`
- Both PPA and CPU records collected: `True`
- PPA FPS: `149.715`
- PPA avg: `6679.3 us`
- PPA target met: `True`
- PPA error: `none`
- CPU FPS: `80.51`
- CPU avg: `12420.8 us`
- CPU target met: `True`
- PPA/CPU speedup: `1.860x`

## Interpretation

This artifact measures only synthetic RGB565 2x scaling inside the
normal lab firmware. It does not include GBC source capture, SPI LCD
output, USB frame transport, or browser rendering.
