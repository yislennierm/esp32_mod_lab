#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

void destination_spi_lcd_handle_status(void);
void destination_spi_lcd_handle_init(void);
void destination_spi_lcd_handle_safe_off(void);
void destination_spi_lcd_handle_clear(const char *line);
void destination_spi_lcd_handle_clear565be(const char *line);
void destination_spi_lcd_handle_clear666(const char *line);
void destination_spi_lcd_handle_test_pattern(const char *line);
void destination_spi_lcd_handle_test_pattern565(const char *line);
void destination_spi_lcd_handle_set_madctl(const char *line);
void destination_spi_lcd_handle_signal_burst(const char *line);
esp_err_t destination_spi_lcd_init(void);
void destination_spi_lcd_safe_off(void);
esp_err_t destination_spi_lcd_clear_black(void);
size_t destination_spi_lcd_rgb666_frame_size(void);
esp_err_t destination_spi_lcd_generate_geometry_rgb666(uint8_t *frame, size_t frame_len, int label_digit);
esp_err_t destination_spi_lcd_draw_rgb666_frame(const uint8_t *frame, size_t frame_len);
esp_err_t destination_spi_lcd_draw_gbc_rgb565_1x(const uint8_t *source_rgb565,
                                                 size_t source_len,
                                                 uint32_t source_stride,
                                                 uint32_t visible_width,
                                                 uint32_t visible_height);
esp_err_t destination_spi_lcd_draw_gbc_rgb565_1x_no_clear(const uint8_t *source_rgb565,
                                                          size_t source_len,
                                                          uint32_t source_stride,
                                                          uint32_t visible_width,
                                                          uint32_t visible_height);
esp_err_t destination_spi_lcd_draw_gbc_rgb565_1x_shifted(const uint8_t *source_rgb565,
                                                         size_t source_len,
                                                         uint32_t source_stride,
                                                         uint32_t visible_width,
                                                         uint32_t visible_height,
                                                         int32_t linear_shift_pixels);
esp_err_t destination_spi_lcd_draw_gbc_rgb565_1x_shifted_no_clear(const uint8_t *source_rgb565,
                                                                  size_t source_len,
                                                                  uint32_t source_stride,
                                                                  uint32_t visible_width,
                                                                  uint32_t visible_height,
                                                                  int32_t linear_shift_pixels);
esp_err_t destination_spi_lcd_draw_gbc_rgb565_scaled2x(const uint8_t *source_rgb565,
                                                       size_t source_len,
                                                       uint32_t source_stride,
                                                       uint32_t visible_width,
                                                       uint32_t visible_height);
esp_err_t destination_spi_lcd_draw_gbc_rgb565_scaled2x_no_clear(const uint8_t *source_rgb565,
                                                                size_t source_len,
                                                                uint32_t source_stride,
                                                                uint32_t visible_width,
                                                                uint32_t visible_height);
esp_err_t destination_spi_lcd_draw_gbc_rgb565_panel565_1x_no_clear(const uint8_t *source_rgb565,
                                                                   size_t source_len,
                                                                   uint32_t source_stride,
                                                                   uint32_t visible_width,
                                                                   uint32_t visible_height);
esp_err_t destination_spi_lcd_draw_gbc_rgb565_panel565_1x_shifted_no_clear(const uint8_t *source_rgb565,
                                                                           size_t source_len,
                                                                           uint32_t source_stride,
                                                                           uint32_t visible_width,
                                                                           uint32_t visible_height,
                                                                           int32_t linear_shift_pixels);
esp_err_t destination_spi_lcd_draw_gbc_rgb565_panel565_scaled2x_no_clear(const uint8_t *source_rgb565,
                                                                         size_t source_len,
                                                                         uint32_t source_stride,
                                                                         uint32_t visible_width,
                                                                         uint32_t visible_height);
esp_err_t destination_spi_lcd_draw_fps_overlay(int x_start, int y_start, int64_t fps_x1000);
