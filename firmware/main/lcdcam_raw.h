#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define LCDCAM_RAW_MAX_REPORT_DESCRIPTORS 16U

typedef enum {
    LCDCAM_RAW_DE_SPL = 0,
    LCDCAM_RAW_DE_LP = 1,
    LCDCAM_RAW_DE_HIGH = 2,
} lcdcam_raw_de_source_t;

typedef enum {
    LCDCAM_RAW_START_IMMEDIATE = 0,
    LCDCAM_RAW_START_AFTER_SPS_RISING = 1,
    LCDCAM_RAW_START_AFTER_SPS_THEN_SPL_FALLING = 2,
} lcdcam_raw_start_mode_t;

typedef enum {
    LCDCAM_RAW_DATA_RG44 = 0,
    LCDCAM_RAW_DATA_RGB332 = 1,
    LCDCAM_RAW_DATA_RGB664 = 2,
    LCDCAM_RAW_DATA_RGB565 = 3,
} lcdcam_raw_data_mode_t;

typedef struct {
    uint8_t *buffer;
    size_t buffer_len;
    size_t received_size;
    size_t descriptor_count;
    size_t completed_descriptors;
    uint32_t descriptor_lengths[LCDCAM_RAW_MAX_REPORT_DESCRIPTORS];
    uint8_t descriptor_owners[LCDCAM_RAW_MAX_REPORT_DESCRIPTORS];
    uint8_t descriptor_suc_eof[LCDCAM_RAW_MAX_REPORT_DESCRIPTORS];
    uint8_t descriptor_err_eof[LCDCAM_RAW_MAX_REPORT_DESCRIPTORS];
    uint32_t timeout_ms;
    uint32_t checksum;
    uint32_t raw8_transitions;
    uint8_t min_value;
    uint8_t max_value;
    lcdcam_raw_de_source_t de_source;
    bool vsync_invert;
    bool de_invert;
    bool pclk_invert;
    bool byte_count_eof;
    bool vh_de_mode;
    int hsync_gpio;
    lcdcam_raw_data_mode_t data_mode;
    lcdcam_raw_start_mode_t start_mode;
    bool start_trigger_seen;
    bool eof_seen;
    bool done_seen;
    intptr_t eof_desc_addr;
    const char *failure_stage;
    esp_err_t failure_err;
} lcdcam_raw_result_t;

typedef void (*lcdcam_raw_frame_callback_t)(const lcdcam_raw_result_t *result,
                                            int64_t capture_us,
                                            void *user_data);

typedef struct {
    uint32_t requested_frames;
    uint32_t captured_frames;
    uint32_t failed_frames;
    int64_t elapsed_us;
    int64_t avg_capture_us;
    int64_t max_capture_us;
    esp_err_t last_esp_err;
    const char *failure_stage;
} lcdcam_raw_loop_stats_t;

typedef struct {
    uint32_t requested_chunks;
    uint32_t completed_chunks;
    uint32_t failed_rearms;
    uint32_t timeout_ms;
    uint32_t h_res;
    uint32_t v_res;
    uint32_t bytes_per_sample;
    uint32_t chunk_bytes;
    uint32_t checksum;
    int64_t elapsed_us;
    int64_t first_chunk_us;
    int64_t avg_chunk_us;
    int64_t max_chunk_us;
    bool start_trigger_seen;
    esp_err_t last_esp_err;
    const char *failure_stage;
    lcdcam_raw_data_mode_t data_mode;
} lcdcam_raw_rearm_stats_t;

esp_err_t lcdcam_raw_capture(lcdcam_raw_de_source_t de_source,
                             uint32_t h_res,
                             uint32_t v_res,
                             uint32_t timeout_ms,
                             bool vsync_invert,
                             bool de_invert,
                             bool pclk_invert,
                             bool byte_count_eof,
                             lcdcam_raw_start_mode_t start_mode,
                             bool vh_de_mode,
                             lcdcam_raw_data_mode_t data_mode,
                             lcdcam_raw_result_t *result);
esp_err_t lcdcam_raw_capture_loop(lcdcam_raw_de_source_t de_source,
                                  uint32_t h_res,
                                  uint32_t v_res,
                                  uint32_t timeout_ms,
                                  bool vsync_invert,
                                  bool de_invert,
                                  bool pclk_invert,
                                  bool byte_count_eof,
                                  lcdcam_raw_start_mode_t start_mode,
                                  bool vh_de_mode,
                                  lcdcam_raw_data_mode_t data_mode,
                                  uint32_t frame_count,
                                  lcdcam_raw_frame_callback_t frame_callback,
                                  void *callback_user_data,
                                  lcdcam_raw_loop_stats_t *stats);
esp_err_t lcdcam_raw_rearm_bench(lcdcam_raw_de_source_t de_source,
                                 uint32_t h_res,
                                 uint32_t v_res,
                                 uint32_t timeout_ms,
                                 bool vsync_invert,
                                 bool de_invert,
                                 bool pclk_invert,
                                 bool byte_count_eof,
                                 lcdcam_raw_start_mode_t start_mode,
                                 bool vh_de_mode,
                                 lcdcam_raw_data_mode_t data_mode,
                                 uint32_t chunk_count,
                                 lcdcam_raw_rearm_stats_t *stats);
void lcdcam_raw_result_free(lcdcam_raw_result_t *result);
esp_err_t lcdcam_raw_enter_safe_idle(void);
esp_err_t lcdcam_raw_enter_electrical_isolate(void);
