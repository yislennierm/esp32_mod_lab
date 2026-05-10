# Artifact Manifest Format

## 1. Objective

Define the common manifest format future captures should write.

This matters because every useful experiment should leave behind enough metadata for a human, script, or AI agent to reproduce the interpretation later.

## 2. Current Understanding

Current captures already save many useful JSON, PNG, VCD, and Markdown artifacts, but the metadata is not yet uniform.

Future capture folders should contain a `manifest.json` with:

| Field | Meaning |
|---|---|
| `schema_version` | Manifest schema version. |
| `created_at_utc` | Timestamp for the capture session. |
| `target_profile` | Profile id and profile file path. |
| `firmware` | Firmware version, command names, and binary transport mode. |
| `hardware` | Physical wiring summary and electrical confidence. |
| `capture` | Command, duration, raw dimensions, byte count, sample edge, and marker assumptions. |
| `decode_hypothesis` | Width, height, crop, stride, skew, bit packing, inversion, and color operations. |
| `outputs` | Raw bytes, JSON reports, PNGs, VCD files, thumbnails, and checksums. |
| `observations` | Human notes, anomalies, failures, and confidence level. |

Confidence level: high that these fields are needed; medium for exact naming until implemented in `host/lab/artifacts`.

## 3. Unknowns

- Whether every capture command should create a folder or whether small commands should append to a session.
- How to version decode hypotheses separately from target profiles.
- How to handle live stream captures that generate many frames.

## 4. Experiment Results

2026-05-10: Manifest format drafted from the GBC journey. Existing captures are not rewritten.

## 5. Next Steps

- Add manifest writing to new capture paths first.
- Add a manifest index page or JSON catalog for browser browsing.
- Include representative thumbnails for fast visual review.
- Keep old capture folders valid even if they lack the new manifest fields.
