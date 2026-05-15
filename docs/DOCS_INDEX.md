# Documentation Index

Purpose: map the documentation set so humans and AI agents know what to read first.

Status: canonical navigation file.

Last updated: 2026-05-15.

## Read Order

For AI/Codex:

1. `docs/AI_CONTEXT.md`
2. `docs/DECISIONS.md`
3. `profiles/gbc_lcd.json`
4. The specific doc for the task

For humans:

1. `README.md`
2. `PROJECT_CHARTER.md`
3. `docs/system_method.md`
4. `docs/gbc_lcd_journey.html`
5. task-specific docs below

## Canonical Current-Truth Docs

| Doc | Purpose |
|---|---|
| `PROJECT_CHARTER.md` | Mission, constraints, and project rules. |
| `README.md` | Human entry point and repository overview. |
| `docs/AI_CONTEXT.md` | Compact current truth for AI sessions. |
| `docs/DECISIONS.md` | Decision log and architectural constraints. |
| `docs/DOCS_INDEX.md` | Documentation map and read order. |
| `docs/system_method.md` | Source-processing-destination method. |
| `docs/repository_strategy.md` | Lab repo vs concrete project repo strategy. |
| `docs/project_block_model.md` | Lab block and deployable project model. |
| `docs/flowgraph_lab_plan.md` | GNU Radio-inspired graph UI and ESP32-P4 block-dashboard plan. |
| `docs/graph_workspace_tools_plan.md` | Graph toolbar, layer semantics, and WYSIWYG/GNU Radio workspace plan. |
| `docs/graph_block_editors.md` | Editable graph block overlay contract and RTOS Task editor behavior. |
| `docs/esp_idf_inventory_import_plan.md` | ESP-IDF SDK inventory, example import, and SDK-backed graph plan. |
| `docs/espressif_github_inventory_plan.md` | Espressif GitHub organization inventory and external repository research plan. |
| `docs/sdk_inventory_artifacts.md` | Generated SDK and repository inventory artifact reference. |
| `docs/system_gap_assessment.md` | Current gaps and recommended roadmap. |
| `docs/esp32p4_modder_research_plan.md` | Modder-style ESP32-P4 research plan, references, and benchmark phases. |
| `docs/ant_design_ui_plan.md` | Ant Design UI direction and migration plan. |
| `profiles/gbc_lcd.json` | Machine-readable current GBC source profile. |

## Supporting Architecture Docs

| Doc | Purpose |
|---|---|
| `docs/universal_signal_lab.md` | Generic lab model and target/profile split. |
| `docs/repository_strategy.md` | Strategy for keeping `esp32_mod_lab` generic while concrete projects such as GBC live in project repos. |
| `docs/architecture.md` | Architecture history and firmware/host split. |
| `docs/investigation_workbench.md` | Browser and host workbench workflow. |
| `docs/ant_design_ui_plan.md` | Browser UI component mapping and migration plan. |
| `docs/capture_pipeline.md` | Capture path history and technical details. |
| `docs/dual_transport_strategy.md` | Native USB and WCH UART roles for safe recovery and high-FPS development. |
| `docs/platform/esp32p4_capability_matrix.md` | ESP32-P4 source, processing, destination, and USB capability matrix. |
| `docs/platform/esp32p4_internal_dataflow_plan.md` | Internal ESP32-P4 performance plan for capture, frame rings, accelerators, and sinks. |
| `docs/platform/board_capability_audit.md` | Board-specific exposure audit for ESP32-P4 peripherals and current pin ownership. |
| `docs/platform/peripheral_evidence_table.md` | Evidence-ranked ESP32-P4 peripheral table and next experiment map. |
| `docs/platform/reference_pcb_architecture.md` | Custom ESP32-P4 PCB architecture for lab and production bridge hardware. |
| `docs/firmware_recovery_workflow.md` | Repeatable build, flash, recovery, and serial-port ownership workflow. |
| `docs/production_modes.md` | Compile-time production firmware modes assembled from proven lab blocks. |
| `docs/project_block_model.md` | Contract between lab workflows, reusable blocks, and deployable projects. |
| `docs/flowgraph_lab_plan.md` | Flowgraph workbench direction, stream/message/tag model, and MCU dashboard notes. |
| `docs/graph_workspace_tools_plan.md` | Planned graph workspace tools, modes, and layer semantics. |
| `docs/graph_block_editors.md` | Graph block editor overlay model and current typed editor fields. |
| `docs/esp_idf_inventory_import_plan.md` | Plan for scanning ESP-IDF, importing examples, and mapping SDK APIs to lab graph/resource blocks. |
| `docs/espressif_github_inventory_plan.md` | Plan for tracking and researching all Espressif GitHub repositories. |
| `docs/sdk_inventory_artifacts.md` | Reference for generated inventory JSON files and generator scripts. |
| `docs/esp32p4_modder_research_plan.md` | Research path for source capture, PPA, USB, and serious display destinations. |
| `docs/destination_spi_lcd_lab.md` | Draft destination research contract for SPI LCD/IPS modules. |
| `docs/artifacts/manifest_format.md` | Future common capture manifest. |
| `docs/project_maintenance.md` | Cleanup and modularization plan. |

## GBC Target Docs

| Doc | Purpose |
|---|---|
| `docs/gbc_lcd_pinout.md` | GBC connector, current wiring, dangerous rails. |
| `docs/signal_hypotheses.md` | GBC signal role hypotheses and confidence. |
| `docs/timing_notes.md` | Timing measurements and caveats. |
| `docs/protocol_discoveries.md` | Protocol discoveries and rejected assumptions. |
| `docs/framebuffer_format.md` | Pixel/capture format notes. |
| `docs/hardware_notes.md` | Electrical and physical hardware lessons. |
| `docs/risks_and_unknowns.md` | Current risks and open questions. |
| `docs/debugging_guide.md` | Practical debugging and recovery notes. |

## Historical And Evidence Docs

| Doc | Purpose |
|---|---|
| `docs/experiment_log.md` | Chronological experiment history. |
| `docs/gbc_lcd_journey.md` | Markdown narrative of the GBC investigation. |
| `docs/gbc_lcd_journey.html` | Web-ready visual journey report. |

## Maintenance Inventories

| Doc | Purpose |
|---|---|
| `docs/document_inventory.md` | Classification of current documentation. |
| `docs/host_script_inventory.md` | Classification of current host scripts. |
| `docs/platform/README.md` | Platform documentation entry point. |
| `docs/platform/esp32p4_capability_matrix.md` | ESP32-P4 capability and benchmark planning matrix. |
| `docs/platform/esp32p4_internal_dataflow_plan.md` | ESP32-P4 internal dataflow benchmark plan. |
| `docs/platform/board_capability_audit.md` | Specific ESP32-P4 board audit and pin/peripheral exposure gaps. |
| `docs/platform/peripheral_evidence_table.md` | Peripheral evidence level table for source, processing, destination, and transport blocks. |
| `docs/platform/reference_pcb_architecture.md` | Reference hardware architecture for custom ESP32-P4 signal-lab PCBs. |
| `docs/targets/gbc_lcd/README.md` | Placeholder for future GBC target docs. |

## Documentation Rules

- Keep `docs/AI_CONTEXT.md` compact and current.
- Put durable decisions in `docs/DECISIONS.md`, not scattered through logs.
- Put machine-readable facts in profiles when possible.
- Use detailed docs for evidence, not as the only source of current truth.
- Do not move docs with image links until paths are updated or compatibility links exist.
- Every new doc should declare purpose, status, and whether it is canonical, supporting, or historical.

## When To Add A New Markdown File

Add a new file only when it has a clear role:

- canonical current truth
- decision record
- architecture contract
- target profile explanation
- evidence report
- maintenance inventory

If the information is short and temporary, add it to the relevant existing doc instead.
