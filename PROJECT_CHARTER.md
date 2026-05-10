# Console Picture Signal Lab using ESP32-P4

# PROJECT MISSION

Create a production-quality reverse engineering and display bridge framework for console picture signals using an ESP32-P4 as a programmable capture and experimentation platform.

The Game Boy Color LCD bus is the first target profile and proof case. The architecture must remain useful for other consoles and display buses, including unknown or partially documented parallel video, LCD, DVP-like, sync-separated, sync-embedded, and driver-specific picture interfaces.

The system must:
- reverse engineer picture/video protocols safely
- capture timing and pixel/data signals
- reconstruct frames
- eventually bridge captured frames to modern displays where appropriate
- remain modular, testable, and fully documented

This project is NOT a quick prototype.

The goal is:
- reusable architecture
- deterministic behavior
- traceability
- reproducibility
- proper reverse engineering methodology
- maintainable firmware and tooling

The ESP32-P4 acts initially as a programmable signal lab:

USB <-> ESP32-P4 <-> target console picture bus

The ESP32-P4 is NOT initially a display bridge.

The initial goal is:
- passive observation
- timing analysis
- protocol discovery
- frame reconstruction

---

# CRITICAL CODING AND ENGINEERING RULES

Codex MUST follow these rules throughout the project.

## DO NOT:
- invent undocumented behavior
- assume timing semantics
- hardcode magic values without documenting them
- silently modify interfaces
- remove instrumentation/debugging without explanation
- optimize prematurely
- collapse architecture into monolithic files
- connect analog LCD rails to ESP32
- assume voltage compatibility
- assume standard HSYNC/VSYNC semantics
- assume RGB sample edge without evidence
- hardwire the architecture to one console's signal names
- treat a successful visual output as proof of protocol correctness

## ALWAYS:
- preserve previous functionality
- maintain backward compatibility where possible
- document every discovery
- document every assumption
- explain WHY changes are made
- keep modules isolated and testable
- prioritize observability and instrumentation
- log unknown behavior instead of hiding it
- create tools for inspection before optimization
- favor deterministic state machines over ad-hoc logic
- treat hardware as unsafe until verified
- separate reusable capture/analysis machinery from target-specific profiles
- document target-specific assumptions in target docs, not generic modules

---

# DOCUMENTATION REQUIREMENTS

Documentation quality is CRITICAL.

Every major change MUST update documentation.

Codex MUST maintain these files continuously:

```text
docs/
|-- architecture.md
|-- universal_signal_lab.md
|-- investigation_workbench.md
|-- gbc_lcd_pinout.md
|-- signal_hypotheses.md
|-- timing_notes.md
|-- experiment_log.md
|-- protocol_discoveries.md
|-- capture_pipeline.md
|-- framebuffer_format.md
|-- hardware_notes.md
|-- esp32p4_gpio_inventory.md
|-- risks_and_unknowns.md
|-- debugging_guide.md
`-- future_work.md
```

---

# REQUIRED DOCUMENTATION STYLE

Every document must contain:

## 1. Objective

Explain:
- what is being investigated
- why it matters

## 2. Current Understanding

Explain:
- current hypothesis
- evidence
- confidence level

## 3. Unknowns

Explicitly list:
- unresolved behavior
- timing ambiguity
- electrical uncertainty

## 4. Experiment Results

Document:
- measurements
- captures
- screenshots
- failures
- anomalies

## 5. Next Steps

Explain:
- what should be tested next
- what evidence is needed

---

# REVERSE ENGINEERING METHODOLOGY

The project must progress in strict phases.

DO NOT skip phases.

---

# PHASE 1 - ELECTRICAL SAFETY

Goals:
- identify safe digital lines
- measure voltage levels
- identify dangerous rails

Known dangerous rails:

V0-V9
VCOM
VEE
VSHA
VSHD

ESP32-P4 GPIOs are NOT 5V tolerant.

Before connecting:
- measure HIGH voltage
- measure LOW voltage
- verify clock amplitude
- use high impedance probes
- use level shifting if required

NO GPIO outputs allowed during this phase.

Firmware must configure ALL capture GPIOs as INPUT ONLY.

---

# PHASE 2 - TIMING DISCOVERY

Goals:
- identify pixel clock
- identify frame sync
- identify line sync
- determine valid sample edge
- identify blanking, porch-like, or dummy transfers
- identify whether apparent sync signals are real protocol boundaries or target-specific driver controls

For the GBC target profile, candidate signals:

DCLK
LP
SPL
SPS
CLS
MOD

Expected possibilities:

| Signal | Candidate Meaning |
|---|---|
| DCLK | pixel clock |
| LP | line pulse |
| SPL | horizontal start |
| SPS | frame start |
| MOD | polarity inversion |

Measurements MUST be logged.

---

# PHASE 3 - PARTIAL PIXEL/DATA CAPTURE

Start with a reduced target-specific bus. For the GBC target profile:

DCLK
LP
SPS
R5
G5
B5

Goals:
- confirm RGB synchronization
- determine valid edge
- determine blanking behavior

Then expand to:

R0-R5
G0-G5
B0-B5

---

# PHASE 4 - FRAME RECONSTRUCTION

Goal:
- reconstruct recognizable PNG images from captured traces

Workflow:

```text
target console picture bus
    |
ESP32-P4 capture firmware
    |
USB dump
    |
Python decoder
    |
PNG reconstruction
```

FIRST GBC TARGET SUCCESS CONDITION:

Reconstruct Game Boy boot logo as PNG.

Once achieved:
- timing is mostly solved
- real-time bridge becomes feasible

---

# PHASE 5 - REAL-TIME CAPTURE

Only after stable frame reconstruction:
- attempt DMA capture
- attempt continuous frame buffering
- measure throughput and latency

DO NOT optimize before successful offline reconstruction.

---

# PHASE 6 - IPS OUTPUT

Only after stable frame capture:
- add framebuffer conversion
- add scaling
- add RGB565/RGB666 output
- add IPS panel driver

---

# TARGET PROFILE 001: GAME BOY COLOR LCD BUS

The GBC LCD bus is the first target profile. It is allowed to have dedicated pinout, timing, and protocol documents. It must not force GBC-specific names or assumptions into generic firmware/host abstractions unless the abstraction is explicitly a target profile adapter.

Target-specific docs:

- `docs/gbc_lcd_pinout.md`
- GBC entries inside `docs/signal_hypotheses.md`
- GBC entries inside `docs/timing_notes.md`
- GBC entries inside `docs/protocol_discoveries.md`

## Expected GBC Bus Characteristics

Current hypothesis:

RGB666 parallel transport.

Signals:

R0-R5
G0-G5
B0-B5

Likely timing:
- DCLK = pixel clock
- SPS = frame start
- LP/SPL = line markers

This remains a hypothesis until experimentally verified.

---

# REQUIRED REPOSITORY STRUCTURE

```text
gbc-p4-probe/
|-- firmware/
|   |-- main/
|   |   |-- main.c
|   |   |-- usb_protocol.c
|   |   |-- gpio_sampler.c
|   |   |-- timing_analysis.c
|   |   |-- frame_capture.c
|   |   |-- signal_decoder.c
|   |   |-- ringbuffer.c
|   |   |-- diagnostics.c
|   |   `-- pinmap_gbc.h
|   |-- tests/
|   |-- CMakeLists.txt
|   `-- sdkconfig
|
|-- host/
|   |-- gbc_probe.py
|   |-- decode_trace.py
|   |-- reconstruct_frame.py
|   |-- hypotheses.py
|   |-- render_png.py
|   |-- analyze_timing.py
|   |-- compare_frames.py
|   `-- export_vcd.py
|
|-- captures/
|   |-- raw/
|   |-- decoded/
|   |-- screenshots/
|   `-- oscilloscope/
|
|-- profiles/
|   |-- README.md
|   `-- gbc_lcd.json
|
|-- docs/
|
|-- tools/
|
|-- scripts/
|
`-- codex_prompts/
```

---

# FIRMWARE DESIGN REQUIREMENTS

Firmware must be:
- modular
- instrumented
- deterministic
- debuggable

NO giant main.c logic.

Subsystems must be isolated.

Generic firmware modules should expose capture primitives and transport capabilities. Target-specific wiring, packing, and hypotheses should live in target profile modules such as `pinmap_gbc.h` or future profile-specific adapters.

---

# REQUIRED FIRMWARE CAPABILITIES

USB serial command interface:

PING
GET_VERSION
MEASURE_CLOCKS
CAPTURE_TIMING
CAPTURE_RAW
CAPTURE_FRAME
SET_TRIGGER
DUMP_BUFFER
EXPORT_STATS

Example:

```text
CAPTURE_TIMING 1000
```

Expected response:

```json
{
  "dclk_hz": 4194304,
  "lp_hz": 9198,
  "sps_hz": 59.7
}
```

---

# HOST TOOL REQUIREMENTS

Python host tools must:
- save raw traces
- generate reproducible outputs
- support hypothesis testing
- support automated decoding
- support PNG export
- support VCD export
- support timing analysis

The host side is the primary experimentation layer.

Firmware should remain generic.

Host tooling should separate:

- capture transport
- raw artifact storage
- target profile metadata
- hypothesis decoding
- visualization
- confidence/scoring

The same viewer and capture artifact model should be able to support future targets beyond GBC.

---

# HYPOTHESIS SYSTEM REQUIREMENTS

The decoder MUST support:
- multiple timing hypotheses
- configurable sample edge
- configurable line markers
- configurable frame markers
- configurable blanking assumptions

Examples:
- sample on rising edge
- sample on falling edge
- LP=line start
- SPL=line start
- SPS=frame start

Hypotheses must be target-profile aware. A GBC hypothesis may use `DCLK`, `SPL`, `LP`, and `SPS`; another console may use different names or sync semantics. Generic tools should refer to roles such as `sample_clock`, `line_marker_candidate`, `frame_marker_candidate`, `data_enable_candidate`, and `pixel_bus`.

Each hypothesis should produce:
- PNG output
- timing report
- confidence metrics

---

# OBSERVABILITY REQUIREMENTS

The system MUST maximize observability.

Always prefer:
- logging
- captures
- metrics
- traces
- screenshots
- exported artifacts

over hidden internal state.

Every experiment should be reproducible.

---

# DEBUGGING REQUIREMENTS

All critical firmware modules must support:
- verbose logging
- sanity checks
- timing diagnostics
- overflow detection
- dropped sample detection
- synchronization loss detection

---

# CODE QUALITY REQUIREMENTS

Code must:
- compile cleanly
- avoid unsafe memory usage
- avoid hidden globals
- avoid timing-sensitive hacks
- avoid undocumented constants

Every magic number must be documented.

---

# PERFORMANCE REQUIREMENTS

Correctness is more important than speed initially.

Priority order:

1. electrical safety
2. observability
3. correctness
4. determinism
5. maintainability
6. performance

---

# LONG-TERM GOALS

Potential future features:
- additional console target profiles
- universal picture-signal profile format
- AI-assisted capture triage and anomaly detection
- IPS replacement
- HDMI output
- integer scaling
- scanline simulation
- USB streaming
- framebuffer recording
- emulator-assisted debugging
- shader effects
- FPGA assist mode
- automatic timing discovery
- live protocol visualization

---

# IMPORTANT FINAL RULE

The project should evolve like a professional reverse engineering lab tool, not a hobby script collection.

Every stage must leave behind:
- reusable tooling
- reusable documentation
- reproducible experiments
- understandable architecture

The final result should be understandable and maintainable even years later.

---

# REFERENCE MATERIALS AND INTERNET SOURCES

Codex is encouraged to use the following official documentation and technical references when implementing firmware, host tooling, timing analysis, DMA capture, LCD interfaces, DVP capture, or RGB display output.

Always prefer official vendor documentation and stable APIs over random examples.

---

# ESP32-P4 OFFICIAL REFERENCES

## ESP-IDF Camera Controller Driver

Official ESP32-P4 camera/DVP/CSI capture APIs.

Reference:
https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/camera_driver.html

Important concepts:
- LCD_CAM DVP
- ISP DVP
- camera controller
- DMA frame capture
- PCLK
- VSYNC
- HSYNC/HREF

Potentially useful APIs:
- esp_cam_new_lcd_cam_ctlr()
- esp_cam_ctlr_receive()

This is likely the MOST important reference for bus capture.

---

## ESP-IDF LCD Driver Documentation

Official RGB LCD / display output APIs.

Reference:
https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/lcd/index.html

Important concepts:
- RGB LCD output
- framebuffer pipelines
- RGB timing
- DMA-backed display
- RGB565/RGB666 output

Potential future usage:
- IPS output
- framebuffer display
- scaling pipeline

---

## ESP32-P4 Datasheet

Reference:
https://www.espressif.com/sites/default/files/documentation/esp32-p4_datasheet_en.pdf

Important sections:
- GPIO electrical limits
- LCD_CAM peripheral
- DMA
- timing limits
- GPIO matrix
- memory architecture
- PSRAM bandwidth

Must be consulted before:
- pin assignment
- voltage assumptions
- timing assumptions
- DMA architecture decisions

---

## ESP-IDF SoC Capability Documentation

Reference:
https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/system/soc_caps.html

Useful for:
- conditional compilation
- hardware capability checks
- DMA capability detection
- peripheral support validation

---

# RELATED ESPRESSIF CAMERA REFERENCES

## Espressif DVP/MIPI Camera Overview

Reference:
https://docs.espressif.com/projects/esp-techpedia/en/latest/esp-friends/solution-introduction/camera/dvp-mipi-csi-camera-solution.html

Useful concepts:
- DVP timing
- camera pipelines
- frame synchronization
- DMA image capture
- RGB/YUV buses

Useful because the GBC bus may behave similarly to a DVP-style interface.

---

# LCD_CAM REGISTER REFERENCES

Useful low-level register reference:

https://docs.rs/esp32p4/latest/esp32p4/lcd_cam/index.html

Useful for:
- direct register work
- undocumented features
- low-level timing control
- DMA debugging

Only use low-level register access when necessary.

Prefer ESP-IDF drivers first.

---

# RELEVANT CONCEPTS TO STUDY

Codex should study and understand:
- DVP camera buses
- RGB parallel LCD buses
- LCD timing generation
- framebuffer architectures
- DMA ringbuffers
- VSYNC/HSYNC/PCLK semantics
- LCD polarity inversion
- scanline rendering
- pixel clock synchronization
- metastability
- asynchronous clock domains

---

# RECOMMENDED EXPERIMENTAL STRATEGY

DO NOT attempt full RGB capture immediately.

FIRST:
1. discover clocks
2. discover frame sync
3. discover line sync
4. determine valid sampling edge
5. validate timing hypotheses
6. reconstruct PNG offline

ONLY THEN:
7. attempt real-time capture
8. attempt IPS output

---

# KNOWN RISKS

Potential risks include:
- nonstandard timing
- unusual blanking behavior
- inverted clock edge
- nonstandard line markers
- unstable sampling edge
- mixed voltage domains
- analog interference
- DMA overruns
- synchronization drift

All risks must be documented continuously.

---

# REQUIRED ENGINEERING ATTITUDE

Treat this project as:
- reverse engineering instrumentation
- embedded systems research
- digital video protocol analysis

NOT as:
- a quick display mod
- a demo
- a hacky prototype

Every subsystem should be reusable and independently testable.

All experiments should leave behind:
- captures
- logs
- screenshots
- timing reports
- reproducible artifacts

The final project should resemble a professional hardware reverse engineering toolkit.
