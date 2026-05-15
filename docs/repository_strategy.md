# Repository Strategy

Purpose: define how the generic lab and concrete console projects should coexist across GitHub repositories.

Status: planning reference.

Last updated: 2026-05-15.

## Objective

Keep the ESP32-P4 lab reusable while allowing concrete projects, starting with the Game Boy Color screen mod, to move fast without contaminating the generic tool with target-specific assumptions.

## Current Understanding

Known repositories:

| Repository | Role |
|---|---|
| `yislennierm/esp32_mod_lab` | Generic lab, browser workbench, ESP32-P4 SDK inventory, graph editor, reusable instrumentation, reusable host tooling. |
| `yislennierm/esp32p4_gbc_screen_mod` | Concrete GBC screen-mod project built from evidence and reusable lab blocks. |

Confidence level: high for the conceptual split, medium for the exact filesystem/export mechanics.

## Unknowns

- Whether project repos should vendor generated firmware from the lab, consume a package, or carry a thin project descriptor that the lab builds.
- Whether shared firmware blocks should live in the lab repo, a separate reusable library repo, or ESP-IDF components copied into each project.
- How much captured evidence should remain in the lab repo versus project-specific repos.
- Whether SDK inventories should be committed snapshots, regenerated local artifacts, or both.

## Experiment Results

As of this checkpoint, the lab repo already contains project descriptors under `projects/` and profiles under `profiles/`.

The GBC work has proven that the lab needs all three layers:

- `lab`: discover, capture, visualize, inventory SDK examples, edit graphs, and test hardware resources.
- `project`: preserve target-specific wiring, timing, and production intent.
- `implementation`: deploy focused firmware such as GBC source capture to SPI LCD mirror, without the browser lab overhead.

The current split is not fully enforced. Some GBC assumptions still exist inside lab-era files and should be migrated behind project/profile selection over time.

## Next Steps

1. Keep `esp32_mod_lab` as the generic workbench repo.
2. Keep `esp32p4_gbc_screen_mod` as the GBC project repo.
3. Define an explicit project package/export format:
   - project JSON
   - source profile
   - destination profile
   - pin map
   - firmware target descriptor
   - evidence notes
4. Add a lab command/UI flow to open, import, or export a project package.
5. Make GBC-specific UI labels and defaults come from the active project descriptor instead of hardcoded frontend/backend constants.
6. Keep imported ESP-IDF examples read-only; store edits as lab overlays until the user explicitly generates or patches project code.
