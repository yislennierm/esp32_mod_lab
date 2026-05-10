# Document Inventory

## 1. Objective

Classify current documentation before splitting files into platform and target-specific folders.

This matters because the project now has two overlapping documentation needs: preserving the GBC journey and building a reusable ESP32-P4 signal lab.

## 2. Current Understanding

Documentation should eventually be grouped by responsibility:

- platform docs for reusable ESP32-P4 lab behavior
- target docs for the GBC LCD module
- artifact docs for evidence formats
- maintenance docs for cleanup and project history

No docs should be moved until links are checked and compatibility pointers are added.

## 3. Unknowns

- Which docs should remain at top-level for easy access.
- Whether GBC-specific docs should move under `docs/targets/gbc_lcd/` or stay top-level until a second target exists.
- How much of the journey report should be copied into a permanent archive package.

## 4. Experiment Results

2026-05-10 inventory:

| Document | Class | Notes |
|---|---|---|
| `PROJECT_CHARTER.md` | project governance | Top-level controlling document. |
| `README.md` | project entry | Keep top-level. |
| `docs/AI_CONTEXT.md` | canonical current truth | Compact current project state for AI/Codex sessions. |
| `docs/DECISIONS.md` | canonical decisions | Durable decision log and constraints. |
| `docs/DOCS_INDEX.md` | canonical navigation | Documentation map and read order. |
| `docs/README.md` | docs entry | Short entry point for the docs directory. |
| `docs/architecture.md` | mixed platform/GBC | Split later after generic architecture stabilizes. |
| `docs/ant_design_ui_plan.md` | platform/UI | Ant Design component mapping and migration plan. |
| `docs/capture_pipeline.md` | platform | Capture pipeline should become generic. |
| `docs/debugging_guide.md` | mixed platform/GBC | Keep until GBC-specific troubleshooting is separated. |
| `docs/dual_transport_strategy.md` | platform/transport | Native USB and WCH UART roles for recovery, app control, logs, and high-FPS streaming. |
| `docs/esp32p4_gpio_inventory.md` | platform/hardware | ESP32-P4 board capability notes. |
| `docs/experiment_log.md` | project history | Keep as chronological record. |
| `docs/framebuffer_format.md` | mixed target/decode | Current details are GBC-heavy. |
| `docs/firmware_recovery_workflow.md` | platform/firmware | Repeatable ESP32-P4 build, flash, recovery, and verification procedure. |
| `docs/future_work.md` | project roadmap | Keep top-level. |
| `docs/gbc_lcd_journey.html` | target story | Web-ready GBC preservation report. |
| `docs/gbc_lcd_journey.md` | target story | Markdown companion to HTML report. |
| `docs/gbc_lcd_pinout.md` | target | GBC-specific; future `docs/targets/gbc_lcd/` candidate. |
| `docs/hardware_notes.md` | mixed hardware/target | Contains safety lessons; split later. |
| `docs/host_script_inventory.md` | maintenance | Current host script classification. |
| `docs/investigation_workbench.md` | platform | Browser/lab workflow. |
| `docs/project_maintenance.md` | maintenance | Cleanup gate and plan. |
| `docs/protocol_discoveries.md` | target | GBC discoveries so far. |
| `docs/risks_and_unknowns.md` | mixed | Keep top-level until risks are separated by target/platform. |
| `docs/signal_hypotheses.md` | target | GBC signal role hypotheses. |
| `docs/system_gap_assessment.md` | maintenance/architecture | Gap assessment against the full system method. |
| `docs/system_method.md` | platform/architecture | Repeatable source-processing-destination method. |
| `docs/timing_notes.md` | target | GBC timing evidence. |
| `docs/universal_signal_lab.md` | platform | Generic architecture direction. |
| `docs/artifacts/manifest_format.md` | platform/artifacts | Future common manifest format. |

## 5. Next Steps

- Add `docs/platform/README.md` and `docs/targets/gbc_lcd/README.md` before moving docs.
- Keep top-level compatibility links if docs are moved.
- Do not move the journey HTML until its image links are updated or copied into an archive package.
