# Host Lab Library

## 1. Objective

Define the future home for reusable host-side APIs used by the ESP32-P4 signal lab.

This matters because the current host scripts proved useful during the GBC investigation, but many contain repeated serial, artifact, decode, and profile-handling behavior. Those reusable pieces should eventually live here.

## 2. Current Understanding

Current hypothesis: `host/lab/` should become a small Python package for generic lab operations.

Candidate modules:

- `serial_transport` - command/response and binary frame transport.
- `profiles` - target profile loading, validation, and safe GPIO extraction.
- `artifacts` - timestamped experiment folders, manifests, checksums, and thumbnails.
- `timing` - timing-edge parsing, relationship reports, and VCD helpers.
- `frame_decode` - generic crop, stride, skew, bit order, inversion, and color packing helpers.
- `lcdcam` - host-side wrappers for LCD_CAM raw capture commands.

Confidence level: medium. The GBC workflow shows these boundaries, but they should be extracted incrementally after compatibility wrappers exist.

## 3. Unknowns

- Whether this should become an installable Python package.
- Whether command schemas should be generated from firmware metadata.
- How much target-specific decode logic belongs in `host/lab/` versus `host/targets/<target>/`.
- How much of the browser workbench server should import from this package.

## 4. Experiment Results

2026-05-10: Directory added as part of the non-destructive maintenance phase. No working scripts were moved.

## 5. Next Steps

- Extract profile loading from the browser and capture tools first.
- Extract artifact manifest writing before changing capture directory layout.
- Keep old script names working while new library code is introduced.
- Add tests around extracted code before deleting duplicated helpers.
