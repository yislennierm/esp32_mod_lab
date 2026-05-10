# Future Work

## 1. Objective

Track long-term features that should remain out of scope until earlier reverse engineering phases are complete.

This matters because premature display output work could obscure unresolved timing and electrical questions.

## 2. Current Understanding

Current hypothesis: future capabilities may include additional console target profiles, IPS output, HDMI output, integer scaling, scanline simulation, USB streaming, framebuffer recording, emulator-assisted debugging, shader effects, FPGA assist mode, automatic timing discovery, AI-assisted anomaly detection, and live protocol visualization.

Evidence: these are listed in `PROJECT_CHARTER.md`.

Confidence level: medium as long-term goals, low for priority and feasibility.

## 3. Unknowns

- Which output panel or interface will be targeted.
- Whether ESP32-P4 bandwidth is sufficient for real-time capture plus output.
- Required scaling, color conversion, and buffering strategy.
- Whether external hardware assistance is needed.
- Which target profile format should be used for consoles beyond GBC.
- How to preserve compatibility with GBC-specific scripts while generalizing the platform.

## 4. Experiment Results

2026-05-09: Added the universal signal-lab direction. Future work now explicitly includes target profiles beyond GBC and AI-assisted picture-signal analysis.

2026-05-09: Added the maintenance and modularization plan. Future product features should be built from reusable ESP32-P4 pipeline blocks rather than from GBC-only scripts: safe GPIO isolation, pin inspection, activity scanning, clock measurement, timing capture, raw LCD_CAM capture, hypothesis decoding, artifact manifests, and browser workbench controls.

## 5. Next Steps

- Defer output work until offline frame reconstruction is stable.
- Revisit this document after Phase 4 success.
- Define the first reusable target profile schema using the GBC LCD bus as the reference case.
- Identify a second target console or display bus to validate that the architecture is not overfitted to GBC.
- Classify current host scripts into stable tools, GBC target tools, experiments, and obsolete prototypes before moving files.
- Add compatibility wrappers before renaming or relocating working GBC scripts.
- Define the boundary between target modules and product modules such as retimers, screen mods, scalers, recorders, and protocol analyzers.
