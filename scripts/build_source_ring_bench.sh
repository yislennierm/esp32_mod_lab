#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EXPERIMENT_DIR="${ROOT_DIR}/experiments/source_ring_bench"
BUILD_DIR="${BUILD_DIR:-build_esp32p4}"

source "${ROOT_DIR}/scripts/idf_env.sh"
source_idf

cd "${EXPERIMENT_DIR}"
IDF_TARGET=esp32p4 idf.py -B "${BUILD_DIR}" build

printf 'source ring bench image: %s/%s/source_ring_bench.bin\n' "${EXPERIMENT_DIR}" "${BUILD_DIR}"
