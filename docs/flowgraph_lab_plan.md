# Flowgraph Lab Plan

## Objective

Define the path from the current ESP32-P4 Signal Lab toward a GNU Radio-inspired visual block system for console/display bus investigation and deployable firmware generation.

This matters because the lab should become more than a collection of commands. It should let a user compose, validate, observe, and eventually compile source-processing-destination pipelines.

## Current Understanding

GNU Radio provides the useful model:

- flowgraphs composed from typed blocks
- streams for high-rate data
- messages for asynchronous control
- stream tags for metadata tied to sample positions
- block descriptions for UI/tooling
- performance counters per block
- companion UI for composing and generating runnable systems

The ESP32-P4 lab needs a stricter version of this model because MCU graphs must respect:

- GPIO ownership
- peripheral ownership
- DMA-capable memory requirements
- PSRAM/internal RAM tradeoffs
- bandwidth limits
- electrical safety
- production vs lab firmware modes

Confidence level: high for the model, medium for the first graph schema.

## Unknowns

- Whether React Flow remains sufficient once graph editing and validation overlays become advanced.
- Whether block descriptors should live beside firmware modules or in a central `blocks/` registry.
- How much graph compilation should generate C headers vs build environment variables.
- How to represent stream tags and timing evidence in the first editable schema.

## Experiment Results

Initial implementation:

- Added a read-only `Graph` tab using React Flow.
- Added a dashboard-style ESP32-P4 block map inspired by the official Espressif functional block diagram.
- The current graph renders:

```text
GBC LCD Source -> LCD_CAM + GDMA -> Production Mirror -> SPI LCD
                                      |
                                      v
                                  Workbench telemetry
```

The MCU dashboard currently highlights:

- HP RISC-V cores
- L2MEM / Cache
- GPIO Matrix / IO MUX
- GDMA
- LCD_CAM
- SPI
- Internal DMA RAM
- PSRAM
- USB Serial/JTAG when the device is connected

## Next Steps

- Move MCU block and project graph metadata into JSON descriptors.
- Add an ESP-IDF inventory layer so graph blocks map to real SDK components, APIs, examples, Kconfig symbols, and target capabilities.
- Add ESP-IDF example import so official examples can become inspectable lab projects rather than separate reference code.
- Add node click inspection and per-node evidence links.
- Add validation overlays for pin/peripheral/bandwidth conflicts.
- Add editable graph mode after the read-only view is stable.
- Add a graph compiler that emits project profiles and firmware build configuration.

See `docs/esp_idf_inventory_import_plan.md` for the SDK-backed inventory/import architecture.
