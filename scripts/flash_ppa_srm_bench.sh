#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
    printf 'usage: %s <serial-port>\n' "$0" >&2
    printf 'example: %s /dev/cu.wchusbserial5A470211841\n' "$0" >&2
    exit 2
fi

if [[ "${ALLOW_EXPERIMENTAL_PPA_FLASH:-0}" != "1" ]]; then
    printf 'Refusing to flash standalone PPA benchmark by default.\n' >&2
    printf '\n' >&2
    printf 'This experiment uses a separate ESP-IDF app/sdkconfig and has caused\n' >&2
    printf 'manual board recovery on the current ESP32-P4 setup. Keep using the\n' >&2
    printf 'known-good lab/production firmware until PPA is integrated behind the\n' >&2
    printf 'normal recovery-safe firmware path.\n' >&2
    printf '\n' >&2
    printf 'To override intentionally:\n' >&2
    printf '  ALLOW_EXPERIMENTAL_PPA_FLASH=1 %s <serial-port>\n' "$0" >&2
    exit 3
fi

PORT="$1"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EXPERIMENT_DIR="${ROOT_DIR}/experiments/ppa_srm_bench"
BUILD_DIR="${BUILD_DIR:-build_esp32p4}"
BAUD="${BAUD:-115200}"

source "${ROOT_DIR}/scripts/idf_env.sh"
source_idf

cd "${EXPERIMENT_DIR}"
IDF_TARGET=esp32p4 idf.py -B "${BUILD_DIR}" -p "${PORT}" -b "${BAUD}" flash
