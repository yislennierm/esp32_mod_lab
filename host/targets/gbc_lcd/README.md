# GBC LCD Host Target

## 1. Objective

Define where Game Boy Color LCD-specific host decode presets, compatibility wrappers, and analysis notes should live.

This matters because the GBC LCD bus is the first target module, but the host tooling should become reusable for other console picture buses.

## 2. Current Understanding

GBC-specific host knowledge currently includes:

- `160x144` visible display expectations.
- Current useful `161x145` stream-period model.
- RGB332/RGB565-style live capture presets.
- Historical red-only and red/green line-burst decoders.
- GBC boot/no-cartridge visual expectations.
- GPIO32 avoidance and `CLS -> GPIO3` wiring.

Confidence level: high that these details are target-specific.

## 3. Unknowns

- Final color packing from the source.
- Whether the extra transfer byte/line is true blanking, porch-like behavior, dummy transfer, or capture framing.
- Which old one-off scripts should become target utilities versus archived experiments.

## 4. Experiment Results

2026-05-10: Directory added as an index only. Existing GBC scripts remain at top-level `host/` until wrappers are added.

## 5. Next Steps

- Move GBC decode presets here only after `decode_lcdcam_fast.py` can load target modules or profile presets.
- Add a compatibility command that reproduces the current working live viewer settings.
- Keep representative GBC images linked from `docs/gbc_lcd_journey.html`.
