#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "lcdcam_raw.h"

#define GBC_LCD_SOURCE_CAPTURE_WIDTH 192U
#define GBC_LCD_SOURCE_CAPTURE_HEIGHT 145U
#define GBC_LCD_SOURCE_STREAM_WIDTH 161U
#define GBC_LCD_SOURCE_STREAM_HEIGHT 145U
#define GBC_LCD_SOURCE_VISIBLE_WIDTH 160U
#define GBC_LCD_SOURCE_VISIBLE_HEIGHT 144U
#define GBC_LCD_SOURCE_DEFAULT_TIMEOUT_MS 300U

typedef struct {
    uint32_t capture_width;
    uint32_t capture_height;
    uint32_t stream_width;
    uint32_t stream_height;
    uint32_t visible_width;
    uint32_t visible_height;
    uint32_t default_timeout_ms;
    uint32_t emit_len_rgb565;
    bool pclk_invert;
    lcdcam_raw_data_mode_t default_data_mode;
} gbc_lcd_source_status_t;

void gbc_lcd_source_get_status(gbc_lcd_source_status_t *status);
esp_err_t gbc_lcd_source_capture_frame(uint32_t timeout_ms,
                                       lcdcam_raw_data_mode_t data_mode,
                                       bool pclk_invert,
                                       lcdcam_raw_result_t *result,
                                       int64_t *capture_us);
uint32_t gbc_lcd_source_emit_len(lcdcam_raw_data_mode_t data_mode);
