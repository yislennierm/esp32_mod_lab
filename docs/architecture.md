# Architecture

## 1. Objective

Define the intended system architecture for a modular ESP32-P4 based console picture-signal capture and analysis platform.

This matters because the project must remain a reusable reverse engineering toolchain, not a single-purpose Game Boy Color firmware prototype.

## 2. Current Understanding

Current hypothesis: the system should be separated into generic firmware capture modules, USB command transport, host-side decoding tools, target profiles, and documentation/capture artifacts. The GBC LCD bus is target profile 001. The initial firmware skeleton now separates `main.c`, `usb_protocol.c`, `gpio_sampler.c`, `diagnostics.c`, and capture/decoder modules. Peripheral-backed capture should be introduced as isolated probes before it becomes a production capture path.

The project should now be treated as three layers:

- ESP32-P4 instrument platform: safe GPIO control, timing capture, raw capture, transport, workbench integration, artifact creation.
- Target modules: GBC LCD first, later other console/display buses, each with profiles, pinout, safety notes, timing hypotheses, and decode presets.
- Product modules: future retimer, scaler, screen-mod, bridge, recorder, or analyzer features built from proven platform blocks.

Evidence: this structure is required by `PROJECT_CHARTER.md`.

Confidence level: high for repository organization, low for final capture architecture until electrical and timing experiments are complete.

## 3. Unknowns

- Which ESP32-P4 peripheral path is most appropriate for capture: GPIO sampling, LCD_CAM DVP, ISP DVP, or another route.
- Whether each target's picture bus maps cleanly onto camera-style PCLK/VSYNC/HSYNC assumptions.
- Final buffering strategy, DMA constraints, and PSRAM bandwidth requirements.
- Target profile schema and how generic firmware should consume profile-specific pin maps.

## 4. Experiment Results

2026-05-06: Added initial ESP-IDF firmware skeleton for Phase 1 electrical safety. No capture behavior has been enabled.

2026-05-08: Added isolated `dvp_probe.c` / `dvp_probe.h` with `DVP_PROBE_ALLOC`. This validates the ESP-IDF generic DVP driver can be linked and allocated on the board with PSRAM enabled, without routing GBC signals into the DVP peripheral.

2026-05-08: Pinned the USB command task, which currently executes CPU-polled capture commands, to HP core 1 with `xTaskCreatePinnedToCore()`. The project remains a dual-core FreeRTOS SMP build:

- `CONFIG_FREERTOS_NUMBER_OF_CORES=2`
- `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=360`
- `app_main` affinity: CPU0
- ESP timer task affinity: CPU0
- ESP timer ISR affinity: CPU0
- `usb_protocol` task pinned core: CPU1

Verification command:

```text
CORE_STATUS
```

Observed response:

```json
{"command":"CORE_STATUS","current_core":1,"esp_timer_isr_affinity":"CPU0","esp_timer_task_affinity":"CPU0","freertos_cores":2,"main_task_affinity":"CPU0","ok":true,"usb_protocol_task_pinned_core":1}
```

This does not make CPU polling a production capture path, but it removes task migration as one source of timing jitter while line-burst experiments continue.

2026-05-09: Reframed the architecture as a universal console picture-signal lab. The GBC LCD bus remains the first target profile, but generic code should expose capture primitives and target profiles should provide signal names, pin maps, voltage constraints, and decode hypotheses. Added `docs/universal_signal_lab.md`.

2026-05-09: Added `docs/project_maintenance.md` to define cleanup, modularization, and preservation rules. The core decision is that GBC becomes the first target module and reference case, while ESP32-P4 capture primitives become reusable platform blocks.

## 5. Next Steps

- Build and flash the Phase 1 firmware baseline.
- Complete Phase 1 electrical safety measurements.
- Record verified safe digital lines in the active target profile docs, currently `gbc_lcd_pinout.md`.
- Review official ESP32-P4 GPIO, LCD_CAM, and DMA documentation before choosing a firmware capture path.
- Keep DVP experiments isolated from Phase 1 GPIO polling commands until the DVP sync/data assumptions are proven.
- Keep timing-sensitive CPU-polled experiments pinned to one HP core or replace them with peripheral/DMA-backed capture.
- Introduce a profile format so future consoles can reuse the same capture, artifact, and viewer tooling.
- Keep GBC-specific behavior working while gradually extracting generic instrument APIs.
- Use `docs/project_maintenance.md` as the gate before deleting generated files or moving experiment scripts.
