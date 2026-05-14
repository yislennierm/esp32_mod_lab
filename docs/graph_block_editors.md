# Graph Block Editors

Purpose: define how graph blocks expose editable lab parameters before source-code generation exists.

Status: active architecture contract.

Last updated: 2026-05-14.

## Objective

Define the interaction model for editable GNU Radio-style graph blocks in the ESP32-P4 Signal Lab.

This matters because imported ESP-IDF examples should be inspectable and editable as system blocks without silently modifying vendor example source code.

## Current Understanding

Graph blocks have two layers:

- imported evidence: SDK metadata, inferred API groups, MCU resources, and source paths
- lab overlay: user-editable block intent, parameters, notes, and future code-generation hints

Current confidence level: high for using overlays before generation, medium for the final parameter set per block type.

The first typed editor is the RTOS Task block used by imported FreeRTOS examples.

Editable overlay fields:

| Field | UI Control | Reason |
|---|---|---|
| Enabled | Switch | Binary scheduling/build intent. |
| Task name | Text input | Exact symbolic name should be typed, not selected. |
| Priority | Slider plus numeric input | Fast comparison by slider, exact value by input. |
| Stack size bytes | Slider plus numeric input | Common values are stepped, but exact byte count must remain possible. |
| Core affinity | Select | Small fixed set: any core, core 0, core 1. |
| Notes | Text area | Captures assumptions and generation rationale. |

The overlay is stored under the graph node's `params.overlay` object. `source_write_policy` is `overlay_only`, meaning the imported example source remains read-only.

Inspector layout:

| Tab | Role |
|---|---|
| Block | Primary authoring surface for the selected node. Shows typed editor, connections, metadata, and debug JSON. |
| Context | Project-level source, destination, graph size, status, and device state. |
| SDK | Imported ESP-IDF example metadata, source-file references, CMake, and sdkconfig references. |
| Library | Selected-project blocks first, then reusable registry candidates. This is the future palette source. |
| Model | Inspector rules and block-authoring semantics. |

Raw JSON must remain available for AI/debugging, but it should live behind an explicit debug section. The normal user workflow should be through typed controls and concise context.

The Library tab is project-oriented. It should not imply that the current GBC proof target is active when another project, such as `hello_led`, is selected. Global/reusable blocks are secondary candidates until the user adds them to the selected project graph.

## Unknowns

- Final FreeRTOS priority limits per generated target profile.
- Whether stack size should be represented in bytes, words, or both for every target.
- Whether core affinity should expose ESP-IDF's exact affinity constants or a lab-neutral enum.
- How validation should flag impossible combinations before code generation.

## Experiment Results

Implemented first RTOS Task inspector editor for SDK-imported graph nodes:

- RTOS task nodes can be selected and dragged.
- The inspector exposes task name, enabled state, priority, stack size, core affinity, and notes.
- Edits are stored as lab overlay metadata in the project JSON.
- The graph summary line reflects task priority and stack size.
- Saving the graph persists layout and overlay edits together.
- Inspector was reorganized so the selected block has a compact summary, typed editor, connection table, metadata section, and debug JSON section.

No source files from imported ESP-IDF examples are modified by this editor.

## Next Steps

- Add typed editors for ESP LCD, SPI bus, GPIO, PSRAM, GDMA, PPA, TinyUSB, and external panel blocks.
- Add validation rules that compare overlay claims against ESP32-P4 capabilities and current pin ownership.
- Add a generator preview that shows what C/FreeRTOS code would be emitted before writing files.
- Add palette-based block creation so users can add source, processing, destination, and RTOS blocks without importing an SDK example first.
