#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FIRMWARE_DIR="${ROOT_DIR}/firmware"
BUILD_DIR="${BUILD_DIR:-build_production_mirror}"
PRODUCTION_MIRROR_MODE="${PRODUCTION_MIRROR_MODE:-1}"

source "${ROOT_DIR}/scripts/idf_env.sh"
source_idf

cd "${FIRMWARE_DIR}"
GBC_P4_PRODUCTION_MIRROR=1 PRODUCTION_MIRROR_MODE="${PRODUCTION_MIRROR_MODE}" IDF_TARGET=esp32p4 idf.py -B "${BUILD_DIR}" reconfigure build

printf 'production mirror image: %s/%s/gbc_p4_probe.bin\n' "${FIRMWARE_DIR}" "${BUILD_DIR}"
