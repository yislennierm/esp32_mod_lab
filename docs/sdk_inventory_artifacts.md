# SDK Inventory Artifacts

Purpose: describe generated inventory files used by the lab.

Status: supporting reference.

Last updated: 2026-05-14.

## Objective

Keep generated SDK and repository inventories discoverable, reproducible, and separate from hand-authored architecture documents.

## Current Understanding

The lab now has two inventory families:

```text
sdk_inventory/
└── esp-idf-v5.5-esp32p4.json

inventories/
└── espressif_github_repositories.json
```

Generators:

```text
host/idf_inventory.py
host/espressif_github_inventory.py
```

The SDK inventory is local and tied to the installed ESP-IDF tree. The GitHub inventory is remote and tied to the Espressif organization state at generation time.

Confidence level: high for metadata indexing, medium for automatic API-to-block classification.

## Unknowns

- Whether future ESP-IDF releases will require deeper parsing of CMake or Kconfig.
- Whether all ESP32-P4-specific example support can be inferred without running `idf.py` per example.
- Whether the UI should store user research notes inside generated inventory files or in separate annotation overlays.

## Experiment Results

Initial generator support:

- ESP-IDF version/git metadata
- component list
- example list
- source file paths
- includes
- CMake requirement extraction
- `sdkconfig.defaults` key extraction
- Kconfig symbol extraction for components
- regex-based API group detection
- inferred MCU block usage

The generator is intentionally conservative. Unknown source remains unknown rather than being forced into a fake block.

Initial local ESP-IDF scan:

```text
ESP-IDF path label: ${IDF_PATH}
Target: esp32p4
Git describe: v5.5-dirty
Components indexed: 108
Examples indexed: 416
```

Detected high-value example families include:

- `peripherals/spi_master/lcd`
- `peripherals/lcd/rgb_panel`
- `peripherals/camera/dvp_isp_dsi`
- `peripherals/ppa/ppa_dsi`
- `peripherals/parlio/parlio_rx/logic_analyzer`
- `peripherals/usb/device/tusb_serial_device`
- `peripherals/gpio/generic_gpio`
- `get-started/blink`

Backend inventory endpoints:

```text
GET /api/sdk/idf
GET /api/sdk/components
GET /api/sdk/examples
GET /api/sdk/examples?id=<example-id>
GET /api/sdk/examples/<example-id>
GET /api/research/espressif/repos
```

Useful filters:

```text
/api/sdk/examples?relevance=high
/api/sdk/examples?category=destination_display
/api/sdk/examples?api_group=spi_master
/api/sdk/examples?mcu_block=SPI
/api/sdk/examples?q=dvp
/api/research/espressif/repos?relevance=high
/api/research/espressif/repos?category=ai_agent
/api/research/espressif/repos?q=claw
```

UI support:

- The workbench has an `SDK` navigation item.
- The top strip shows compact inventory status for the local SDK and Espressif repositories.
- The main browser switches between local ESP-IDF examples and Espressif GitHub repositories.
- The inspector stays on the right and follows the selected example or repository.
- Example filters support search, relevance, category, API group, and MCU block.
- Repository filters support search, relevance, and category.
- Repository links open in the operating-system/browser as normal external links. The lab indexes and filters repositories; it does not embed GitHub or duplicate repository browsing.

## Next Steps

- Import selected examples as lab projects with read-only SDK source references.
- Add descriptor files for known API groups so inference becomes more precise over time.
