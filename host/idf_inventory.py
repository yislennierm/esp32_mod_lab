#!/usr/bin/env python3
"""Generate a local ESP-IDF inventory for ESP32-P4 Signal Lab.

This script scans an installed ESP-IDF tree without modifying it. The output is
used by the lab UI/backend to reason about official examples, components,
source files, CMake metadata, Kconfig symbols, and inferred MCU block usage.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import Any


DEFAULT_IDF_PATH = Path(os.environ.get("IDF_PATH", "~/esp/v5.5/esp-idf")).expanduser()
DEFAULT_IDF_PATH_LABEL = "${IDF_PATH}" if os.environ.get("IDF_PATH") else "~/esp/v5.5/esp-idf"
DEFAULT_TARGET = "esp32p4"
DEFAULT_OUTPUT = Path("sdk_inventory/esp-idf-v5.5-esp32p4.json")

SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hpp"}
CONFIG_FILE_NAMES = {"CMakeLists.txt", "idf_component.yml", "sdkconfig.defaults", "sdkconfig.defaults.esp32p4"}

API_RULES: list[tuple[str, str, list[str], list[str]]] = [
    ("spi_master", r"\bspi_(bus|device|transaction|slave|master|bus_lock)_", ["esp_driver_spi"], ["SPI", "GPIO Matrix / IO MUX", "GDMA", "Internal DMA RAM"]),
    ("esp_lcd", r"\besp_lcd_", ["esp_lcd"], ["LCD", "SPI", "I80", "RGB LCD", "MIPI DSI", "GPIO Matrix / IO MUX", "GDMA"]),
    ("camera", r"\besp_cam_|camera|dvp|mipi_csi", ["esp_driver_cam"], ["CAM/LCD_CAM", "ISP", "GDMA", "PSRAM"]),
    ("isp", r"\besp_isp_", ["esp_driver_isp"], ["ISP", "GDMA", "PSRAM"]),
    ("ppa", r"\bppa_|esp_ppa_", ["esp_driver_ppa"], ["PPA", "Internal DMA RAM", "PSRAM"]),
    ("jpeg", r"\bjpeg_|esp_jpeg_", ["esp_driver_jpeg"], ["JPEG", "Internal DMA RAM", "PSRAM"]),
    ("parlio", r"\bparlio_|esp_parlio_", ["esp_driver_parlio"], ["PARLIO", "GPIO Matrix / IO MUX", "GDMA"]),
    ("bitscrambler", r"\bbitscrambler|esp_bitscrambler_", ["esp_driver_bitscrambler"], ["Bitscrambler", "DMA Path"]),
    ("gpio", r"\bgpio_", ["esp_driver_gpio"], ["GPIO Matrix / IO MUX"]),
    ("usb_serial_jtag", r"\busb_serial_jtag_", ["esp_driver_usb_serial_jtag"], ["USB Serial/JTAG"]),
    ("tinyusb", r"\btinyusb_|tusb_", ["usb"], ["USB OTG", "TinyUSB"]),
    ("uart", r"\buart_", ["esp_driver_uart"], ["UART", "GPIO Matrix / IO MUX"]),
    ("i2c", r"\bi2c_", ["esp_driver_i2c"], ["I2C", "GPIO Matrix / IO MUX"]),
    ("i2s", r"\bi2s_", ["esp_driver_i2s"], ["I2S", "GPIO Matrix / IO MUX", "GDMA"]),
    ("rmt", r"\brmt_", ["esp_driver_rmt"], ["RMT", "GPIO Matrix / IO MUX"]),
    ("gptimer", r"\bgptimer_", ["esp_driver_gptimer"], ["Timers"]),
    ("ledc", r"\bledc_", ["esp_driver_ledc"], ["LEDC", "GPIO Matrix / IO MUX"]),
    ("psram", r"\bheap_caps_|MALLOC_CAP_SPIRAM|esp_psram", ["esp_psram", "heap"], ["PSRAM", "Heap"]),
    ("freertos", r"\bxTask|vTask|xQueue|xSemaphore|portYIELD|FreeRTOS", ["freertos"], ["FreeRTOS"]),
]

CATEGORY_RULES: list[tuple[str, list[str]]] = [
    ("display_camera_video", ["lcd", "camera", "dvp", "mipi", "dsi", "rgb", "jpeg", "ppa", "parlio"]),
    ("source_capture", ["camera", "dvp", "parlio_rx", "logic_analyzer", "i2s_recorder"]),
    ("destination_display", ["lcd", "rgb_panel", "i80", "spi_lcd", "mipi_dsi", "parlio_tx"]),
    ("transport_usb", ["usb", "tinyusb", "tusb", "usb_serial_jtag"]),
    ("gpio_timing", ["gpio", "gptimer", "rmt", "ledc", "pcnt"]),
    ("storage", ["spiffs", "fatfs", "sd_card", "sdmmc", "sdspi"]),
    ("wireless", ["wifi", "mesh", "openthread", "ieee802154", "bluetooth", "ble", "zigbee"]),
    ("build_system", ["build_system", "cmake", "component_manager"]),
    ("security", ["security", "tee", "flash_encryption", "hmac", "secure"]),
]

HIGH_VALUE_CATEGORIES = {
    "display_camera_video",
    "source_capture",
    "destination_display",
    "transport_usb",
    "gpio_timing",
    "build_system",
}


def utc_now() -> str:
    return dt.datetime.now(dt.UTC).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def rel(path: Path, root: Path) -> str:
    return path.relative_to(root).as_posix()


def read_text_limited(path: Path, limit: int = 750_000) -> str:
    try:
        if path.stat().st_size > limit:
            return path.read_text(encoding="utf-8", errors="ignore")[:limit]
        return path.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return ""


def run_git(idf_path: Path, args: list[str]) -> str | None:
    try:
        proc = subprocess.run(
            ["git", "-C", str(idf_path), *args],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            timeout=5,
        )
    except (OSError, subprocess.TimeoutExpired):
        return None
    if proc.returncode != 0:
        return None
    return proc.stdout.strip() or None


def idf_version(idf_path: Path) -> dict[str, str | None]:
    version_file = idf_path / "version.txt"
    version_txt = read_text_limited(version_file).strip() if version_file.exists() else None
    return {
        "version_txt": version_txt,
        "git_describe": run_git(idf_path, ["describe", "--tags", "--dirty", "--always"]),
        "git_commit": run_git(idf_path, ["rev-parse", "HEAD"]),
        "git_branch": run_git(idf_path, ["rev-parse", "--abbrev-ref", "HEAD"]),
    }


def detect_categories(path_text: str) -> list[str]:
    lowered = path_text.lower()
    categories = []
    for category, terms in CATEGORY_RULES:
        if any(term in lowered for term in terms):
            categories.append(category)
    return categories or ["unclassified"]


def detect_api_usage(text: str) -> tuple[list[str], list[str], list[str]]:
    api_groups: list[str] = []
    components: set[str] = set()
    mcu_blocks: set[str] = set()
    for name, pattern, rule_components, rule_blocks in API_RULES:
        if re.search(pattern, text):
            api_groups.append(name)
            components.update(rule_components)
            mcu_blocks.update(rule_blocks)
    return sorted(set(api_groups)), sorted(components), sorted(mcu_blocks)


def extract_includes(text: str) -> list[str]:
    includes = re.findall(r'^\s*#\s*include\s+[<"]([^>"]+)[>"]', text, flags=re.MULTILINE)
    return sorted(set(includes))


def extract_cmake_reqs(text: str) -> list[str]:
    reqs: set[str] = set()
    for match in re.finditer(r"\b(?:REQUIRES|PRIV_REQUIRES)\s+([A-Za-z0-9_\- \n\t]+?)(?:\)|SRCS|INCLUDE_DIRS|EMBED_|WHOLE_ARCHIVE)", text, flags=re.MULTILINE):
        words = re.findall(r"[A-Za-z0-9_\-]+", match.group(1))
        reqs.update(word for word in words if word not in {"REQUIRES", "PRIV_REQUIRES"})
    return sorted(reqs)


def extract_sdkconfig_keys(text: str) -> list[str]:
    keys = []
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        if "=" in stripped:
            key = stripped.split("=", 1)[0].strip()
            if key.startswith("CONFIG_"):
                keys.append(key)
    return sorted(set(keys))


def extract_kconfig_symbols(text: str) -> list[str]:
    symbols = re.findall(r"^\s*(?:config|menuconfig)\s+([A-Za-z0-9_]+)", text, flags=re.MULTILINE)
    return sorted(set(f"CONFIG_{symbol}" for symbol in symbols))


def component_inventory(idf_path: Path) -> list[dict[str, Any]]:
    components_root = idf_path / "components"
    components: list[dict[str, Any]] = []
    for component_dir in sorted(path for path in components_root.iterdir() if path.is_dir()):
        files = sorted(path for path in component_dir.rglob("*") if path.is_file())
        cmake_files = [path for path in files if path.name == "CMakeLists.txt"]
        kconfig_files = [path for path in files if path.name in {"Kconfig", "Kconfig.projbuild"}]
        yml_files = [path for path in files if path.name == "idf_component.yml"]
        source_files = [path for path in files if path.suffix in SOURCE_SUFFIXES]

        source_text = "\n".join(read_text_limited(path, 120_000) for path in source_files[:80])
        kconfig_text = "\n".join(read_text_limited(path, 200_000) for path in kconfig_files)
        api_groups, inferred_components, mcu_blocks = detect_api_usage(source_text)

        components.append(
            {
                "name": component_dir.name,
                "path": rel(component_dir, idf_path),
                "cmake_files": [rel(path, idf_path) for path in cmake_files],
                "idf_component_yml": [rel(path, idf_path) for path in yml_files],
                "kconfig_files": [rel(path, idf_path) for path in kconfig_files],
                "source_file_count": len(source_files),
                "source_files_sample": [rel(path, idf_path) for path in source_files[:30]],
                "kconfig_symbols": extract_kconfig_symbols(kconfig_text)[:250],
                "api_groups": api_groups,
                "inferred_components": inferred_components,
                "mcu_blocks": mcu_blocks,
                "categories": detect_categories(component_dir.name),
            }
        )
    return components


def is_example_root(path: Path, examples_root: Path) -> bool:
    if path == examples_root:
        return False
    if not (path / "CMakeLists.txt").exists():
        return False
    if (path / "main" / "CMakeLists.txt").exists():
        return True
    if (path / "README.md").exists() and any(child.name == "main" and child.is_dir() for child in path.iterdir()):
        return True
    return False


def example_inventory(idf_path: Path) -> list[dict[str, Any]]:
    examples_root = idf_path / "examples"
    examples: list[dict[str, Any]] = []
    roots = sorted(path for path in examples_root.rglob("*") if path.is_dir() and is_example_root(path, examples_root))
    for example_dir in roots:
        files = sorted(path for path in example_dir.rglob("*") if path.is_file())
        source_files = [path for path in files if path.suffix in SOURCE_SUFFIXES]
        cmake_files = [path for path in files if path.name == "CMakeLists.txt"]
        yml_files = [path for path in files if path.name == "idf_component.yml"]
        sdkconfig_files = [path for path in files if path.name.startswith("sdkconfig.defaults")]
        readme = example_dir / "README.md"

        text_parts = []
        for path in source_files[:80]:
            text_parts.append(read_text_limited(path, 160_000))
        source_text = "\n".join(text_parts)
        cmake_text = "\n".join(read_text_limited(path, 120_000) for path in cmake_files)
        sdkconfig_text = "\n".join(read_text_limited(path, 120_000) for path in sdkconfig_files)

        api_groups, inferred_components, mcu_blocks = detect_api_usage(source_text)
        cmake_reqs = extract_cmake_reqs(cmake_text)
        all_components = sorted(set(inferred_components).union(cmake_reqs))
        categories = detect_categories(rel(example_dir, examples_root))
        score = sum(2 for category in categories if category in HIGH_VALUE_CATEGORIES)
        score += len(api_groups)
        relevance = "high" if score >= 5 else "medium" if score >= 2 else "track"

        examples.append(
            {
                "id": rel(example_dir, examples_root),
                "name": example_dir.name,
                "path": rel(example_dir, idf_path),
                "category_path": rel(example_dir.parent, examples_root),
                "readme": rel(readme, idf_path) if readme.exists() else None,
                "cmake_files": [rel(path, idf_path) for path in cmake_files],
                "idf_component_yml": [rel(path, idf_path) for path in yml_files],
                "sdkconfig_defaults": [rel(path, idf_path) for path in sdkconfig_files],
                "sdkconfig_keys": extract_sdkconfig_keys(sdkconfig_text),
                "source_file_count": len(source_files),
                "source_files": [rel(path, idf_path) for path in source_files],
                "includes": extract_includes(source_text)[:250],
                "cmake_requires": cmake_reqs,
                "api_groups": api_groups,
                "components": all_components,
                "mcu_blocks": mcu_blocks,
                "categories": categories,
                "relevance": relevance,
                "import_status": "candidate",
                "research_notes": "",
            }
        )
    return examples


def summarize(components: list[dict[str, Any]], examples: list[dict[str, Any]]) -> dict[str, Any]:
    by_category: dict[str, int] = {}
    by_relevance: dict[str, int] = {}
    mcu_blocks: dict[str, int] = {}
    api_groups: dict[str, int] = {}
    for example in examples:
        by_relevance[example["relevance"]] = by_relevance.get(example["relevance"], 0) + 1
        for category in example["categories"]:
            by_category[category] = by_category.get(category, 0) + 1
        for block in example["mcu_blocks"]:
            mcu_blocks[block] = mcu_blocks.get(block, 0) + 1
        for group in example["api_groups"]:
            api_groups[group] = api_groups.get(group, 0) + 1

    return {
        "component_count": len(components),
        "example_count": len(examples),
        "examples_by_category": dict(sorted(by_category.items())),
        "examples_by_relevance": dict(sorted(by_relevance.items())),
        "example_mcu_block_hits": dict(sorted(mcu_blocks.items())),
        "example_api_group_hits": dict(sorted(api_groups.items())),
        "top_import_candidates": [
            {
                "id": example["id"],
                "relevance": example["relevance"],
                "categories": example["categories"],
                "api_groups": example["api_groups"],
                "mcu_blocks": example["mcu_blocks"],
            }
            for example in sorted(
                examples,
                key=lambda item: (
                    {"high": 2, "medium": 1, "track": 0}.get(item["relevance"], 0),
                    len(item["api_groups"]),
                    len(item["mcu_blocks"]),
                    item["id"],
                ),
                reverse=True,
            )[:50]
        ],
    }


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Generate local ESP-IDF inventory.")
    parser.add_argument("--idf-path", type=Path, default=Path(os.environ.get("IDF_PATH", DEFAULT_IDF_PATH)))
    parser.add_argument(
        "--source-path-label",
        default=DEFAULT_IDF_PATH_LABEL,
        help="Portable label stored in the generated inventory instead of the resolved local SDK path.",
    )
    parser.add_argument("--target", default=DEFAULT_TARGET)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args(argv)

    idf_path = args.idf_path.expanduser().resolve()
    if not idf_path.exists():
        raise SystemExit(f"ESP-IDF path does not exist: {idf_path}")
    if not (idf_path / "components").exists() or not (idf_path / "examples").exists():
        raise SystemExit(f"Path does not look like ESP-IDF: {idf_path}")

    components = component_inventory(idf_path)
    examples = example_inventory(idf_path)
    inventory = {
        "schema": "esp_idf_inventory.v1",
        "generated_at": utc_now(),
        "source": {
            "idf_path": args.source_path_label,
            "target": args.target,
            "version": idf_version(idf_path),
        },
        "classification": {
            "method": "filesystem_scan_with_regex_api_inference",
            "api_rules": [{"name": name, "pattern": pattern, "components": comps, "mcu_blocks": blocks} for name, pattern, comps, blocks in API_RULES],
            "category_rules": {category: terms for category, terms in CATEGORY_RULES},
            "note": "This inventory is a conservative index. It does not claim full C semantic understanding.",
        },
        "summary": summarize(components, examples),
        "components": components,
        "examples": examples,
    }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(inventory, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"Wrote {len(components)} components and {len(examples)} examples to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
