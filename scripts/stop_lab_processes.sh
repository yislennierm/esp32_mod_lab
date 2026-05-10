#!/usr/bin/env bash
set -euo pipefail

patterns=(
    "host/live_lcdcam_stream_viewer.py"
    "host/gbc_probe.py"
    "esptool.py"
    "idf.py"
    "npm run dev"
    "vite --host"
)

for pattern in "${patterns[@]}"; do
    while IFS= read -r pid; do
        [[ -z "${pid}" ]] && continue
        if [[ "${pid}" == "$$" ]]; then
            continue
        fi
        kill "${pid}" 2>/dev/null || true
    done < <(pgrep -f "${pattern}" || true)
done

printf 'requested stop for lab serial/backend/dev processes\n'
