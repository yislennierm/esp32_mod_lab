#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PORT="${PORT:-/dev/cu.usbmodem14401}"
LISTEN_PORT="${LISTEN_PORT:-8791}"

source "${ROOT_DIR}/scripts/idf_env.sh"
source_idf

cd "${ROOT_DIR}"
python -B host/live_lcdcam_stream_viewer.py \
    --port "${PORT}" \
    --listen-port "${LISTEN_PORT}" \
    --no-pclk-invert \
    --interval-ms 33 \
    --capture-timeout-ms 300 \
    --width 192 \
    --height 145 \
    --data-mode RGB565 \
    --firmware-binary \
    --gbc-source-driver \
    --no-source-binary \
    --continuous-capture \
    --stream-batch-size 1 \
    --host-crop \
    --crop-offset 0 \
    --crop-width 161 \
    --crop-height 145 \
    --profile profiles/gbc_lcd.json
