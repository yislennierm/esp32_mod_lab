# Graph Workspace Tools Plan

Purpose: define the next graph-toolbar and graph-semantics direction for the ESP32-P4 Signal Lab.

Status: active UI architecture plan.

Last updated: 2026-05-14.

## Objective

Move the graph from a decorative project diagram toward a GNU Radio/WYSIWYG hybrid workspace.

This matters because SDK examples, GBC pipelines, and future production firmware need to show real system structure:

- dataflow
- control/message flow
- build/config dependencies
- ESP32-P4 peripheral/resource usage
- pins and routing
- memory/DMA ownership
- evidence and runtime observability

## Current Understanding

The first SDK import graph was wrong because it chained API groups as if they were sequential processing blocks.

Correct model:

- ESP-IDF examples are not always dataflow systems.
- Imported SDK graphs should show layers:
  - system intent
  - functional building blocks
  - buses/interfaces
  - ESP32-P4 MCU resources
  - external electronics
- Source files, CMake, sdkconfig, and other code/build details belong in the inspector, not as graph nodes.
- Edges should say `control/data`, `claims`, `drives`, and `connects`.
- Real source-processing-destination projects can use stream/message/tag edges later.

Confidence level: high for layer model, medium for exact visual grammar.

## Unknowns

- How much graph editing should be allowed before the graph compiler exists.
- Whether React Flow remains enough after routing/pin/memory overlays become rich.
- How to show large SDK examples without overcrowding the canvas.
- How to separate inferred metadata from proven hardware behavior visually.

## Toolbar Plan

Graph toolbar modes:

- Select: inspect nodes and edges.
- Resource: emphasize ESP32-P4 blocks, peripherals, GPIOs, DMA, memory.
- Flow: emphasize streams, messages, tags, timing, and data movement.
- Build: emphasize CMake, components, Kconfig, sdkconfig, generated headers.
- Inspector: open/close details panel.

Future tools:

- Import: open SDK/import browser from graph context.
- Validate: run project/resource/pin/bandwidth validation.
- Generate: produce firmware config or production mode artifacts.
- Evidence: attach captures, screenshots, reports, and decisions to nodes.
- Probe: bind a graph node to a live ESP32-P4 command or telemetry source.

## Experiment Results

Implemented first toolbar scaffold:

- Select icon
- Resource icon
- Flow icon
- Build icon
- Inspector icon
- Save layout icon

Implemented improved SDK example import graph:

```text
System Intent
├── Functional Blocks
├── ESP32-P4 Resources
└── External Electronics
```

This is still read-only, but it avoids fake sequential API chains and keeps code/build metadata out of the canvas.

Implemented first interactive behavior:

- Toolbar modes filter visible graph layers:
  - Select: show all graph nodes and edges.
  - Resource: show functional blocks and ESP32-P4 resource claims.
  - Flow: show functional blocks and external electronics, hiding MCU resource nodes.
  - Build: show functional blocks only while build/source metadata remains in the inspector.
- Nodes are selectable.
- The inspector shows selected node metadata.
- Imported SDK metadata is available in an inspector tab.
- Nodes are draggable.
- Layout edits can be saved back into the project JSON.

Implemented first editable block overlay:

- RTOS Task blocks expose a typed inspector editor.
- The editor writes `params.overlay` metadata into the lab project, not vendor source files.
- Editable fields are task enabled state, task name, priority, stack size, core affinity, and notes.
- Priority and stack size use sliders plus numeric inputs so quick exploration and exact values are both possible.
- This establishes the interaction pattern for future block editors.

## Next Steps

- Add typed node editors for source, ESP LCD, SPI, GPIO, DMA, memory, and external-device blocks.
- Add ESP32-P4 block mini-map or side palette for active resource claims.
- Add graph legends for edge semantics.
- Add import action from graph toolbar.
- Add validation overlays for conflicts and unknowns.
