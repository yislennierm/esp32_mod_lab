# ESP-IDF Inventory And Example Import Plan

Purpose: define how the Signal Lab should stay tied to ESP-IDF and ESP32-P4 SDK reality instead of becoming a manually maintained toy block editor.

Status: canonical plan for the next flowgraph/lab architecture phase.

Last updated: 2026-05-14.

## Objective

Build an ESP-IDF-aware inventory and importer so the lab can understand ESP32-P4 peripherals, drivers, examples, configuration, resources, and source code structure from the installed SDK.

This matters because the graph UI should represent real ESP-IDF systems:

- components and their APIs
- peripheral ownership
- DMA and memory paths
- GPIO matrix and IO MUX routing
- Kconfig and `sdkconfig.defaults`
- CMake component dependencies
- official example project structure
- generated firmware modes

The goal is not to drag an LED onto a canvas and pick a pin. The goal is closer to GNU Radio Companion for ESP32-P4 firmware: a project graph backed by SDK metadata, source files, build configuration, and hardware resource validation.

## Current Understanding

The local ESP-IDF install at `/Users/nene/esp/v5.5/esp-idf` contains useful ESP32-P4-relevant examples and components.

High-value example families found locally:

- `examples/peripherals/camera/dvp_isp_dsi`
- `examples/peripherals/camera/mipi_isp_dsi`
- `examples/peripherals/lcd/rgb_panel`
- `examples/peripherals/lcd/i80_controller`
- `examples/peripherals/lcd/spi_lcd_touch`
- `examples/peripherals/lcd/mipi_dsi`
- `examples/peripherals/lcd/parlio_simulate`
- `examples/peripherals/ppa/ppa_dsi`
- `examples/peripherals/parlio/parlio_rx/logic_analyzer`
- `examples/peripherals/parlio/parlio_tx/simple_rgb_led_matrix`
- `examples/peripherals/parlio/parlio_tx/advanced_rgb_led_matrix`
- `examples/peripherals/spi_master/lcd`
- `examples/peripherals/usb/device/tusb_serial_device`
- `examples/peripherals/usb/device/tusb_ncm`
- `examples/peripherals/usb/host/uvc`
- `examples/peripherals/gpio/generic_gpio`
- `examples/peripherals/bitscrambler`

High-value ESP-IDF components found locally:

- `esp_driver_cam`
- `esp_driver_isp`
- `esp_driver_ppa`
- `esp_driver_spi`
- `esp_driver_parlio`
- `esp_driver_bitscrambler`
- `esp_driver_usb_serial_jtag`
- `esp_driver_gpio`
- `esp_lcd`
- `usb`

Confidence level: high that official ESP-IDF examples are useful as import fixtures and canonical API references. Medium that they fully cover the GBC-style source-to-destination bridge, because our project mixes non-camera source timing, LCD_CAM-style capture, memory rings, optional PPA, USB telemetry, and display output.

## Unknowns

- How much detail can be extracted reliably from C source without a full C parser.
- Whether ESP-IDF example metadata is consistent enough to support fully automatic graph creation.
- Whether the first importer should copy examples into `projects/` or keep read-only references to the SDK path.
- How to represent CMake/Kconfig changes that are conditional on target, board, or example-specific component dependencies.
- How to map every SDK component API to ESP32-P4 internal blocks without a manually curated descriptor layer.
- How much editing should be round-tripped back into source code versus represented as a lab overlay that later generates source.

## Architecture

The lab should use four layers.

## 1. SDK Inventory Layer

Scans the installed ESP-IDF tree and produces a versioned inventory file.

Proposed output:

```text
sdk_inventory/
└── esp-idf-v5.5-esp32p4.json
```

The inventory should include:

- ESP-IDF path
- ESP-IDF version
- selected target
- component list
- example list
- example categories
- `CMakeLists.txt` paths
- `idf_component.yml` paths
- `sdkconfig.defaults` paths
- source file paths
- detected headers
- detected ESP-IDF API calls
- detected Kconfig symbols
- inferred MCU block usage

This file is generated, not hand-authored. When ESP-IDF changes, the inventory can be regenerated.

## 2. Descriptor Layer

Maps SDK concepts to lab concepts.

Example descriptor shape:

```json
{
  "id": "esp_driver_spi_master",
  "sdk_component": "esp_driver_spi",
  "apis": [
    "spi_bus_initialize",
    "spi_bus_add_device",
    "spi_device_transmit",
    "spi_device_queue_trans"
  ],
  "mcu_blocks": [
    "SPI",
    "GPIO Matrix / IO MUX",
    "GDMA",
    "Internal DMA RAM"
  ],
  "resources": {
    "peripheral": "SPI host",
    "gpio": ["sclk", "mosi", "miso", "cs"],
    "dma_capable_buffers": true
  },
  "ports": [
    {"name": "pixels_in", "type": "stream", "format": "rgb565"},
    {"name": "control", "type": "message"}
  ]
}
```

Descriptors are the compatibility layer between ESP-IDF and the lab. They let the graph stay stable even if the SDK implementation changes.

## 3. Project Graph Layer

Represents a project as a real firmware system:

- source blocks
- processing blocks
- destination blocks
- control/message paths
- timing tags
- resource claims
- firmware mode
- build profile
- evidence links

The graph should support multiple views of the same project:

- flowgraph view: data/control movement
- MCU view: ESP32-P4 blocks and resource usage
- source view: C files, CMake, Kconfig
- evidence view: captures, screenshots, timing reports
- deployment view: build/flash/runtime status

This is the WYSIWYG plus GNU Radio combination:

- WYSIWYG for hardware topology, board pins, MCU blocks, and live status
- GNU Radio-style graph for typed streams, messages, tags, buffers, and scheduling
- ESP-IDF project view for source files, build options, components, and examples

## 4. Import And Generation Layer

Imports official examples into lab projects and eventually generates deployable firmware.

Initial importer behavior:

- read an ESP-IDF example directory
- index its files
- detect components and APIs
- infer graph nodes from known descriptors
- show unresolved code as generic source nodes
- mark imported source as read-only by default
- create a lab project JSON that references the example path

Later generator behavior:

- produce C headers from graph configuration
- produce `sdkconfig.defaults` fragments
- produce CMake component dependencies
- select lab firmware or production firmware mode
- build through `idf.py`

## Example Import Semantics

Official examples should be treated as canonical evidence, not copied blindly.

Example import for `examples/peripherals/spi_master/lcd` should produce something like:

```text
ESP-IDF Example Project
├── Component: esp_driver_spi
├── Component: esp_lcd
├── MCU: SPI
├── MCU: GPIO Matrix / IO MUX
├── MCU: GDMA / DMA-capable buffers
├── Graph:
│   ├── Pattern Source
│   ├── SPI LCD Panel IO
│   └── LCD Panel Driver
└── Editable Overlays:
    ├── pin assignment
    ├── panel dimensions
    ├── pixel format
    ├── SPI clock
    └── transfer strategy
```

Example import for `examples/peripherals/camera/dvp_isp_dsi` should produce:

```text
DVP Sensor Source -> ISP -> DSI Display
       |              |          |
       v              v          v
   CAM/LCD_CAM      ISP       MIPI DSI
       |
       v
     GDMA / frame buffers
```

The imported graph must show uncertainty if an API cannot be mapped.

## UI Requirements

The graph section should be arranged around three synchronized panels.

Left/project panel:

- active project
- imported ESP-IDF example, if any
- source files
- build profile
- target and SDK version

Center/workspace:

- flowgraph canvas
- MCU block overlay mode
- resource-conflict overlays
- stream/message/tag edges
- high-rate path emphasis

Right/inspector:

- selected block descriptor
- ESP-IDF APIs used
- resource claims
- Kconfig symbols
- pins and routing
- evidence links
- generated firmware outputs

The dashboard remains global. The flowgraph is per project.

## Why ESP-IDF Examples Help

Yes, the examples directly help realize the lab.

They provide:

- official API usage
- known-good CMake structure
- known-good `sdkconfig.defaults`
- examples of component dependencies
- examples of target-specific drivers
- practical peripheral initialization sequences
- regression fixtures for the importer

They do not replace our architecture.

They are not enough because:

- examples are usually single-purpose
- examples may use fixed pins or board assumptions
- examples may not optimize for our source-processing-destination pipeline
- examples do not describe our evidence workflow
- examples do not decide lab mode vs production mode
- examples do not solve resource conflicts across combined blocks

The lab should learn from examples, represent them, and then let us compose better firmware systems.

## Implementation Plan

Phase 1: inventory generator.

- Add `host/idf_inventory.py`. Status: done.
- Scan ESP-IDF examples and components. Status: done.
- Generate `sdk_inventory/esp-idf-v5.5-esp32p4.json`. Status: done.
- Detect basic metadata first: paths, files, components, sdkconfig defaults, source files. Status: done.

Phase 2: API detector.

- Detect common ESP-IDF APIs with regex-based extraction.
- Map APIs to descriptors for SPI, LCD, GPIO, USB, camera, PPA, PARLIO, bitscrambler.
- Mark unmapped APIs explicitly.

Phase 3: backend endpoints.

- `GET /api/sdk/idf`
- `GET /api/sdk/examples`
- `GET /api/sdk/examples/{id}`

Status: initial read-only endpoints are implemented in `host/live_lcdcam_stream_viewer.py`.
- `POST /api/projects/import-idf-example`

Phase 4: UI import view.

- Add an ESP-IDF inventory/project import panel.
- Filter examples by component, peripheral, category, and target support.
- Import an example into a lab project.
- Display source files and inferred graph.

Phase 5: editable overlays.

- Let the user change lab overlays first:
  - pins
  - clocks
  - resolution
  - pixel format
  - buffer count
  - memory placement
- Keep SDK example source read-only until generation rules are reliable.

Phase 6: graph-to-firmware generation.

- Generate configuration headers and build variables from the graph.
- Keep production firmware separate from lab firmware.
- Keep generated code obvious and traceable.

## Next Steps

- Implement the inventory generator with read-only scanning of the installed ESP-IDF tree.
- Add the first generated inventory JSON to the repo.
- Add backend endpoints for SDK inventory and examples.
- Add UI project import controls.
- Start with these importer test examples:
  - `get-started/blink`
  - `peripherals/gpio/generic_gpio`
  - `peripherals/spi_master/lcd`
  - `peripherals/lcd/rgb_panel`
  - `peripherals/camera/dvp_isp_dsi`
  - `peripherals/ppa/ppa_dsi`
  - `peripherals/parlio/parlio_rx/logic_analyzer`
  - `peripherals/usb/device/tusb_serial_device`
