#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "lcdcam_raw.h"

typedef struct {
    uint32_t requested_frames;
    uint32_t captured_frames;
    uint32_t processed_frames;
    uint32_t output_frames;
    uint32_t failed_frames;
    uint32_t budget_miss_frames;
    uint32_t capture_width;
    uint32_t capture_height;
    uint32_t stream_width;
    uint32_t stream_height;
    uint32_t bytes_per_sample;
    uint32_t capture_frame_bytes;
    uint32_t emit_frame_bytes;
    uint32_t target_fps;
    uint32_t checksum;
    int64_t elapsed_us;
    int64_t avg_capture_us;
    int64_t max_capture_us;
    int64_t avg_process_us;
    int64_t max_process_us;
    int64_t avg_output_us;
    int64_t max_output_us;
    int64_t max_frame_us;
    esp_err_t last_esp_err;
    bool pclk_invert;
    bool persistent_capture;
    bool target_met;
    lcdcam_raw_data_mode_t data_mode;
} source_pipeline_bench_result_t;

esp_err_t source_pipeline_bench_run(uint32_t frame_count,
                                    uint32_t timeout_ms,
                                    lcdcam_raw_data_mode_t data_mode,
                                    bool pclk_invert,
                                    uint32_t target_fps,
                                    source_pipeline_bench_result_t *result);
esp_err_t source_pipeline_bench_run_persistent(uint32_t frame_count,
                                               uint32_t timeout_ms,
                                               lcdcam_raw_data_mode_t data_mode,
                                               bool pclk_invert,
                                               uint32_t target_fps,
                                               source_pipeline_bench_result_t *result);
