# GBC LCD Firmware Target

This directory holds Game Boy Color LCD-specific firmware modules that should
not remain mixed into the generic ESP32-P4 lab runtime long-term.

Current migration state:

- `gbc_lcd_source.*` now lives here as the target-owned source driver
- `pinmap_gbc.h` now lives here as the target-owned wiring contract
- `firmware/main/` keeps compatibility wrappers so existing builds and includes
  continue to work during the migration

Follow-up migration candidates:

- production-mirror logic that is only meaningful for the GBC screen-mod path
- target-specific decoding and framing assumptions
- any future fixed GBC board/panel pin contracts
