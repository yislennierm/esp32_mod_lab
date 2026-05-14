# GBC Alignment Test ROM

Temporary homebrew ROM for ESP32-P4 LCD bus capture validation.

The ROM renders a deterministic `160x144` Game Boy Color background:

- unique colored corner blocks
- one-pixel style vertical and horizontal stripe tiles
- checkerboard tiles
- border and center crosshair
- diagonal reference tiles
- GBC background palettes chosen to expose channel/order errors

Build:

```sh
make
```

Output:

```text
build/p4_align.gbc
```

This ROM is generated from local source and contains no commercial ROM assets.
