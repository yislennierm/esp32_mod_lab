# Target Profiles

Target profiles describe one console picture-signal interface without making the whole project console-specific.

The profile files are intentionally data-first. Firmware and host tools should use them to learn wiring, signal roles, safety limits, capture presets, and decode hypotheses. Target-specific assumptions belong here and in `docs/`, not inside generic transport, capture, or viewer code.

## Current Profiles

- `gbc_lcd.json` - Game Boy Color LCD bus, the first proof target.

## Schema Status

The schema is provisional. Keep additions backward-compatible where possible and document breaking changes in `docs/universal_signal_lab.md`.

Draft schema files live in `profiles/schema/`:

- `profiles/schema/target_profile.schema.json` - permissive JSON Schema draft for current target profile fields.
- `profiles/schema/README.md` - schema rationale, unknowns, and next steps.

Expected profile responsibilities:

- target identity and connector notes
- dangerous rails and do-not-connect signals
- current ESP32-P4 wiring
- candidate signal roles
- capture presets
- decode hypotheses
- evidence, confidence, and open questions

Do not make the schema strict enough to block the active GBC profile until a validator and migration path exist.

Current lightweight validation:

```sh
python host/tools/validate_profile.py profiles/gbc_lcd.json
```
