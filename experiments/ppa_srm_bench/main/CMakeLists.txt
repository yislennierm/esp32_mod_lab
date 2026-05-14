idf_component_register(
    SRCS "ppa_srm_bench_main.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_ppa esp_hw_support esp_mm esp_psram esp_timer
)
