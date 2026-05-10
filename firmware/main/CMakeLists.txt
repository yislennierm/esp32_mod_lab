idf_component_register(
    SRCS
        "main.c"
        "usb_protocol.c"
        "gpio_sampler.c"
        "diagnostics.c"
        "dvp_probe.c"
        "gbc_lcd_source.c"
        "lcdcam_raw.c"
        "pipeline_bench.c"
        "source_pipeline_bench.c"
        "timing_analysis.c"
        "frame_capture.c"
        "signal_decoder.c"
        "ringbuffer.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_cam esp_driver_gpio esp_driver_isp esp_driver_pcnt esp_hw_support esp_timer
)

if(DEFINED ENV{GBC_P4_SAFE_RECOVERY})
    target_compile_definitions(${COMPONENT_LIB} PRIVATE GBC_P4_SAFE_RECOVERY=1)
endif()
