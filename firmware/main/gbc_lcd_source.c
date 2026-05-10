#include "gbc_lcd_source.h"

#include "esp_timer.h"

static uint32_t bytes_per_sample(lcdcam_raw_data_mode_t data_mode)
{
    return (data_mode == LCDCAM_RAW_DATA_RGB664 || data_mode == LCDCAM_RAW_DATA_RGB565) ? 2U : 1U;
}

uint32_t gbc_lcd_source_emit_len(lcdcam_raw_data_mode_t data_mode)
{
    return GBC_LCD_SOURCE_STREAM_WIDTH * GBC_LCD_SOURCE_STREAM_HEIGHT * bytes_per_sample(data_mode);
}

void gbc_lcd_source_get_status(gbc_lcd_source_status_t *status)
{
    if (status == NULL) {
        return;
    }

    *status = (gbc_lcd_source_status_t) {
        .capture_width = GBC_LCD_SOURCE_CAPTURE_WIDTH,
        .capture_height = GBC_LCD_SOURCE_CAPTURE_HEIGHT,
        .stream_width = GBC_LCD_SOURCE_STREAM_WIDTH,
        .stream_height = GBC_LCD_SOURCE_STREAM_HEIGHT,
        .visible_width = GBC_LCD_SOURCE_VISIBLE_WIDTH,
        .visible_height = GBC_LCD_SOURCE_VISIBLE_HEIGHT,
        .default_timeout_ms = GBC_LCD_SOURCE_DEFAULT_TIMEOUT_MS,
        .emit_len_rgb565 = gbc_lcd_source_emit_len(LCDCAM_RAW_DATA_RGB565),
        .pclk_invert = false,
        .default_data_mode = LCDCAM_RAW_DATA_RGB565,
    };
}

esp_err_t gbc_lcd_source_capture_frame(uint32_t timeout_ms,
                                       lcdcam_raw_data_mode_t data_mode,
                                       bool pclk_invert,
                                       lcdcam_raw_result_t *result,
                                       int64_t *capture_us)
{
    if (timeout_ms == 0) {
        timeout_ms = GBC_LCD_SOURCE_DEFAULT_TIMEOUT_MS;
    }

    int64_t start_us = esp_timer_get_time();
    esp_err_t err = lcdcam_raw_capture(LCDCAM_RAW_DE_HIGH,
                                       GBC_LCD_SOURCE_CAPTURE_WIDTH,
                                       GBC_LCD_SOURCE_CAPTURE_HEIGHT,
                                       timeout_ms,
                                       false,
                                       false,
                                       pclk_invert,
                                       true,
                                       LCDCAM_RAW_START_AFTER_SPS_RISING,
                                       false,
                                       data_mode,
                                       result);
    if (capture_us != NULL) {
        *capture_us = esp_timer_get_time() - start_us;
    }
    return err;
}
