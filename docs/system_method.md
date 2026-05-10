# Signal Lab Method And System Model

## 1. Objective

Define the repeatable method every target project should follow when using the ESP32-P4 signal lab.

This matters because the same physical ESP32-P4 board should serve two roles:

- an investigation instrument controlled from the computer and AI-assisted tools
- a future implementation device for products such as display mods, retimers, scalers, bridges, recorders, and analyzers

The method should shape the UI, firmware commands, host tools, profile schema, capture artifacts, and future product architecture.

## 2. Current Understanding

Current hypothesis: every project can be modeled as a pipeline:

```text
Source bus
  -> ESP32-P4 input/capture
  -> ESP32-P4 processing blocks
  -> ESP32-P4 output/transport
  -> Destination

Computer + AI workbench
  <-> ESP32-P4 control and telemetry
  <-> profiles, captures, manifests, decoded images, reports
```

In this model, the console display bus is the source. The ESP32-P4 is not only a passive capture board; it is the exact processing platform that can later become part of the finished mod. The computer, browser UI, and AI tools operate as the investigation layer around that platform.

Confidence level: high that this is the correct architecture direction; medium for exact module APIs until a second source or destination is implemented.

## 3. Unknowns

- How many source bus types the first ESP32-P4 hardware fixture should support.
- Whether output destinations should be developed before source capture is fully stable.
- Which processing blocks must run in real time on the ESP32-P4 versus offline on the host during investigation.
- How much AI should directly control hardware versus propose commands through the workbench safety layer.
- Whether target profiles should generate firmware configuration, host presets, UI panels, or all three.

## 4. Experiment Results

The GBC LCD bus investigation validates the model:

- Source discovery required electrical safety, pin mapping, timing measurement, raw capture, and hypothesis decoding.
- ESP32-P4 processing was minimal at first: input-only GPIO, timing capture, LCD_CAM raw capture, binary frame transport, and safe-idle recovery.
- Destination was initially the computer/browser, not a replacement panel.
- The browser became an instrument panel for live view, controls, pin reads, timing captures, and recovery.
- AI assistance became useful only because captures, screenshots, docs, and profile data were preserved.
- The GPIO32 power-cycle issue proved that final-product hardware behavior and investigation behavior cannot be separated. The instrument has to reveal electrical interaction, not hide it.

## 5. Next Steps

- Structure the UI around this method instead of around one live viewer.
- Represent each target as a source profile.
- Represent each future display/output as a destination profile.
- Represent ESP32-P4 processing as reusable blocks that can be used in both investigation and product firmware.
- Add manifests that tie source, processing settings, destination assumptions, commands, and artifacts together.
- Gap-assess the current project against this system model before major refactors.

## Method Stages

| Stage | Goal | Main evidence |
|---|---|---|
| 0. Project setup | Define the source and safety assumptions before touching hardware. | source profile draft, references, connector notes |
| 1. Electrical safety | Decide what can be connected and how. | voltage table, dangerous rails, input-only verification |
| 2. Source activity discovery | Find active, static, clock-like, data-like, and unsafe lines. | GPIO levels, edge counts, clock measurements |
| 3. Timing and role classification | Classify relationships without assuming standard video names. | timing captures, VCD, relationship reports |
| 4. Raw source capture | Capture bytes/samples before final interpretation. | raw captures, overflow stats, sample-edge notes |
| 5. Hypothesis decoding | Test many interpretations of the same raw data. | PNGs, contact sheets, compare reports |
| 6. Live source monitor | Promote a proven hypothesis into a live monitor. | live frames, fps, latency, recovery behavior |
| 7. ESP32-P4 processing blocks | Convert investigation code into reusable firmware blocks. | block telemetry, latency, throughput, debug taps |
| 8. Destination output | Drive another panel, protocol, file, or stream. | destination profile, test patterns, output timing |
| 9. Product mode | Freeze a source-processing-destination pipeline. | product profile, diagnostics, limits, recovery plan |

## Source Graduation Rule

Investigation code should not remain the permanent high-performance path after a source has enough evidence.

The method has two different firmware modes:

| Mode | Purpose | Typical implementation |
|---|---|---|
| Investigation mode | Discover pins, timing, roles, sample edge, blanking, and bit packing. | Generic commands, configurable captures, extra telemetry, host/Python hypothesis tools. |
| Source-driver mode | Capture a known source efficiently and repeatably. | Source-specific firmware module generated or configured from the source profile. |

A source can graduate from investigation mode to source-driver mode when:

- electrical safety is documented
- connected pins and dangerous rails are documented
- line/frame timing is stable enough to define a preset
- a recognizable image has been reconstructed or monitored live
- sample edge and bit packing have a current best hypothesis
- source-present/source-lost behavior is understood enough for recovery
- open unknowns are listed and do not block the next performance target

Graduation does not delete the investigation tools. It changes their role:

- investigation tools become regression tools and tools for new targets
- source-driver firmware becomes the performance path for the current target
- host/Python tools remain useful for validation, comparison, and artifact generation
- compatibility command names remain available while new optimized commands are added

This matches the prototype-to-C pattern: Python and generic capture prove the concept; firmware source drivers implement the known capture model for speed and determinism.

For the current GBC target, the next source-driver goal is:

```text
GBC LCD source profile
  -> persistent LCD_CAM/GDMA capture source driver
  -> compact binary native-USB stream
  -> browser live monitor near source frame rate
```

The old `LCDCAM_RAW_CAPTURE_BIN`/`LCDCAM_RAW_STREAM_BIN` commands remain valuable as diagnostic compatibility commands, but they should not define the final GBC high-FPS path if they require per-frame setup/teardown.

## Architecture Roles

### Source Profiles

Source profiles describe incoming signals.

Examples:

- `gbc_lcd`
- future handheld LCD bus
- console RGB bus
- digital panel link

Responsibilities:

- connector pinout
- dangerous rails
- voltage domain
- signal names
- GPIO mapping
- candidate roles
- source timing hypotheses
- source capture presets
- source decode hypotheses
- known anomalies
- graduation status: discovery, monitor, source-driver, or product-ready
- evidence links that justify promoted presets

### Source Drivers

Source drivers are firmware modules that implement a graduated source profile efficiently.

Responsibilities:

- configure GPIO matrix/peripherals for the source
- keep capture peripherals configured across frames when possible
- expose source-present/source-lost state
- produce framed binary data with explicit headers
- report dropped frames, DMA overruns, sync loss, and timing counters
- provide safe idle and electrical isolate behavior
- preserve enough debug taps to validate the source against investigation artifacts

Source drivers should be target-specific only where the evidence requires it. The GBC driver can know about the current `SPS`, `SPL`, `DCLK`, RGB565/RGB332 packing, and `161x145` stream-period hypothesis. It should still use reusable lower-level blocks for LCD_CAM/GDMA, USB transport, buffers, and telemetry.

### Processing Blocks

Processing blocks describe what the ESP32-P4 can do between input and output.

Candidate blocks:

- input sampling
- clock-domain crossing
- retiming
- line buffering
- frame buffering
- color conversion
- scaling
- cropping/windowing
- overlay/debug markers
- statistics and health monitoring
- USB streaming
- destination timing generation

Important rule: a block should be useful in both lab mode and product mode when possible.

Benchmark rule: every promoted processing block should have a no-source synthetic benchmark and a source-connected counter benchmark. The browser does not need to receive every frame to prove internal real-time behavior.

Current proof commands:

- `PIPELINE_BENCH`: synthetic generate/process/output-sink loop, no GPIO or capture peripheral.
- `GBC_SOURCE_BENCH`: real source capture counters, no full-frame USB payload.
- `GBC_PIPELINE_BENCH`: real source capture plus firmware processing and output-sink counters, no full-frame USB payload.

Current evidence: `PIPELINE_BENCH 300 160 144 2 60` completes at `60.124 fps` with zero drops and `max_frame_us=1833`; unthrottled synthetic processing is about `550.9 fps`. `GBC_SOURCE_BENCH 5 300 RGB565 0` captures real source frames at about `30.8 fps` on the current compatibility path. `GBC_PIPELINE_BENCH 5 300 RGB565 0 60` captures/processes/outputs real-source frames at about `28.1 fps`, with capture taking about `33.5 ms` and processing taking about `2.2 ms`.

Source-connected benchmarks should report capture/process/output FPS, target-FPS misses, dropped frames, overruns or sync loss when available, and the active performance path. If the path is still investigatory, the benchmark must say so explicitly.

Interpretation: the method must distinguish these limits:

- internal processing capacity
- real source capture capacity
- USB/browser monitoring capacity
- final destination output capacity

For product-mode confidence, the important proof is internal counters across source, processing, and destination stages. The browser is an instrument panel and preview tap, not the final performance authority.

### Destination Profiles

Destination profiles describe output targets.

Responsibilities:

- electrical output interface
- timing requirements
- color format
- framebuffer format
- scaling constraints
- initialization sequence
- test pattern support
- safe disable behavior

### Product Profiles

Product profiles bind one source, one or more processing blocks, and one destination into an implementation.

Examples:

- GBC LCD source -> scaler -> IPS RGB panel
- console digital video source -> recorder -> USB stream
- source bus -> retimer -> same bus output

Responsibilities:

- selected source profile
- selected destination profile
- processing chain
- fixed presets
- calibration values
- performance budget
- production diagnostics

## Computer And AI Control Model

The computer is the investigation controller. AI can help plan captures, compare artifacts, propose hypotheses, and summarize evidence, but hardware control should pass through the same safety-aware workbench APIs as human actions.

Control layers:

```text
Browser UI / AI agent / CLI
  -> host lab APIs
  -> serial/binary transport
  -> ESP32-P4 firmware commands
  -> safe GPIO/peripheral operations
```

Required safeguards:

- target profile allowlists
- no arbitrary GPIO output during investigation
- safe-idle and electrical-isolate commands
- explicit source-present/source-lost state
- artifact creation for important actions
- profile update proposals instead of silent rewrites

AI-facing artifacts should include:

- active source profile
- active destination profile, if any
- processing chain
- command history
- raw captures
- decoded images
- timing reports
- manifests
- screenshots/contact sheets
- open questions
- confidence notes

## UI Structure From The Method

The browser should be organized by the pipeline and investigation stage:

| UI area | Purpose |
|---|---|
| Project | Select project, source, processing chain, destination, and product mode. |
| Source | Safety, pin map, activity discovery, timing, raw capture, source hypothesis. |
| Processing | ESP32-P4 blocks, debug taps, buffering, retiming, color conversion, scaling. |
| Destination | Panel/protocol profiles, output tests, timing generation, safe disable. |
| Live | Current source/destination monitor and telemetry. |
| Artifacts | Captures, manifests, images, VCD, reports, AI review packs. |
| Profile | Source/destination/product profile editor and proposed changes. |
| Logs | Command history, errors, recovery, firmware stats. |

Left side should show data, images, timelines, reports, and live monitors.
Right side should show parameters and actions for the selected stage.

## Design Principle

The lab should avoid a split between "development instrument" and "final device" too early. The ESP32-P4 should expose the same capture, processing, telemetry, and recovery blocks during investigation that later become the implementation building blocks.
