# Documentation Index

Purpose: map the documentation set so humans and AI agents know what to read first.

Status: canonical navigation file.

Last updated: 2026-05-10.

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
| `docs/system_gap_assessment.md` | Current gaps and recommended roadmap. |
| `docs/ant_design_ui_plan.md` | Ant Design UI direction and migration plan. |
| `profiles/gbc_lcd.json` | Machine-readable current GBC source profile. |

## Supporting Architecture Docs

| Doc | Purpose |
|---|---|
| `docs/universal_signal_lab.md` | Generic lab model and target/profile split. |
| `docs/architecture.md` | Architecture history and firmware/host split. |
| `docs/investigation_workbench.md` | Browser and host workbench workflow. |
| `docs/ant_design_ui_plan.md` | Browser UI component mapping and migration plan. |
| `docs/capture_pipeline.md` | Capture path history and technical details. |
| `docs/dual_transport_strategy.md` | Native USB and WCH UART roles for safe recovery and high-FPS development. |
| `docs/firmware_recovery_workflow.md` | Repeatable build, flash, recovery, and serial-port ownership workflow. |
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
| `docs/platform/README.md` | Placeholder for future platform docs. |
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
