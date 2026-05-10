#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
    printf 'usage: %s <serial-port>\n' "$0" >&2
    printf 'example: %s /dev/cu.usbmodem14301\n' "$0" >&2
    exit 2
fi

PORT="$1"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FIRMWARE_DIR="${ROOT_DIR}/firmware"
BUILD_DIR="${BUILD_DIR:-build_esp32p4}"
BAUD="${BAUD:-115200}"

source "${ROOT_DIR}/scripts/idf_env.sh"
source_idf

cd "${FIRMWARE_DIR}"
IDF_TARGET=esp32p4 idf.py -B "${BUILD_DIR}" -p "${PORT}" -b "${BAUD}" flash
