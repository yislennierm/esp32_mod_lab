# Hardware Notes

## 1. Objective

Record hardware setup, electrical safety constraints, wiring, level shifting, and measurement equipment.

This matters because the ESP32-P4 GPIOs are not 5V tolerant and the GBC LCD connector includes dangerous analog rails.

## 2. Current Understanding

Current hypothesis: all LCD lines are unsafe until measured, and all ESP32-P4 capture GPIOs must remain input-only during Phase 1. The initial firmware assigns no GBC capture GPIOs.

Evidence: this is mandated by `PROJECT_CHARTER.md`.

Confidence level: high.

## 3. Unknowns

- Actual voltage levels on candidate digital lines.
- Required level shifting or buffering topology.
- Safe grounding and probing setup.
- Maximum input frequency margin for selected ESP32-P4 pins/peripherals.

## 4. Experiment Results

2026-05-06: Firmware pin map created with zero assigned capture pins to avoid accidental GPIO connection assumptions.

2026-05-06: Host detected `/dev/cu.usbmodem5A470211841` and `/dev/cu.wchusbserial5A470211841`. Both ports opened, but the ESP32-P4 did not respond to automatic bootloader entry during flashing.

2026-05-06: Board later enumerated as `/dev/cu.usbmodem14201`; flashing succeeded on that port. ESP32-P4 revision v1.0 was reported by esptool.

2026-05-06: USB-Serial/JTAG was confirmed usable for command I/O after firmware console configuration was changed from UART0 primary to USB-Serial/JTAG primary.

2026-05-06: Phase 1 voltage-measurement template generated in `captures/experiments/20260505T223950Z-phase1-baseline-smoke/phase1_voltage_template.csv`. Candidate digital signals are marked `unknown`; V0-V9, VCOM, VEE, VSHA, and VSHD are marked `dangerous`.

2026-05-06: Added `host/validate_phase1_measurements.py` as the required validation gate before any measured signal can be promoted into firmware pin mapping.

2026-05-06: Updated Phase 1 voltage templates now include a `proposed_gpio` column. A template with all candidate signals left `unknown` validates as no-op evidence and proposes no firmware pinmap changes.

2026-05-06: GPIO33 added as a board-labeled Phase 1 test input. This does not assign it to GBC capture and does not allow GPIO output.

2026-05-07: Added official-doc-derived GPIO candidate tiers in `docs/esp32p4_gpio_inventory.md`. Early preferred candidates are GPIO26-GPIO33 and GPIO39-GPIO48; GPIO24-GPIO25 are avoided while USB-Serial/JTAG is in use; GPIO34-GPIO38 are avoided because they are strapping pins.

2026-05-09: With GBC battery/bench power disconnected and only the ESP32-P4 powered, the user measured about 1.8 V to 2.5 V on several connected GBC LCD timing/sync lines. This indicates the ESP32-P4 side can back-power or bias an unpowered target through direct GPIO connections, even when the firmware intends to use input-only capture.

Firmware mitigation added: `SAFE_IDLE` detaches LCD_CAM inputs and leaves connected capture GPIOs as floating inputs for normal recovery/debug work. `ELECTRICAL_ISOLATE`/`SAFE_ISOLATE` detaches LCD_CAM inputs and configures connected capture GPIOs as `GPIO_MODE_DISABLE` with pull-ups and pull-downs disabled. The browser Stop path uses `ELECTRICAL_ISOLATE` so the ESP32-P4 pads are disabled before the user power-cycles the target. This is software isolation only; it is not equivalent to a physical high-Z bus switch when the target is unpowered.

2026-05-09 update: firmware now enters `ELECTRICAL_ISOLATE` during `app_main()` startup before the USB command loop. This reduces the time that known connected LCD bus pins remain in default or previous peripheral states after firmware starts. It does not control ESP32-P4 ROM/reset behavior before application startup.

## 5. Next Steps

- Keep `GBC_CAPTURE_PINS` empty until measured pin assignments are recorded.
- Treat `/dev/cu.usbmodem14201` as the currently verified flashing port, but rescan ports after reset because macOS device names can change.
- Validate measurement CSVs with `host/validate_phase1_measurements.py` before editing firmware pin assignments.
- Use `READ_GPIO 33` only for input-only board/command-path tests.
- Measure candidate digital lines with high impedance probes.
- After flashing the updated firmware, stop live capture or run `ELECTRICAL_ISOLATE` with the GBC unpowered and re-measure connected LCD lines. If the lines still sit significantly above ground, add hardware isolation before further power-cycle testing.
- Add hardware protection for production use: series resistors on every digital LCD bus line at minimum, preferably buffers or bus switches with partial-power-down/Ioff behavior and output-enable controlled by target power-good.
- Record equipment model, probe attenuation, and reference ground.
- Do not connect analog rails V0-V9, VCOM, VEE, VSHA, or VSHD to ESP32-P4 GPIO.
