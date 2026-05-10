#!/usr/bin/env bash

load_project_env() {
    local root_dir="${ROOT_DIR:-$(pwd)}"
    local env_file="${root_dir}/.env"
    if [[ -f "${env_file}" ]]; then
        set -a
        # shellcheck disable=SC1090
        source "${env_file}"
        set +a
    fi
}

resolve_idf_export() {
    if [[ -n "${IDF_EXPORT:-}" ]]; then
        printf '%s\n' "${IDF_EXPORT}"
        return
    fi

    if [[ -n "${IDF_PATH:-}" ]]; then
        printf '%s/export.sh\n' "${IDF_PATH}"
        return
    fi

    printf '%s/esp/v5.5/esp-idf/export.sh\n' "${HOME}"
}

source_idf() {
    local export_script
    load_project_env
    export_script="$(resolve_idf_export)"
    if [[ ! -f "${export_script}" ]]; then
        printf 'ESP-IDF export script not found: %s\n' "${export_script}" >&2
        printf 'Set IDF_EXPORT=/path/to/esp-idf/export.sh or IDF_PATH=/path/to/esp-idf\n' >&2
        return 2
    fi
    source "${export_script}" >/tmp/idf_export.log
}
