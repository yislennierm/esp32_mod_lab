#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint32_t controller_count;
    uint32_t max_data_width;
    uint32_t configured_width;
    uint32_t h_res;
    uint32_t v_res;
    size_t frame_buffer_len;
    bool backup_buffer_disabled;
} dvp_probe_alloc_result_t;

typedef enum {
    DVP_PROBE_DE_SPL = 0,
    DVP_PROBE_DE_LP = 1,
} dvp_probe_de_source_t;

typedef enum {
    DVP_PROBE_SYNC_NONE = -1,
    DVP_PROBE_SYNC_SPL = 0,
    DVP_PROBE_SYNC_LP = 1,
} dvp_probe_sync_source_t;

typedef struct {
    uint8_t *buffer;
    size_t buffer_len;
    size_t received_size;
    uint32_t timeout_ms;
    uint32_t checksum;
    uint8_t min_value;
    uint8_t max_value;
    uint32_t lower6_transitions;
    uint32_t raw8_transitions;
    dvp_probe_de_source_t de_source;
    bool vsync_invert;
    bool de_invert;
    bool pclk_invert;
    bool byte_count_eof;
    const char *failure_stage;
    esp_err_t failure_err;
} dvp_probe_capture_result_t;

esp_err_t dvp_probe_allocate_raw8(dvp_probe_alloc_result_t *result);
esp_err_t dvp_probe_capture_raw8(dvp_probe_de_source_t de_source,
                                 uint32_t h_res,
                                 uint32_t v_res,
                                 uint32_t timeout_ms,
                                 bool vsync_invert,
                                 bool de_invert,
                                 bool pclk_invert,
                                 dvp_probe_capture_result_t *result);
esp_err_t dvp_probe_capture_raw8_byte_count(dvp_probe_de_source_t de_source,
                                            uint32_t h_res,
                                            uint32_t v_res,
                                            uint32_t timeout_ms,
                                            bool vsync_invert,
                                            bool de_invert,
                                            bool pclk_invert,
                                            dvp_probe_capture_result_t *result);
esp_err_t dvp_probe_capture_isp_raw8(dvp_probe_sync_source_t hsync_source,
                                     dvp_probe_sync_source_t de_source,
                                     uint32_t h_res,
                                     uint32_t v_res,
                                     uint32_t timeout_ms,
                                     bool hsync_invert,
                                     bool vsync_invert,
                                     bool de_invert,
                                     bool pclk_invert,
                                     dvp_probe_capture_result_t *result);
esp_err_t dvp_probe_capture_isp_rgb565(dvp_probe_sync_source_t hsync_source,
                                       dvp_probe_sync_source_t de_source,
                                       uint32_t h_res,
                                       uint32_t v_res,
                                       uint32_t timeout_ms,
                                       bool hsync_invert,
                                       bool vsync_invert,
                                       bool de_invert,
                                       bool pclk_invert,
                                       dvp_probe_capture_result_t *result);
void dvp_probe_capture_result_free(dvp_probe_capture_result_t *result);
