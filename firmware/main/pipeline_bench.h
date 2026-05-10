#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint32_t requested_frames;
    uint32_t generated_frames;
    uint32_t processed_frames;
    uint32_t output_frames;
    uint32_t dropped_frames;
    uint32_t width;
    uint32_t height;
    uint32_t bytes_per_pixel;
    uint32_t target_fps;
    uint32_t frame_bytes;
    uint32_t checksum;
    int64_t elapsed_us;
    int64_t avg_generate_us;
    int64_t max_generate_us;
    int64_t avg_process_us;
    int64_t max_process_us;
    int64_t avg_output_us;
    int64_t max_output_us;
    int64_t max_frame_us;
    bool target_met;
} pipeline_bench_result_t;

esp_err_t pipeline_bench_run(uint32_t frame_count,
                             uint32_t width,
                             uint32_t height,
                             uint32_t bytes_per_pixel,
                             uint32_t target_fps,
                             pipeline_bench_result_t *result);
