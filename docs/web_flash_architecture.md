# Browser Flashing And Three-Build Architecture

## Objective

Define the architectural rule for how the lab should build, flash, observe, and ship ESP32-P4 projects without letting lab/WYSIWYG tooling pollute production firmware.

This matters because the project goal is higher than a typical visual tool:

- `lab` firmware must support investigation and controlled hardware interaction
- `telemetry` firmware must support selected observability with minimal behavioral drift
- `production` firmware must be as close as possible to a clean hand-written ESP-IDF application for the same function

The reference mental model is simple:

```text
project intent / graph / SDF-like model
    -> build flavor selection
    -> generated artifacts
    -> browser flashing
    -> runtime ownership and telemetry
```

## Research Summary

Current browser-based flashing is real and already supported by the platform pieces we need.

- Chrome's Web Serial API supports browser access to serial ports, including `navigator.serial.requestPort()`, `navigator.serial.getPorts()`, and output signal control through `port.setSignals()` for DTR/RTS-style reset handling.
- Espressif's `esptool-js` is the browser-oriented JavaScript implementation of `esptool` and is intended for Web Serial based flashing.
- `esptool-js` supports built-in reset strategies including `UsbJtagSerialReset`, plus custom reset sequences when boards need non-default DTR/RTS timing.
- Espressif's `ESP Launchpad` is an official browser flasher that can flash prebuilt firmware and publish firmware sets from a TOML configuration, but it uses WebUSB and is aimed more at published installers than a tightly integrated local lab workflow.
- Espressif's normal build flow still emits the canonical flash image set and offsets that we should preserve: bootloader, partition table, app image, and flash arguments.

Conclusion:

- for the integrated local lab, use `esptool-js` inside the workbench
- for public "one-click install" distribution later, optionally emit an `ESP Launchpad` config as a publishing artifact

## Core Principle

The lab must not be the product.

The lab is an authoring, instrumentation, validation, and deployment environment.
The production firmware is a generated or assembled result of project intent, not a lab runtime with features disabled.

That means:

- `lab` is allowed to contain extra code for probing, safe GPIO manipulation, inspection, graph control, and evidence collection
- `telemetry` is allowed to contain only the observation hooks explicitly selected for that project
- `production` must contain only what is needed for the final product behavior and platform bring-up

The bar:

- a generated production build for `hello_led` should look and behave like a clean ESP-IDF LED example, not a lab firmware hiding behind flags
- a generated production build for GBC mirror should look like a clean product firmware assembled from proven blocks, not a debug build with the UI path disabled

## Architecture Rule

Use one project model and three build compositions.

### 1. Project IR

Every project should have a single machine-readable source of truth:

- project metadata
- graph / SDF-like block topology
- target and destination profiles
- resource claims
- optional telemetry points
- codegen parameters

This IR should not be a binary blob or opaque editor state. It should stay inspectable and versionable.

### 2. Build Flavors

`lab`

- includes browser command/control compatibility
- includes safe GPIO/manual manipulation tools where appropriate
- includes research helpers, probes, and evidence capture paths
- may use generic lab modules that would never ship in a product

`telemetry`

- includes the production pipeline
- includes only selected telemetry taps and status export points
- should preserve production timing and ownership as much as possible
- should not include arbitrary lab mutation/debug features

`production`

- includes only the product data path and required control/config code
- must not link the lab command server unless the product explicitly requires a runtime control channel
- should be generated toward a clean ESP-IDF project structure and idioms

## Browser Flashing Rule

Build on the workstation or CI. Flash from the browser.

Do not try to compile ESP-IDF in the browser. The browser should flash already-built artifacts.

The workbench split should be:

### Backend responsibilities

- build `lab`, `telemetry`, or `production`
- generate a flash manifest from ESP-IDF outputs
- expose artifacts over HTTP
- manage serial ownership with the running backend
- stop live monitor / release the board before browser flashing begins
- regenerate production installers and published manifests when needed

### Frontend responsibilities

- ask the user for a serial port with Web Serial
- fetch the selected build's flash manifest and binaries
- flash via `esptool-js`
- show progress, chip detection, reset mode, and failure logs
- reconnect or hand back to monitor mode after flashing

## Flash Manifest Contract

The lab backend should emit a build-specific flash manifest, derived from the canonical ESP-IDF output.

Minimum shape:

```json
{
  "project_id": "hello_led",
  "build_profile": "production",
  "chip": "esp32p4",
  "generated_at": "2026-05-14T00:00:00Z",
  "reset_mode_before": "default_reset",
  "reset_mode_after": "hard_reset",
  "preferred_transport": "webserial",
  "images": [
    { "path": "/artifacts/hello_led/production/bootloader.bin", "address": 8192 },
    { "path": "/artifacts/hello_led/production/partition-table.bin", "address": 32768 },
    { "path": "/artifacts/hello_led/production/app.bin", "address": 65536 }
  ],
  "flash_mode": "dio",
  "flash_freq": "80m",
  "flash_size": "detect"
}
```

The source of truth for this should be the real ESP-IDF output, such as `flash_args` and the generated esptool command, not hand-maintained offsets in UI code.

## Port Ownership Rule

The board has one active owner at a time.

That means:

- live monitor cannot own the port while browser flashing owns it
- direct host probe commands cannot own the port while live monitor owns it
- the UI must make ownership explicit

Required UI states:

- `idle`
- `monitor_attached`
- `flash_preparing`
- `browser_flash_active`
- `flash_complete`
- `reconnect_available`

Before browser flashing:

1. backend stops capture/runtime sessions
2. backend releases the device
3. browser requests port access
4. browser flashes selected build
5. browser resets device
6. user optionally re-attaches monitor mode

## Production Codegen Rule

Production should be assembled from the project IR into clean ESP-IDF-shaped output.

The generator may reuse:

- project templates
- block templates
- Espressif driver calls
- selected official example structures

But production output should avoid:

- unused lab modules
- generic workbench transport layers
- probe handlers
- debug-only command parsers
- accidental telemetry code when telemetry is not selected

For a trivial project like `hello_led`:

- `lab` may include pin discovery and manual high/low control
- `telemetry` may include LED state reporting and maybe GPIO timing counters
- `production` should be essentially a clean LED app with the chosen pin and behavior

## Recommended Tooling Direction

### Local lab flashing

Preferred path:

- embed `esptool-js` in the workbench frontend
- feed it backend-generated flash manifests
- support explicit reset profiles per board/transport

Why:

- tightly integrated with the current lab UI
- works with local artifacts
- keeps the lab in control of build selection and serial handoff
- gives us enough control for `lab`, `telemetry`, and `production`

### Published public installers

Optional later path:

- emit `ESP Launchpad` TOML configs for public-ready builds

Why:

- official Espressif publishing path
- useful when shipping known-good prebuilt firmware to others
- separate from the richer local lab workflow

## Design Constraint For AI/WYSIWYG

The graph editor is not allowed to justify runtime bloat.

The editor may be rich. The shipping firmware must stay lean.

So the architecture should be:

```text
rich editable model
    -> validation
    -> code generation / assembly
    -> flavor-specific output
```

Not:

```text
rich editable model
    -> one giant runtime
    -> flags disable most of it in "production"
```

The second model is exactly what we should reject.

## Immediate Next Steps

1. Add a backend artifact endpoint that exposes per-build flash manifests from actual ESP-IDF outputs.
2. Add a frontend Flash panel that uses Web Serial plus `esptool-js`.
3. Add explicit serial ownership transitions in the workbench UI.
4. Separate `telemetry` runtime hooks from generic `lab` command/probe paths.
5. Start the first generated clean production example with `hello_led` as the reference proof.
