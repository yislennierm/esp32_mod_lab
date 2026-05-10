# ESP32-P4 GPIO Inventory

## 1. Objective

Track board-visible ESP32-P4 GPIO candidates before assigning them to GBC LCD bus capture.

This matters because the board header labels may not expose every ESP32-P4 GPIO, and the project must distinguish board labels from physical IC package pins.

## 2. Current Understanding

Current hypothesis: a board header labeled `GPIO33` maps to ESP32-P4 logical GPIO33. This does not mean physical IC package pin 33.

Evidence: user reports the board exposes numbered GPIO pins, and Espressif documentation identifies ESP32-P4 GPIO33 as an IO pin. Espressif's ESP-IDF GPIO documentation says ESP32-P4 has GPIO0-GPIO54, and peripheral input signals can be routed through GPIO matrix/IO MUX. The same documentation flags GPIO34-GPIO38 as strapping pins and GPIO24/GPIO25 as USB-JTAG by default.

Confidence level: medium for using the board label as logical GPIO33, low until confirmed against the exact board schematic.

## Official GPIO Reference Summary

Source references:

- Espressif ESP-IDF GPIO documentation for ESP32-P4: https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/gpio.html
- Espressif ESP32-P4 hardware design guidelines, strapping pins: https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32p4/schematic-checklist-esp32p4.html
- Espressif ESP32-P4 built-in JTAG documentation: https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-guides/jtag-debugging/configure-builtin-jtag.html

| GPIO Range | Official Notes | Project Use Tier | Rationale |
|---|---|---|---|
| GPIO0-GPIO1 | LP GPIO | caution | Usable as GPIO, but LP-domain role should be noted. |
| GPIO2-GPIO15 | LP GPIO, touch-capable | caution | Usable as GPIO, but touch/LP roles should be noted. |
| GPIO16-GPIO23 | ADC1-capable | caution | Usable as GPIO, but analog ADC role should be noted. |
| GPIO24-GPIO25 | USB-JTAG by default | avoid for now | We are using USB-Serial/JTAG for flashing/control; reusing these can break the lab link. |
| GPIO26-GPIO33 | General GPIO in official summary | preferred initial candidates | No official strapping/USB/analog warning in the GPIO summary. Board exposure still must be confirmed. |
| GPIO34-GPIO38 | Strapping pins | avoid for now | Can affect boot mode or startup state. Do not use until required and understood. |
| GPIO39-GPIO48 | General GPIO in official summary | preferred initial candidates | No official strapping/USB/analog warning in the GPIO summary. Board exposure still must be confirmed. |
| GPIO49-GPIO54 | ADC2/comparator-capable | caution | Usable as GPIO, but analog/comparator role should be noted. |

Project policy:

- Prefer `GPIO26-GPIO33` and `GPIO39-GPIO48` for early input-only LCD bus experiments.
- Avoid `GPIO24-GPIO25` while USB-Serial/JTAG is the control path.
- Avoid `GPIO34-GPIO38` during early work because they are strapping pins.
- Treat board header labels as logical GPIO numbers until the exact board schematic says otherwise.
- Confirm every candidate with firmware input-only tests before adding it to `pinmap_gbc.h`.

## Board Inventory

| Board Label | ESP32-P4 Logical GPIO | Status | Allowed Firmware Use | Notes |
|---|---:|---|---|---|
| GPIO7 | 7 | temporary G5 test input | `READ_GPIO 7`, `COUNT_GPIO_EDGES 7 <ms>` input-only tests | Temporary wiring: display pin 25 `G5 -> GPIO7`; caution tier due LP/touch-capable GPIO |
| GPIO8 | 8 | temporary G4 test input | `READ_GPIO 8`, `COUNT_GPIO_EDGES 8 <ms>` input-only tests | Temporary wiring: display pin 24 `G4 -> GPIO8`; caution tier due LP/touch-capable GPIO |
| GPIO9 | 9 | temporary G3 test input | `READ_GPIO 9`, `COUNT_GPIO_EDGES 9 <ms>` input-only tests | Temporary wiring: display pin 23 `G3 -> GPIO9`; caution tier due LP/touch-capable GPIO |
| GPIO10 | 10 | temporary G2 test input | `READ_GPIO 10`, `COUNT_GPIO_EDGES 10 <ms>` input-only tests | Temporary wiring: display pin 22 `G2 -> GPIO10`; caution tier due LP/touch-capable GPIO |
| GPIO11 | 11 | temporary G1 test input | `READ_GPIO 11`, `COUNT_GPIO_EDGES 11 <ms>` input-only tests | Temporary wiring: display pin 21 `G1 -> GPIO11`; caution tier due LP/touch-capable GPIO |
| GPIO12 | 12 | temporary G0 test input | `READ_GPIO 12`, `COUNT_GPIO_EDGES 12 <ms>` input-only tests | Temporary wiring: display pin 20 `G0 -> GPIO12`; caution tier due LP/touch-capable GPIO |
| GPIO13 | 13 | temporary R5 test input | `READ_GPIO 13`, `COUNT_GPIO_EDGES 13 <ms>` input-only tests | Temporary wiring: display pin 19 `R5 -> GPIO13`; caution tier due LP/touch-capable GPIO |
| GPIO14 | 14 | temporary R4 test input | `READ_GPIO 14`, `COUNT_GPIO_EDGES 14 <ms>` input-only tests | Temporary wiring: display pin 18 `R4 -> GPIO14`; caution tier due LP/touch-capable GPIO |
| GPIO15 | 15 | temporary R3 test input | `READ_GPIO 15`, `COUNT_GPIO_EDGES 15 <ms>` input-only tests | Temporary wiring: display pin 17 `R3 -> GPIO15`; caution tier due LP/touch-capable GPIO |
| GPIO16 | 16 | temporary R2 test input | `READ_GPIO 16`, `COUNT_GPIO_EDGES 16 <ms>` input-only tests | Temporary wiring: display pin 16 `R2 -> GPIO16`; caution tier due ADC1-capable GPIO |
| GPIO17 | 17 | temporary R1 test input | `READ_GPIO 17`, `COUNT_GPIO_EDGES 17 <ms>` input-only tests | Temporary wiring: display pin 15 `R1 -> GPIO17`; caution tier due ADC1-capable GPIO |
| GPIO18 | 18 | temporary R0 test input | `READ_GPIO 18`, `COUNT_GPIO_EDGES 18 <ms>` input-only tests | Temporary wiring: display pin 14 `R0 -> GPIO18`; caution tier due ADC1-capable GPIO |
| GPIO19 | 19 | temporary SPL test input | `READ_GPIO 19`, `COUNT_GPIO_EDGES 19 <ms>` input-only tests | Temporary wiring: display pin 13 `SPL -> GPIO19`; not assigned to GBC capture pin map |
| GPIO20 | 20 | temporary PS test input | `READ_GPIO 20`, `COUNT_GPIO_EDGES 20 <ms>` input-only tests | Temporary wiring: GBC pin 34 `PS -> GPIO20`; not assigned to GBC capture pin map |
| GPIO21 | 21 | temporary LP test input | `READ_GPIO 21`, `COUNT_GPIO_EDGES 21 <ms>` input-only tests | Temporary wiring: GBC pin 35 `LP -> GPIO21`; not assigned to GBC capture pin map |
| GPIO22 | 22 | temporary DCLK test input | `READ_GPIO 22`, `COUNT_GPIO_EDGES 22 <ms>` input-only tests | Temporary wiring: GBC pin 36 `DCLK -> GPIO22`; not assigned to GBC capture pin map |
| GPIO3 | 3 | temporary CLS test input | `READ_GPIO 3`, `COUNT_GPIO_EDGES 3 <ms>` input-only tests | Temporary wiring: display pin 7 `CLS -> GPIO3`; moved from GPIO32 after GPIO32 was suspected of backfeeding the target |
| GPIO33 | 33 | temporary SPS test input | `READ_GPIO 33`, `COUNT_GPIO_EDGES 33 <ms>` input-only tests | Temporary wiring: display pin 6 `SPS -> GPIO33`; not assigned to GBC capture pin map |

## 3. Unknowns

- Exact development board model.
- Whether board GPIO33 has any onboard circuit, pull, peripheral, or boot interaction.
- Header voltage domain for GPIO33.
- Whether GPIO33 is convenient for later DVP/LCD_CAM capture routing.
- Which preferred candidate GPIOs are physically exposed on the user's board.
- Whether ESP-IDF `gpio_dump_io_configuration()` reports any board-reserved or already-routed pins at runtime.

## 4. Experiment Results

2026-05-06: GPIO33 added as a Phase 1 test input allowlist entry. It is not part of the GBC capture pin map and does not change `capture_pin_count`.

2026-05-06: `READ_GPIO 33` verified over USB-Serial/JTAG. Result was `level=1` with input-only mode and pulls disabled. `READ_GPIO 32` was rejected because it is not allowlisted.

2026-05-06: User selected GPIO33 for a temporary input test initially thought to be `SPL`. Later pinout correction identifies this signal as `SPS`.

2026-05-06: `READ_GPIO 33` during temporary `SPL` test returned level `1`.

2026-05-06: Added `COUNT_GPIO_EDGES 33 <duration_ms>` for input-only edge counting on temporary timing-signal wiring.

2026-05-06: `COUNT_GPIO_EDGES 33 1000` returned rising_edges=4029 and falling_edges=2622. Power state must be controlled and recorded before interpreting this as SPL timing.

2026-05-06: With GBC ON, GPIO33 static level read `0`. Edge counts on the signal later corrected to `SPS -> GPIO33` were stable around 8658 falling edges per second over 1 s and 5 s windows.

2026-05-07: Added official-doc-derived GPIO candidate tiers. GPIO33 falls in the preferred initial candidate group `GPIO26-GPIO33`.

2026-05-07: User reported additional temporary wiring: display pin 36 `DCLK -> GPIO22`, display pin 35 `LP -> GPIO21`, and display pin 34 `PS -> GPIO20`. These are recorded as input-only test mappings, not capture pin-map assignments. GPIO20-GPIO22 are in the caution tier because they are ADC1-capable, but are usable as digital inputs for controlled tests.

2026-05-07: Initial input-only tests on `/dev/cu.usbmodem14401`: GPIO20, GPIO21, and GPIO22 all read level `0` and all reported zero edges over 1000 ms.

2026-05-07: Full user pinout correction added: `SPL -> GPIO19`, `CLS -> GPIO32`, and `SPS -> GPIO33`. GPIO19 and GPIO32 added to input-only test allowlist. Previous `SPL -> GPIO33` notes should be interpreted as `SPS -> GPIO33`.

2026-05-07: Updated allowlist firmware built and flashed. Input-only baseline with GBC ON:

| GPIO | Signal | Static Level | Rising Edges / 1000 ms | Falling Edges / 1000 ms |
|---:|---|---:|---:|---:|
| 19 | SPL | 0 | 0 | 8656 |
| 20 | PS | 1 | 9195 | 9196 |
| 21 | LP | 0 | 0 | 9196 |
| 22 | DCLK | 1 | 46003 | 57970 |
| 32 | CLS | 1 | 9196 | 9195 |
| 33 | SPS | 1 | 60 | 60 |

GPIO22/DCLK is active, but GPIO interrupt edge counting is not a valid MHz-rate clock measurement.

2026-05-07: PCNT-backed rising-edge measurements for the same temporary inputs:

| GPIO | Signal | Duration | Rising Edges | Rising Edge Hz |
|---:|---|---:|---:|---:|
| 19 | SPL | 1000 ms | 8659 | 8659 |
| 20 | PS | 1000 ms | 9197 | 9197 |
| 21 | LP | 1000 ms | 9197 | 9197 |
| 22 | DCLK | 1000 ms | 1395226 | 1395226 |
| 32 | CLS | 1000 ms | 9197 | 9197 |
| 33 | SPS | 1000 ms | 60 | 60 |

The PCNT path is more appropriate for GPIO22 than the ISR edge counter, but the result still requires independent confirmation because it contradicts the initial 6-8 MHz expectation.

2026-05-08: User connected all six red data bits: `R5 -> GPIO13`, `R4 -> GPIO14`, `R3 -> GPIO15`, `R2 -> GPIO16`, `R1 -> GPIO17`, and `R0 -> GPIO18`. Added these GPIOs to the Phase 1 input-only test allowlist. They remain temporary test inputs, not capture pin-map assignments.

2026-05-08: User corrected green wiring to ESP32-P4 GPIO range `12:7`. Current mapping: `G0 -> GPIO12`, `G1 -> GPIO11`, `G2 -> GPIO10`, `G3 -> GPIO9`, `G4 -> GPIO8`, and `G5 -> GPIO7`. GPIO7-GPIO12 are in the Phase 1 input-only test allowlist.

2026-05-08: User connected all six blue data bits: `B5 -> GPIO50`, `B4 -> GPIO48`, `B3 -> GPIO47`, `B2 -> GPIO46`, `B1 -> GPIO45`, and `B0 -> GPIO36`. Added these GPIOs to the input-only test allowlist. GPIO36 remains higher risk because earlier notes avoid GPIO34-GPIO38 as strapping pins; keep it input-only and do not drive it.

2026-05-08: Red data input-only activity test:

| GPIO | Signal | Static Level | Rising Edges / 1000 ms | Falling Edges / 1000 ms |
|---:|---|---:|---:|---:|
| 13 | R5 | 1 | 15964 | 5981 |
| 14 | R4 | 1 | 15904 | 6138 |
| 15 | R3 | 1 | 15742 | 6220 |
| 16 | R2 | 1 | 15639 | 6490 |
| 17 | R1 | 1 | 15625 | 6585 |
| 18 | R0 | 1 | 0 | 0 |

R0/GPIO18 is static high in this activity test and needs synchronized sampling or alternate screen content before interpreting it as a wiring fault.

## 5. Next Steps

- Verify the exact board model and schematic.
- Run edge counts for GPIO19/SPL, GPIO20/PS, GPIO21/LP, GPIO22/DCLK, GPIO32/CLS, and GPIO33/SPS with controlled GBC state.
- Do not promote temporary mappings into `pinmap_gbc.h` until measurements validate voltage safety and timing purpose.
- Add all physically exposed board GPIOs to the board inventory table before mapping more GBC signals.
- Test GPIO13-GPIO18 red data lines with `READ_GPIO` and `COUNT_GPIO_EDGES`.
