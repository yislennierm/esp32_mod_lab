# ESP32-P4 Console Signal Lab

Reverse engineering and display bridge framework for console picture signals using an ESP32-P4.

The Game Boy Color LCD bus is the first target profile. The project is intentionally being shaped as a reusable lab tool for other consoles and picture/display buses too.

The controlling project document is [PROJECT_CHARTER.md](PROJECT_CHARTER.md). All implementation, experiments, and documentation must follow that document unless it is intentionally revised.

For AI/Codex sessions, start with [docs/AI_CONTEXT.md](docs/AI_CONTEXT.md). Documentation navigation is in [docs/DOCS_INDEX.md](docs/DOCS_INDEX.md), and durable decisions are in [docs/DECISIONS.md](docs/DECISIONS.md).

Current target profile: Game Boy Color LCD bus.

Current practical status: live RGB565 browser inspection is working for the GBC target on the native USB lab path, while full-rate streaming and product-mode capture remain under development.

Current project/build model:

- `lab`: research, probing, live monitor, validation, and destination experiments
- `telemetry`: selected runtime observation for chosen ESP32-P4 blocks
- `production`: clean deployable product path with the lab control path removed from the hot loop

The first machine-readable target profile is [profiles/gbc_lcd.json](profiles/gbc_lcd.json).

The browser/host tooling roadmap is [docs/investigation_workbench.md](docs/investigation_workbench.md). The goal is a staged research instrument for pin inspection, timing measurement, signal-role discovery, raw capture, hypothesis testing, and AI-readable evidence packs; the live image viewer is only one module.

The maintenance and modularization plan is [docs/project_maintenance.md](docs/project_maintenance.md). Use it before deleting generated files, moving experiment scripts, or splitting GBC-specific code from reusable platform code.

The GBC investigation story is preserved as a web-ready article at [docs/gbc_lcd_journey.html](docs/gbc_lcd_journey.html).

The repeatable source-processing-destination method is [docs/system_method.md](docs/system_method.md), with current gaps tracked in [docs/system_gap_assessment.md](docs/system_gap_assessment.md).

## Split Status

The lab/project split is only partially reflected in the filesystem today.

What is already true:

- generic lab workflows live under `host/workbench/`, `scripts/`, `profiles/`, and shared docs
- deployable compositions live under `projects/`
- target-specific placeholder areas now exist under `docs/targets/`, `host/targets/`, and `firmware/targets/`

What is not fully true yet:

- the working GBC firmware is still mixed into `firmware/main/`
- several active GBC host tools still live at top-level `host/`
- most GBC investigation docs still live in top-level `docs/`

That is intentional for now: working code paths are being preserved while the repository grows explicit split boundaries first.

## Repository Setup

Clone the repo:

```sh
git clone https://github.com/yislennierm/esp32_mod_lab.git
cd esp32_mod_lab
```

The project is developed on macOS and Linux. The scripts are written for `bash` and use these environment variables:

| Variable | Purpose |
|---|---|
| `IDF_PATH` | Path to ESP-IDF checkout. Example: `$HOME/esp/v5.5/esp-idf`. |
| `IDF_EXPORT` | Optional direct path to `export.sh`; overrides `IDF_PATH`. |
| `PORT` | Native ESP32-P4 USB Serial/JTAG app/data port. |
| `RECOVERY_PORT` | Optional WCH UART recovery/flashing port. |
| `BUILD_DIR` | Optional ESP-IDF build directory name. |
| `LISTEN_PORT` | Browser backend port, default `8791`. |

Copy the example environment file if you want machine-local settings:

```sh
cp .env.example .env
```

Do not commit `.env`; it is ignored because port names and ESP-IDF paths are machine-specific.

Project scripts load `.env` automatically. For manual shell commands that use `$PORT` or `$RECOVERY_PORT`, load it into your current shell:

```sh
set -a
source .env
set +a
```

## Python Setup

Use a virtual environment so the Python host tools move cleanly between laptop and desktop.

macOS/Linux:

```sh
python3 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -r requirements.txt
```

The Python host tools currently depend on `pyserial`; most rendering and analysis helpers use the standard library.

## Frontend Setup

The browser workbench lives in `host/workbench/frontend`.

```sh
cd host/workbench/frontend
npm install
npm run build
cd ../../..
```

The live backend serves the built frontend from `host/workbench/frontend/dist`.

## ESP-IDF Setup

Install ESP-IDF `v5.5` or a compatible version for ESP32-P4.

Expected default layout on both macOS and Linux:

```sh
mkdir -p "$HOME/esp/v5.5"
# install or clone ESP-IDF into:
# $HOME/esp/v5.5/esp-idf
```

Then either export `IDF_PATH`:

```sh
export IDF_PATH="$HOME/esp/v5.5/esp-idf"
```

or export `IDF_EXPORT` directly:

```sh
export IDF_EXPORT="$HOME/esp/v5.5/esp-idf/export.sh"
```

The scripts use `IDF_EXPORT` first, then `IDF_PATH`, then `$HOME/esp/v5.5/esp-idf/export.sh`.

## Serial Ports

Typical macOS ports:

```text
/dev/cu.usbmodem14401              native ESP32-P4 USB Serial/JTAG app/data
/dev/cu.wchusbserial5A470211841    WCH UART recovery/flashing
```

Typical Linux ports:

```text
/dev/ttyACM0                       native ESP32-P4 USB Serial/JTAG app/data
/dev/ttyUSB0                       WCH UART recovery/flashing
/dev/serial/by-id/...              preferred stable Linux device names
```

List ports with:

```sh
python host/gbc_probe.py ports
```

On Linux, your user may need serial permissions:

```sh
sudo usermod -aG dialout "$USER"
```

Log out and back in after changing group membership.

## Build And Flash

Build lab firmware:

```sh
./scripts/build_lab_firmware.sh
```

Flash lab firmware. Use the native USB port or the WCH recovery port, depending on board state:

```sh
./scripts/flash_lab_firmware.sh "$PORT"
```

Build telemetry firmware:

```sh
./scripts/build_telemetry_firmware.sh
```

Flash telemetry firmware:

```sh
./scripts/flash_telemetry_firmware.sh "$PORT"
```

Build production firmware:

```sh
./scripts/build_production_mirror.sh
```

Flash the current known-good production GBC mirror:

```sh
DEST_SPI_LCD_RAW_SPI=1 DEST_SPI_LCD_PCLK_HZ=70000000 PRODUCTION_MIRROR_MODE=2 ./scripts/flash_production_mirror.sh "$PORT"
```

Build or flash safe recovery firmware before risky transport/capture experiments:

```sh
./scripts/build_safe_recovery.sh
./scripts/flash_safe_recovery.sh "$RECOVERY_PORT"
```

## Smoke Tests

Firmware command smoke tests:

```text
PING
GET_VERSION
GET_PINMAP
EXPORT_STATS
```

Automated command test:

```sh
python host/gbc_probe.py --port "$PORT" smoke
```

Verify transport configuration:

```sh
python host/gbc_probe.py --port "$PORT" command TRANSPORT_STATUS
```

Verify current GBC source driver:

```sh
python host/gbc_probe.py --port "$PORT" command GBC_SOURCE_STATUS
```

## Live Browser Workbench

Start the current GBC RGB565 live monitor:

```sh
PORT="$PORT" ./scripts/start_gbc_live_view_usb.sh
```

Then open:

```text
http://127.0.0.1:8791/
```

The active GBC source path is RGB565 only. Historical RGB332 tooling may remain for archived raw artifacts and generic LCD_CAM diagnostics, but it is not the active GBC source module.

Only one host process should own the ESP32-P4 app serial port at a time. Stop the live backend before flashing, monitoring, or running direct `gbc_probe.py` commands against the same port.

## Common Hardware Recovery

If flashing reports `No serial data received`, put the board into ROM download mode and retry:

1. Hold `BOOT`.
2. Tap `RESET`.
3. Release `BOOT`.
4. Run the flash command again.

## Working Rules

- Treat every target console picture bus as electrically unsafe until measured.
- Do not connect ESP32-P4 GPIOs to unknown lines without voltage verification and appropriate level shifting.
- Keep firmware capture GPIOs input-only until the active target profile has evidence for safe capture.
- Document every assumption, measurement, failure, and discovery in `docs/`.
- Prefer offline capture, inspection, and reproducibility before real-time optimization.
- Keep reusable capture/transport/viewer machinery separate from target-specific profiles.

## Repository Layout

```text
firmware/           ESP-IDF firmware sources and build outputs
firmware/main/      Current active firmware entrypoint and working modules
firmware/lab/       Intended reusable lab firmware layer
firmware/targets/   Intended target/project-specific firmware layer
host/               Python capture, decode, transport, and backend tools
host/workbench/     Browser workbench frontend and backend support
host/lab/           Intended reusable host-side lab APIs
host/targets/       Intended target-specific host modules
host/tools/         Stable CLI/tooling helpers
host/experiments/   Historical or prototype script index
projects/           Deployable project compositions and imported example projects
profiles/           Source/destination profiles and validation schema
captures/           Raw traces, decoded captures, screenshots, and oscilloscope exports
docs/               Shared lab documentation and durable decisions
docs/targets/       Target-specific documentation staging area
docs/projects/      Project-specific documentation staging area
scripts/            Repeatable build, flash, recovery, and runtime scripts
tools/              Local helper utilities and external assets
```

## Architecture Direction

The project is organized conceptually as:

```text
ESP32-P4 instrument platform
    -> target modules such as gbc_lcd
    -> future product modules such as retimers, screen mods, scalers, and analyzers
```

The GBC target is the first module and reference case. Its lessons should be preserved, but generic capture, timing, transport, artifact, and browser-workbench code should remain reusable.

## Initial GBC Success Criteria

The first major technical milestone is offline reconstruction of a recognizable Game Boy boot logo PNG from captured LCD bus traces.

## Universal Goal

Build a reusable workflow:

```text
unknown console picture bus
    -> safe ESP32-P4 capture
    -> raw reproducible artifacts
    -> hypothesis-driven decode
    -> AI/human visual inspection
    -> documented protocol model
```
