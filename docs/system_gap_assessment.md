# System Gap Assessment

## 1. Objective

Assess the current project against the full source -> ESP32-P4 processing -> destination method.

This matters because the current GBC work is a strong source-capture milestone, but the whole system needs source profiles, processing blocks, destination profiles, product profiles, manifests, UI structure, and AI-facing evidence packs.

## 2. Current Understanding

Current project state:

- Strongest area: GBC source investigation and live source monitor.
- Emerging area: target profile, browser workbench, artifact preservation.
- Weakest areas: ESP32-P4 internal dataflow benchmarks, destination profiles, processing block architecture, product-mode configuration, formal manifests, automated hypothesis comparison.

Confidence level: high for this gap assessment based on current repo structure and docs.

## 3. Unknowns

- Whether the next milestone should prioritize source-capture robustness, UI reorganization, or destination output.
- Whether LCD_CAM raw capture is the long-term generic input backend or only the GBC backend.
- Which destination panel/protocol will be first.
- Whether hardware buffering/protection should be designed before more targets are connected.

## 4. Experiment Results

2026-05-10: Gap assessment created after defining `docs/system_method.md`.

## 5. Next Steps

- Use this assessment to choose the next development milestone.
- Keep the current GBC live capture path working while building the generic method around it.
- Close gaps by adding small compatibility-preserving blocks, not by rewriting the working path.

## Summary Matrix

| Area | Current Status | Gap | Priority |
|---|---|---|---|
| Source profile | Started with `profiles/gbc_lcd.json` | Needs stronger schema, versioning, evidence links, presets | High |
| Source driver | Not formalized; current GBC path still uses diagnostic LCD_CAM commands | Needs a graduated GBC source-driver module for high-FPS capture | High |
| Electrical safety | Good GBC lessons documented | Needs reusable checklist and hardware fixture guidance | High |
| Pin/activity discovery | Implemented in firmware/browser partially | Needs cleaner generic APIs and saved manifests | High |
| Timing classification | Implemented for GBC timing/control signals | Needs UI confidence scoring and compare views | High |
| Raw capture | Working with LCD_CAM for GBC | Needs backend abstraction and manifest format implementation | High |
| Hypothesis decoding | Many scripts and browser controls exist | Needs generic hypothesis engine and saved profiles | High |
| Live monitor | Working for GBC | Needs source-present/source-lost cleanup and preset management | High |
| ESP32-P4 internal dataflow | Partially proven with synthetic pipeline and production overlap | Needs persistent source capture, frame ring, PPA, DMA2D, and sink benchmarks | High |
| Processing blocks | Conceptual only | Needs firmware block interfaces and telemetry contracts | High |
| Destination profiles | Not started | Needs first destination selection and profile schema | Medium |
| Product profiles | Not started | Needs source-processing-destination binding model | Medium |
| Artifact manifests | Drafted only | Needs implementation in capture tools | High |
| AI review packs | Conceptual only | Needs generator from manifests and thumbnails | Medium |
| UI method alignment | Partially viewer/workbench-centered | Needs pipeline-stage navigation | High |
| Code organization | Skeleton directories added | Needs wrappers, host lab extraction, tests | High |

## Area Assessment

### Firmware

Current useful primitives:

- serial command interface
- GPIO read and edge counting
- safe idle / electrical isolation behavior
- timing-edge capture
- line-clock capture
- LCD_CAM raw capture
- binary frame transport
- core status and diagnostics

Gaps:

- firmware modules are still mostly flat under `firmware/main/`
- no formal processing block interface
- no graduated source-driver interface; current live mode still drives diagnostic capture commands
- no source/destination/product profile consumption in firmware
- no destination output subsystem
- no generic command metadata for UI generation

Recommended next firmware direction:

- keep existing command names as compatibility aliases
- introduce a GBC LCD source-driver module as the performance path after preserving the current live baseline
- add persistent LCD_CAM/GDMA source-ring benchmark before more visual output tuning
- add a common benchmark result structure for source, ring, processing, and sink tests
- benchmark PPA and DMA2D with synthetic frames before adding product transforms
- add generic command names where useful, for example `MEASURE_CLOCK`
- define a block status structure used by capture, processing, and future output blocks
- add explicit source-present/source-lost status for live capture
- add capture/USB timing counters before changing the frame transport again

### Host Tools

Current useful pieces:

- `gbc_probe.py`
- timing analyzers
- LCD_CAM capture scripts
- render/postprocess scripts
- PulseView VCD export
- live browser workbench
- profile validator

Gaps:

- shared serial/profile/artifact code is not yet extracted into `host/lab/`
- experiment scripts are not wrapped or moved
- capture artifacts do not all share a manifest
- hypothesis comparison is spread across scripts and filenames

Recommended next host direction:

- extract profile loading and GPIO allowlist helpers first
- implement manifest writer and use it in new captures
- add a generic hypothesis-runner that can produce contact sheets and reports
- keep old scripts working as wrappers or compatibility entry points

### Browser UI

Current useful pieces:

- live monitor
- start/stop/recover
- safety/profile display
- pin reads and timeline
- edge scans
- timing capture
- line-clock capture
- logs

Gaps:

- UI is still organized around current viewer implementation more than the full pipeline method
- source, processing, destination, product, artifacts, and profile concepts are not first-class navigation sections
- no formal artifact browser
- no profile-change proposal workflow
- no AI review-pack generator

Recommended next UI direction:

- reorganize navigation around the method:
  - Project
  - Source
  - Processing
  - Destination
  - Live
  - Artifacts
  - Profile
  - Logs
- keep the current GBC live view as the first `Live` module
- move existing `Safety`, `Pins`, and `Timing` behavior under `Source`
- add an `Artifacts` view before adding more capture features

### Profiles

Current useful pieces:

- `profiles/gbc_lcd.json`
- `profiles/schema/target_profile.schema.json`
- `host/tools/validate_profile.py`

Gaps:

- schema is permissive and not complete
- no destination profile schema
- no product profile schema
- no versioned capture presets with artifact evidence links
- no generated firmware pin header or host preset from profile

Recommended next profile direction:

- add `profiles/sources/`, `profiles/destinations/`, and `profiles/products/` only after compatibility paths are defined
- keep `profiles/gbc_lcd.json` working
- add a `capture_presets` validation rule
- add profile evidence links to key journey artifacts

## Proposed Development Roadmap

### Milestone 1: Method-Aligned UI Without Breaking Capture

Goal: make the browser reflect the system method while preserving current GBC live behavior.

Work:

- add top-level UI navigation for Project, Source, Processing, Destination, Live, Artifacts, Profile, Logs
- place existing live view under Live
- place existing safety/pins/timing tools under Source
- add placeholder Processing and Destination pages that show "not configured" and explain expected profiles
- add Artifacts page that lists recent capture folders and referenced reports

Exit criteria:

- current GBC live capture still works
- source tools still work
- user can see the high-level pipeline in the UI

Status: started 2026-05-10. The browser now has method-aligned tabs: Project, Source, Processing, Destination, Live, Artifacts, Profile, and Logs. Existing source tools and live monitor remain in the same server file and endpoint set. Processing and Destination are placeholders. Artifacts lists recent experiment folders.

### Milestone 2: Manifest Implementation

Goal: every new meaningful capture writes a common manifest.

Work:

- add `host/lab/artifacts.py`
- add manifest writer for timing captures, line-clock captures, and raw LCD_CAM captures
- include active source profile, command, settings, outputs, and observations

Exit criteria:

- new captures have a `manifest.json`
- browser artifacts page can read them

### Milestone 3: Profile And Preset Hardening

Goal: make the working GBC setup reproducible from profile data.

Work:

- add capture preset validation to `validate_profile.py`
- record current working GBC live preset in a structured way if not already complete
- add evidence links to profile
- add profile-change proposal output from UI actions

Exit criteria:

- validator catches duplicate GPIOs, dangerous assignments, missing presets, and historical GPIO32 mapping
- UI can show the active capture preset from profile

### Milestone 4: Host Lab Extraction

Goal: reduce duplicated code while preserving script names.

Work:

- extract profile loading
- extract serial transport wrapper
- extract artifact writer
- keep existing scripts as entry points

Exit criteria:

- old commands still work
- new reusable APIs exist under `host/lab/`

### Milestone 5: Processing Block Interface

Goal: define how ESP32-P4 middle blocks are represented.

Work:

- add docs for block API
- add firmware status structs for capture/processing/output blocks
- expose block status through command transport
- start with existing raw capture as `source_capture` block

Exit criteria:

- UI can show at least one real block status
- later retimer/scaler work has a defined place

### Milestone 6: First Destination Profile

Goal: begin output side without weakening source capture.

Work:

- choose first destination panel/protocol
- document electrical and timing requirements
- add destination profile draft
- add test-pattern output plan before source-to-panel bridge

Exit criteria:

- destination profile exists
- test-pattern strategy is documented
- no source capture regression

## Near-Term Recommendation

The next best engineering step is Milestone 1: reorganize the browser UI around the method while keeping every existing endpoint and GBC live behavior working.

Reason: the UI is the place where human, AI, ESP32-P4, profiles, and artifacts meet. If the UI reflects the source -> processing -> destination method, future features will naturally land in the right place instead of accumulating as unrelated debug buttons.
