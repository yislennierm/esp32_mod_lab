# GBC LCD Pinout

## 1. Objective

Track known, suspected, and measured Game Boy Color LCD connector pins.

This matters because incorrect pin assumptions can damage the ESP32-P4 or produce misleading captures.

## 2. Current Understanding

Current hypothesis: the LCD bus includes RGB666-style digital color lines and timing/control signals such as DCLK, LP, SPL, SPS, CLS, and MOD.

Evidence: this is listed as a project hypothesis in `PROJECT_CHARTER.md`.

Confidence level: low until measured on hardware and cross-checked against reliable references.

## 3. Unknowns

- Exact pin ordering.
- Logic voltage levels for each candidate digital signal.
- Which lines are analog LCD rails and must never connect to ESP32-P4 GPIO.
- Whether any digital lines require level shifting.

## 4. Experiment Results

No pin measurements have been recorded yet.

2026-05-06: Temporary test wiring was initially recorded as `SPL -> ESP32-P4 GPIO33`, but the later full pinout table corrected this to `SPS -> GPIO33` and `SPL -> GPIO19`.

2026-05-08: User-reported display bus pinout and current ESP32-P4 wiring:

| GBC Pin | Signal | ESP32-P4 GPIO | Status |
|---:|---|---:|---|
| 49 | V9 |  | analog rail - do not connect to GPIO |
| 48 | V8 |  | analog rail - do not connect to GPIO |
| 47 | V7 |  | analog rail - do not connect to GPIO |
| 46 | V6 |  | analog rail - do not connect to GPIO |
| 45 | V5 |  | analog rail - do not connect to GPIO |
| 44 | V4 |  | analog rail - do not connect to GPIO |
| 43 | V3 |  | analog rail - do not connect to GPIO |
| 42 | V2 |  | analog rail - do not connect to GPIO |
| 41 | V1 |  | analog rail - do not connect to GPIO |
| 40 | V0 |  | analog rail - do not connect to GPIO |
| 39 | VSHA |  | analog rail/reference - do not connect to GPIO |
| 38 | TST3 |  | ignore initially |
| 37 | TST2 |  | ignore initially |
| 36 | DCLK | 22 | temporary input-only test wiring |
| 35 | LP | 21 | temporary input-only test wiring |
| 34 | PS | 20 | temporary input-only test wiring |
| 33 | DGND |  | common ground only |
| 32 | VSHD |  | analog rail/reference - do not connect to GPIO |
| 31 | B5 | 50 | temporary input-only blue data wiring |
| 30 | B4 | 48 | temporary input-only blue data wiring |
| 29 | B3 | 47 | temporary input-only blue data wiring |
| 28 | B2 | 46 | temporary input-only blue data wiring |
| 27 | B1 | 45 | temporary input-only blue data wiring |
| 26 | B0 | 36 | temporary input-only blue data wiring |
| 25 | G5 | 7 | temporary input-only green data wiring |
| 24 | G4 | 8 | temporary input-only green data wiring |
| 23 | G3 | 9 | temporary input-only green data wiring |
| 22 | G2 | 10 | temporary input-only green data wiring |
| 21 | G1 | 11 | temporary input-only green data wiring |
| 20 | G0 | 12 | temporary input-only green data wiring |
| 19 | R5 | 13 | temporary input-only red data wiring |
| 18 | R4 | 14 | temporary input-only red data wiring |
| 17 | R3 | 15 | temporary input-only red data wiring |
| 16 | R2 | 16 | temporary input-only red data wiring |
| 15 | R1 | 17 | temporary input-only red data wiring |
| 14 | R0 | 18 | temporary input-only red data wiring |
| 13 | SPL | 19 | temporary input-only test wiring |
| 12 | VCOM |  | analog 5 Vpp - do not connect to GPIO |
| 11 | VCOM |  | analog 5 Vpp - do not connect to GPIO |
| 10 | VEE |  | analog -5 Vpp - do not connect to GPIO |
| 9 | VEE |  | analog -5 Vpp - do not connect to GPIO |
| 8 | VSS |  | ground |
| 7 | CLS | 3 | temporary input-only test wiring; moved from GPIO32 after GPIO32 was suspected of causing backfeed |
| 6 | SPS | 33 | temporary input-only test wiring |
| 5 | TEST1 |  | ignore initially |
| 4 | MOD |  | unconnected |
| 3 | MOD |  | unconnected |
| 2 | VCC |  | power rail - do not connect to GPIO |
| 1 | VDD |  | power rail - do not connect to GPIO |

## 5. Next Steps

- Identify connector pins without connecting to ESP32-P4 GPIO.
- Measure HIGH and LOW voltage levels for candidate digital lines.
- Mark V0-V9, VCOM, VEE, VSHA, and VSHD as dangerous rails unless proven otherwise.
- Validate or reject temporary `DCLK -> GPIO22`, `LP -> GPIO21`, `PS -> GPIO20`, `SPL -> GPIO19`, `CLS -> GPIO3`, and `SPS -> GPIO33` mappings after input-only tests and voltage measurements.
- Validate or reject temporary red data mappings `R5 -> GPIO13`, `R4 -> GPIO14`, `R3 -> GPIO15`, `R2 -> GPIO16`, `R1 -> GPIO17`, and `R0 -> GPIO18` with input-only tests before RGB capture.
- Validate or reject temporary green data mappings `G5 -> GPIO7`, `G4 -> GPIO8`, `G3 -> GPIO9`, `G2 -> GPIO10`, `G1 -> GPIO11`, and `G0 -> GPIO12` with input-only tests before RGB capture.
- Validate or reject temporary blue data mappings `B5 -> GPIO50`, `B4 -> GPIO48`, `B3 -> GPIO47`, `B2 -> GPIO46`, `B1 -> GPIO45`, and `B0 -> GPIO36` with input-only tests before full-color capture. GPIO36 is in the previously avoided strapping range, so it should be treated as higher risk and kept input-only.
