# Target Firmware Layer

This directory defines the intended home for target-specific firmware modules that should not remain mixed into the generic lab runtime.

Examples:

- `gbc_lcd/`
- future console-specific source drivers
- project-specific production pipelines that are not reusable as generic lab blocks

Current state:

- the working GBC source and production mirror code still lives in `firmware/main/`
- this directory is a scaffold only for now so the repository structure reflects the planned split

Migration rule:

- move code here only in deliberate slices with compatibility wrappers or coordinated CMake changes
- preserve known-good `lab`, `telemetry`, and `production` build behavior while migrating
