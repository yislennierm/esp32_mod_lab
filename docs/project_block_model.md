# Project Block Model

## Objective

Define how the ESP32-P4 Signal Lab separates reusable investigation tooling from concrete project firmware.

This matters because the same board should support both:

- lab work: measure pins, test hypotheses, capture frames, validate destinations, collect evidence
- project work: deploy a proven source-processing-destination pipeline with as little overhead as possible

## Current Understanding

The project now has three layers:

| Layer | Purpose | Example |
|---|---|---|
| Lab firmware and UI | Interactive investigation and evidence collection | pin reads, timing capture, live monitor, SPI LCD test patterns |
| Blocks | Reusable source, processing, destination, or transport modules | `gbc_lcd_source`, `spi_lcd_destination` |
| Projects | A deployable composition of blocks | `gbc_spi_lcd_mirror` |

The first real project is:

```text
GBC LCD source -> no processing -> SPI LCD destination
```

Each concrete project now targets three firmware build flavors:

- `lab`: research, probing, capture review, evidence collection, and destination bring-up
- `telemetry`: selected runtime observation and monitoring of chosen ESP32-P4 blocks
- `production`: minimal deployable product firmware with the lab control path removed from the hot loop

The UI should treat this as a project profile, not as a one-off set of buttons. A project can be validated, built, flashed, and monitored from the browser through the local backend.

Confidence level: medium-high for the model, medium for the exact JSON schema. The model matches the workflow that produced the current GBC mirror, but it should remain flexible until a second source or destination is added.

## Unknowns

- Whether project profiles should generate C headers directly or only drive build-time environment variables.
- How much of a production project should remain configurable from the UI after validation.
- Whether long-running build and flash jobs need a persistent job queue instead of synchronous HTTP actions.
- How to represent exclusive peripheral ownership once more ESP32-P4 blocks are active at the same time.

## Experiment Results

Initial implementation:

- `projects/gbc_spi_lcd_mirror.json` describes the current GBC-to-SPI-LCD project.
- `projects/hello_led.json` is a safe draft sandbox project for developing UI, graph, and GPIO concepts without touching the GBC project.
- The browser backend exposes block and project APIs.
- The Project tab can validate the GPIO composition and request build/flash actions.
- Project JSON files can now be created, saved, duplicated, and deleted through backend endpoints.
- Production deployment still uses the existing scripts so ESP-IDF behavior stays traceable.

The known-good production environment is:

```text
DEST_SPI_LCD_RAW_SPI=1
DEST_SPI_LCD_PCLK_HZ=70000000
PRODUCTION_MIRROR_MODE=2
```

The current GBC project metadata now exposes all three build profiles through the workbench so build/flash actions no longer assume a single production-only deployment path.

Current filesystem reality:

- `projects/` is already the right place for deployable compositions
- `profiles/` is already the right place for source/destination metadata
- `host/workbench/` already behaves like shared lab infrastructure
- `firmware/main/` is still carrying both lab and GBC-specific code, but the first compatibility-wrapped target slice now points `gbc_lcd_source.*` and `pinmap_gbc.h` at `firmware/targets/gbc_lcd/`
- `host/targets/` and `docs/targets/` exist, but most active GBC implementation and documentation has not moved there yet

Near-term filesystem policy:

- create explicit split directories before moving known-good code
- keep active firmware includes and scripts stable until each migration slice is tested
- prefer wrapper or compatibility layers when moving target-specific host tools out of top-level `host/`
- do not move evidence-heavy docs if it breaks artifact links or the historical investigation chain

## Next Steps

- Move block metadata into explicit profile files once a second project exists.
- Add form-based editing for project graph nodes, parameters, and metadata.
- Add a backend job log so build and flash output can stream into the UI.
- Add profile-specific telemetry configuration so telemetry builds can explicitly choose which ESP32-P4 blocks remain observable.
- Add generated firmware configuration from project profiles instead of relying only on environment variables.
- Add a project compatibility check for peripheral ownership, memory class, and transport bandwidth.
