# Lab Firmware Layer

This directory defines the intended home for reusable investigation firmware modules.

Scope:

- probing
- timing capture
- generic source ingress experiments
- transport diagnostics
- destination bring-up helpers
- observability hooks shared by multiple projects

Current state:

- the active implementation still lives in `firmware/main/`
- do not move working source files here yet unless include paths, CMake wiring, and flash/build scripts are updated together

Split rule:

- modules that are generic to the ESP32-P4 lab should end up here
- target-specific product code should not accumulate here long-term
