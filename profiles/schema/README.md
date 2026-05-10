# Profile Schema

## 1. Objective

Define schema documentation for target profiles.

This matters because target profiles are the boundary between reusable lab machinery and console-specific knowledge.

## 2. Current Understanding

The first active profile is `profiles/gbc_lcd.json` with `schema_version` `0.1`.

The schema should describe:

- target identity
- source documents
- safety and dangerous rails
- connector pin metadata
- timing/control signals
- pixel bus mapping
- analog/power pins
- capture presets
- decode hypotheses
- known concerns and open questions

Confidence level: medium. The current profile is useful, but the schema should stay provisional until a second target or significantly different capture mode is added.

## 3. Unknowns

- Whether the schema should be enforced with JSON Schema, Python validation, or both.
- Whether firmware headers should be generated from profile data.
- How to represent analog video targets later.

## 4. Experiment Results

2026-05-10: Schema directory added as part of maintenance Phase B.

## 5. Next Steps

- Add a draft JSON Schema for the current GBC profile fields.
- Add a validator that checks dangerous rails, duplicate GPIO mappings, missing evidence, and preset compatibility.
