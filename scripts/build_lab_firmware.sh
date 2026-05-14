#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FIRMWARE_DIR="${ROOT_DIR}/firmware"
SELECTED_BUILD_DIR="${LAB_BUILD_DIR:-build_lab}"

source "${ROOT_DIR}/scripts/idf_env.sh"
source_idf

cd "${FIRMWARE_DIR}"
GBC_P4_LAB=1 IDF_TARGET=esp32p4 idf.py -B "${SELECTED_BUILD_DIR}" build

printf 'lab image: %s/%s/gbc_p4_probe.bin\n' "${FIRMWARE_DIR}" "${SELECTED_BUILD_DIR}"
