#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FIRMWARE_DIR="${ROOT_DIR}/firmware"
BUILD_DIR="${BUILD_DIR:-build_esp32p4}"

source "${ROOT_DIR}/scripts/idf_env.sh"
source_idf

cd "${FIRMWARE_DIR}"
IDF_TARGET=esp32p4 idf.py -B "${BUILD_DIR}" build

printf 'normal probe image: %s/%s/gbc_p4_probe.bin\n' "${FIRMWARE_DIR}" "${BUILD_DIR}"
