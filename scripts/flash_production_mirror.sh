#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
    printf 'usage: %s <serial-port>\n' "$0" >&2
    printf 'example: %s /dev/cu.wchusbserial5A470211841\n' "$0" >&2
    exit 2
fi

PORT="$1"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FIRMWARE_DIR="${ROOT_DIR}/firmware"
BUILD_DIR="${BUILD_DIR:-build_production_mirror}"
BAUD="${BAUD:-115200}"
PRODUCTION_MIRROR_MODE="${PRODUCTION_MIRROR_MODE:-1}"

source "${ROOT_DIR}/scripts/idf_env.sh"
source_idf

cd "${FIRMWARE_DIR}"
GBC_P4_PRODUCTION_MIRROR=1 PRODUCTION_MIRROR_MODE="${PRODUCTION_MIRROR_MODE}" IDF_TARGET=esp32p4 idf.py -B "${BUILD_DIR}" -p "${PORT}" -b "${BAUD}" reconfigure flash
