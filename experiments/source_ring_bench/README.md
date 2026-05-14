# Source Ring Bench

Isolated ESP32-P4 source-ingress benchmark.

This app exists to test the research plan without lab-mode interference. It does not start the browser/workbench command protocol, destination SPI LCD code, TinyUSB, PNG rendering, or frame streaming.

The app boots, configures the current GBC LCD source profile, runs source-ingress benchmark variants, and prints JSON counters over the default UART console.

Current test paths:

```text
GBC LCD bus
    -> LCD_CAM/GDMA
    -> double-buffer rearm
    -> JSON counters only

GBC LCD bus
    -> LCD_CAM/GDMA
    -> esp_cam_ctlr DVP cyclic buffers
    -> JSON counters only

GBC LCD bus
    -> LCD_CAM/GDMA
    -> low-level cyclic descriptor ring
    -> JSON counters only
```

The result schema is `esp32_mod_lab.benchmark.source_ring.v1`.

Build:

```text
./scripts/build_source_ring_bench.sh
```

Flash:

```text
./scripts/flash_source_ring_bench.sh <serial-port>
```

Current key result from 2026-05-11:

- `192x145 RGB565` low-level ring: about `50.09 fps`, no drops/errors.
- Native visible `160x144 RGB565` low-level ring: about `60.53 fps`, no drops/errors, `target_rate_met=true`.

This app is not the final production frame ring. It is the isolated benchmark used to prove which capture path is worth promoting into production firmware.
