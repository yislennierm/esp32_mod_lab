# Platform Documentation

Purpose: ESP32-P4 platform research and board-capability documents for the signal lab.

Status: active platform documentation area.

Last updated: 2026-05-11.

Read these first when making ESP32-P4 hardware or performance decisions:

- `esp32p4_capability_matrix.md` - chip capability matrix, bandwidth budget, and research gaps.
- `esp32p4_internal_dataflow_plan.md` - performance plan for LCD_CAM/GDMA, frame rings, PPA, DMA2D, and fast sinks.
- `board_capability_audit.md` - what this specific board is known to expose safely.
- `peripheral_evidence_table.md` - peripheral ranking by evidence level and project relevance.
- `reference_pcb_architecture.md` - custom PCB architecture for source front end, ESP32-P4 core, destinations, and host/debug paths.

Keep target-specific bus facts in target/profile docs. Keep generic ESP32-P4 facts here.
