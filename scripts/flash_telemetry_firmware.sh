#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
    printf 'usage: %s <serial-port>\n' "$0" >&2
    printf 'example: %s /dev/ttyACM0\n' "$0" >&2
    exit 2
fi

PORT="$1"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FIRMWARE_DIR="${ROOT_DIR}/firmware"
SELECTED_BUILD_DIR="${TELEMETRY_BUILD_DIR:-build_telemetry}"
BAUD="${BAUD:-115200}"

source "${ROOT_DIR}/scripts/idf_env.sh"
source_idf

cd "${FIRMWARE_DIR}"
GBC_P4_TELEMETRY=1 IDF_TARGET=esp32p4 idf.py -B "${SELECTED_BUILD_DIR}" -p "${PORT}" -b "${BAUD}" build flash
