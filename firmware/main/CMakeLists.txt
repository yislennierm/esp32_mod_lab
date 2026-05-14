idf_component_register(
    SRCS
        "main.c"
        "usb_protocol.c"
        "destination_gpio_lab.c"
        "destination_spi_lcd.c"
        "gpio_sampler.c"
        "diagnostics.c"
        "dvp_probe.c"
        "gbc_lcd_source.c"
        "lcdcam_raw.c"
        "pipeline_bench.c"
        "ppa_srm_bench.c"
        "production_mirror.c"
        "source_pipeline_bench.c"
        "timing_analysis.c"
        "frame_capture.c"
        "signal_decoder.c"
        "ringbuffer.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_cam esp_driver_gpio esp_driver_isp esp_driver_pcnt esp_driver_ppa esp_driver_spi esp_hw_support esp_lcd esp_mm esp_psram esp_timer
)

if(DEFINED ENV{GBC_P4_SAFE_RECOVERY})
    target_compile_definitions(${COMPONENT_LIB} PRIVATE GBC_P4_SAFE_RECOVERY=1)
endif()

if(DEFINED ENV{GBC_P4_PRODUCTION_MIRROR})
    target_compile_definitions(${COMPONENT_LIB} PRIVATE GBC_P4_PRODUCTION_MIRROR=1)
endif()

if(DEFINED ENV{PRODUCTION_MIRROR_MODE})
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PRODUCTION_MIRROR_MODE=$ENV{PRODUCTION_MIRROR_MODE})
endif()

if(DEFINED ENV{PRODUCTION_CAPTURE_WINDOW_FRAMES})
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PRODUCTION_CAPTURE_WINDOW_FRAMES=$ENV{PRODUCTION_CAPTURE_WINDOW_FRAMES})
endif()

if(DEFINED ENV{DEST_SPI_LCD_PCLK_HZ})
    target_compile_definitions(${COMPONENT_LIB} PRIVATE DEST_SPI_LCD_PCLK_HZ=$ENV{DEST_SPI_LCD_PCLK_HZ})
endif()

if(DEFINED ENV{DEST_SPI_LCD_RAW_SPI})
    target_compile_definitions(${COMPONENT_LIB} PRIVATE DEST_SPI_LCD_RAW_SPI=$ENV{DEST_SPI_LCD_RAW_SPI})
endif()
