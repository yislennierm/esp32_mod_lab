# ESP32-P4 Reference PCB Architecture

Purpose: define a custom PCB architecture for the ESP32-P4 console signal lab and future product bridges.

Status: draft architecture contract. This is not a schematic; it is the board-level design intent that schematic and layout work should satisfy.

Last updated: 2026-05-11.

## Objective

Design a purpose-built ESP32-P4 board that can act as both:

- a lab instrument for unknown console picture buses
- a production bridge platform for source -> processing -> destination projects

This matters because the current dev board is useful for proof, but it is not the final constraint. A custom PCB lets us route the right USB, display, source, isolation, debug, and measurement paths intentionally.

## Current Understanding

The current dev-board setup has proven the high-level idea:

```text
GBC LCD source bus
    -> ESP32-P4 LCD_CAM/GDMA capture
    -> RGB565 frame representation
    -> RGB666 SPI debug LCD output
```

Confidence is high that the ESP32-P4 can serve as the platform for a reusable signal lab. Confidence is low that the current dev board wiring is the right production topology.

Key lessons from the GBC work:

- Direct GPIO attachment can back-power an unpowered target.
- Software isolation helps but is not equivalent to hardware isolation.
- Debug SPI LCD output is valuable, but one-bit SPI is bandwidth-limited.
- USB Serial/JTAG is good for control/logging but not a full-rate frame data plane.
- The source module, processing blocks, and destination modules should remain separable.

## Board Roles

The reference PCB should expose four independent roles:

| Role | Purpose | Must Be Independent From |
|---|---|---|
| Source front end | Safely receive unknown console picture signals | Destination output and debug GPIOs |
| ESP32-P4 processing core | Capture, buffer, retime, scale, convert, stream | Target power state |
| Destination outputs | Drive displays or external video/capture paths | Source discovery wiring |
| Control/debug/data | Let host/browser/AI observe and command the board | High-rate video hot path |

## Proposed High-Level Topology

```text
target source connector
    -> protection / isolation / level conditioning
    -> ESP32-P4 source input bank
    -> DMA-capable frame buffers
    -> optional processing blocks
    -> destination connector(s)

host computer
    -> USB Serial/JTAG or UART control/debug
    -> USB HS device data plane

test equipment
    -> buffered test pads / probe headers
```

## Source Front End

### Requirements

- Every external source signal must pass through a defined front-end stage before ESP32-P4 GPIO.
- Every source bank must support a true high-impedance state when the target is off or unknown.
- The ESP32-P4 must be unable to back-power the target through source lines.
- Target power-good must control or gate source-buffer output-enable.
- All signal names, voltage assumptions, and safe states must be profile-driven.

### Recommended Circuit Blocks

| Block | Purpose | Notes |
|---|---|---|
| Series resistor per source signal | Current limiting, ringing reduction, accidental contention mitigation | Minimum protection, not sufficient alone for back-power prevention |
| Partial-power-down/Ioff buffer or bus switch | Prevent powered ESP32-P4 side from biasing unpowered target | Prefer devices with explicit Ioff behavior |
| Target power-good detector | Gate source buffer OE and inform firmware | Avoid relying on firmware before reset/application startup |
| Optional level translator | Support 1.8 V, 3.3 V, or other console logic | Must be chosen for speed and directionality |
| Probe/test pads | Let oscilloscope validate source side and ESP side separately | Place before and after buffer where practical |
| Ground reference strategy | Ensure measurement and signal integrity | Common ground still required for digital capture unless isolated design is used |

### Source Connector Strategy

Use interchangeable source adapters where possible:

- main board carries the ESP32-P4 and generic input banks
- target-specific flex/edge adapters carry console connector pinouts
- adapters define dangerous rails, no-connect pins, and mechanical constraints

For GBC specifically:

- keep analog rails (`V0-V9`, `VCOM`, `VEE`, `VSHA`, `VSHD`) physically separated from ESP32-P4 GPIO banks
- route them only to measurement pads or safe analog measurement circuits if needed
- do not route them through generic digital connector positions without clear labeling

## ESP32-P4 Core Routing

### Capture Inputs

Prioritize an LCD_CAM-compatible parallel input bank:

- enough data lines for RGB666-class buses
- timing/control lines for pixel clock, line markers, frame markers, and data-enable candidates
- short, length-conscious routing for pixel clock and data
- test pads for pixel clock and sync candidates

Also keep a secondary generic timing bank:

- PCNT/GPIO/RMT-friendly inputs for slow discovery
- safe pull configuration options
- visible status LEDs only on buffered/internal signals, not directly on unknown source pins

### Memory And Power

- Use PSRAM configuration compatible with DMA frame buffers.
- Place decoupling and power rails for high-rate LCD_CAM, USB HS, and display outputs conservatively.
- Keep noisy display/USB routing away from sensitive source clock inputs where possible.

## Destination Outputs

The board should expose multiple destination classes, but they do not all need to be populated in one assembly.

| Destination | Role | PCB Recommendation |
|---|---|---|
| SPI/QSPI LCD | Bring-up/debug panel | Keep, but label as debug destination unless bandwidth proves otherwise |
| I80 LCD | Practical parallel TFT output | Route a connector/footprint if pins allow; good production candidate |
| RGB LCD | Continuous parallel display output | Route for serious display bridge experiments if enough pins remain |
| MIPI DSI | High-bandwidth modern panel output | Route if package/board stack-up and connector allow controlled impedance |
| USB HS device | Capture-card output to computer | Route correctly and independently from Serial/JTAG control |

Important ESP32-P4 constraint:

- `soc_caps.h` notes that I80 bus and RGB timing generator cannot work at the same time in the LCD_CAM peripheral.
- Treat I80 and RGB LCD as alternate destination modes, not simultaneous active outputs.

## USB And Debug Architecture

Use at least two host-facing paths:

| Path | Purpose | Requirement |
|---|---|---|
| USB Serial/JTAG or UART | flashing, logs, commands, recovery | Must remain available even when video data plane firmware is experimental |
| USB HS OTG device | high-rate frame/data stream | Must route the correct ESP32-P4 USB OTG D+/D- path and VBUS sense requirements |

Do not depend on USB Serial/JTAG for full-rate video streaming. Existing project evidence shows RGB565-sized streams over the current Serial/JTAG command path are far below the GBC native frame-rate requirement.

Recommended debug features:

- UART header for ROM recovery
- BOOT and RESET buttons with accessible pads
- JTAG/Serial path clearly labeled
- USB HS connector clearly labeled as data-plane path
- power-good/status LEDs for target power, source buffer OE, ESP32-P4 power, and USB HS VBUS
- optional logic analyzer header for selected buffered source signals

## Pin Planning Rules

Pin planning should be a first-class design artifact, not a late schematic cleanup.

Create a PCB pin table with:

- ESP32-P4 GPIO number
- package pin / ball
- schematic net
- source/destination/control owner
- reset/default behavior
- boot strap concern
- IO_MUX role
- GPIO-matrix role
- voltage domain
- direction
- high-Z/off behavior
- test pad reference

Rules:

- Do not assign a GPIO to both a source line and a destination line in the same assembly option.
- Keep USB Serial/JTAG pins and USB HS OTG pins out of generic GPIO banks.
- Keep boot strapping pins out of unknown target connectors unless there is a documented reason and resistor network.
- Do not route known-dangerous analog target rails into generic digital banks.
- Reserve pins for recovery and debug even in production assemblies.
- Treat DSI/CSI/USB HS as controlled-impedance layout domains, not generic GPIO.

## Assembly Variants

Recommended variants:

| Variant | Purpose | Populated Blocks |
|---|---|---|
| Lab input-only | Safe source discovery | Source connector, buffers, USB control, test pads; no destination driver required |
| Lab full | Source + debug display + USB stream | Source front end, SPI debug LCD, USB HS, debug/control |
| Product display bridge | Console source to display | Source front end, chosen production display output, minimal debug |
| Capture-card | Console source to computer | Source front end, USB HS data plane, control/debug path |

## Unknowns

- Exact ESP32-P4 package/pinout to use for the custom PCB.
- Which source input width should be standard: 16-bit, 18-bit, 24-bit, or modular banks.
- Whether DSI should be routed on the first custom board or left for a second revision.
- Whether I80 or RGB LCD should be the first production display path.
- Which bus-switch/level-shifter family has the best speed, leakage, and partial-power-down behavior for console LCD buses.
- Whether target adapters should include EEPROM/profile identification.

## Experiment Results Feeding This Design

- GBC direct wiring produced visible frames but also revealed back-power risk when the target was unpowered.
- Moving `CLS` from `GPIO32` to `GPIO3` stabilized GBC power cycling in the current setup.
- Current SPI LCD debug output works but cannot meet full-rate scaled-output bandwidth.
- Current USB Serial/JTAG path is reliable for control but too slow for full-rate RGB565 frame streaming.
- TinyUSB code/build work is not enough without the correct USB OTG hardware route.

## Next Steps

1. Create a `docs/platform/reference_pcb_pin_plan.md` with candidate ESP32-P4 pin groups.
2. Choose the first custom-board destination policy:
   - USB HS capture-card first
   - I80/RGB LCD display bridge first
   - DSI display bridge first
3. Select candidate source isolation/buffer parts with Ioff or partial-power-down behavior.
4. Define a source adapter connector pinout and mechanical strategy.
5. Add measurement points and board bring-up tests before schematic capture:
   - power rails
   - target power-good
   - source buffer OE
   - DCLK before/after buffer
   - representative RGB bits before/after buffer
   - USB HS enumeration
   - destination test-pattern output
