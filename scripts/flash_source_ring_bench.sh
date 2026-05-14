#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    printf 'usage: %s <serial-port>\n' "$0" >&2
    exit 2
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EXPERIMENT_DIR="${ROOT_DIR}/experiments/source_ring_bench"
BUILD_DIR="${BUILD_DIR:-build_esp32p4}"
PORT="$1"

source "${ROOT_DIR}/scripts/idf_env.sh"
source_idf

cd "${EXPERIMENT_DIR}"
IDF_TARGET=esp32p4 idf.py -B "${BUILD_DIR}" -p "${PORT}" -b 115200 flash
