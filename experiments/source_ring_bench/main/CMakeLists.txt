set(PROBE_MAIN_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../firmware/main")

idf_component_register(
    SRCS
        "source_ring_bench_main.c"
        "${PROBE_MAIN_DIR}/gbc_lcd_source.c"
        "${PROBE_MAIN_DIR}/lcdcam_raw.c"
    INCLUDE_DIRS
        "."
        "${PROBE_MAIN_DIR}"
    REQUIRES
        esp_driver_cam
        esp_driver_gpio
        esp_hw_support
        esp_psram
        esp_timer
)
