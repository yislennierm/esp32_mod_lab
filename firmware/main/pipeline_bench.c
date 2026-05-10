#include "pipeline_bench.h"

#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PIPELINE_BENCH_MAX_FRAMES 10000U
#define PIPELINE_BENCH_MAX_WIDTH 640U
#define PIPELINE_BENCH_MAX_HEIGHT 480U
#define PIPELINE_BENCH_MAX_FRAME_BYTES (640U * 480U * 2U)

static void generate_frame(uint8_t *buffer, uint32_t frame_bytes, uint32_t frame_index)
{
    for (uint32_t i = 0; i < frame_bytes; ++i) {
        buffer[i] = (uint8_t)((i + frame_index) & 0xffU);
    }
}

static uint32_t process_frame(const uint8_t *src, uint8_t *dst, uint32_t frame_bytes, uint32_t frame_index)
{
    uint32_t checksum = 0;
    uint8_t mix = (uint8_t)((frame_index * 17U) & 0xffU);
    for (uint32_t i = 0; i < frame_bytes; ++i) {
        uint8_t value = (uint8_t)(src[i] ^ mix);
        dst[i] = value;
        checksum = (checksum + value) & 0xffffffffU;
    }
    return checksum;
}

static uint32_t output_sink(const uint8_t *buffer, uint32_t frame_bytes)
{
    uint32_t checksum = 0;
    uint32_t stride = frame_bytes / 64U;
    if (stride == 0) {
        stride = 1;
    }
    for (uint32_t i = 0; i < frame_bytes; i += stride) {
        checksum = (checksum + buffer[i]) & 0xffffffffU;
    }
    return checksum;
}

esp_err_t pipeline_bench_run(uint32_t frame_count,
                             uint32_t width,
                             uint32_t height,
                             uint32_t bytes_per_pixel,
                             uint32_t target_fps,
                             pipeline_bench_result_t *result)
{
    if (result == NULL ||
        frame_count == 0 ||
        frame_count > PIPELINE_BENCH_MAX_FRAMES ||
        width == 0 ||
        width > PIPELINE_BENCH_MAX_WIDTH ||
        height == 0 ||
        height > PIPELINE_BENCH_MAX_HEIGHT ||
        (bytes_per_pixel != 1U && bytes_per_pixel != 2U) ||
        target_fps > 1000U) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t frame_bytes = width * height * bytes_per_pixel;
    if (frame_bytes == 0 || frame_bytes > PIPELINE_BENCH_MAX_FRAME_BYTES) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t *source = heap_caps_malloc(frame_bytes, MALLOC_CAP_8BIT);
    uint8_t *processed = heap_caps_malloc(frame_bytes, MALLOC_CAP_8BIT);
    if (source == NULL || processed == NULL) {
        free(source);
        free(processed);
        return ESP_ERR_NO_MEM;
    }

    memset(result, 0, sizeof(*result));
    result->requested_frames = frame_count;
    result->width = width;
    result->height = height;
    result->bytes_per_pixel = bytes_per_pixel;
    result->target_fps = target_fps;
    result->frame_bytes = frame_bytes;
    result->target_met = true;

    int64_t period_us = target_fps == 0 ? 0 : 1000000LL / (int64_t)target_fps;
    int64_t bench_start_us = esp_timer_get_time();
    int64_t next_frame_due_us = bench_start_us;

    for (uint32_t frame = 0; frame < frame_count; ++frame) {
        int64_t frame_start_us = esp_timer_get_time();

        int64_t generate_start_us = frame_start_us;
        generate_frame(source, frame_bytes, frame);
        int64_t generate_us = esp_timer_get_time() - generate_start_us;

        int64_t process_start_us = esp_timer_get_time();
        uint32_t process_checksum = process_frame(source, processed, frame_bytes, frame);
        int64_t process_us = esp_timer_get_time() - process_start_us;

        int64_t output_start_us = esp_timer_get_time();
        uint32_t output_checksum = output_sink(processed, frame_bytes);
        int64_t output_us = esp_timer_get_time() - output_start_us;

        int64_t frame_us = esp_timer_get_time() - frame_start_us;
        if (period_us > 0 && frame_us > period_us) {
            result->target_met = false;
            result->dropped_frames++;
        }

        result->generated_frames++;
        result->processed_frames++;
        result->output_frames++;
        result->checksum ^= process_checksum ^ output_checksum;
        result->avg_generate_us += generate_us;
        result->avg_process_us += process_us;
        result->avg_output_us += output_us;
        if (generate_us > result->max_generate_us) {
            result->max_generate_us = generate_us;
        }
        if (process_us > result->max_process_us) {
            result->max_process_us = process_us;
        }
        if (output_us > result->max_output_us) {
            result->max_output_us = output_us;
        }
        if (frame_us > result->max_frame_us) {
            result->max_frame_us = frame_us;
        }

        if (period_us > 0) {
            next_frame_due_us += period_us;
            int64_t now_us = esp_timer_get_time();
            int64_t delay_us = next_frame_due_us - now_us;
            if (delay_us > 1000) {
                vTaskDelay(pdMS_TO_TICKS((uint32_t)(delay_us / 1000)));
            }
        }
    }

    result->elapsed_us = esp_timer_get_time() - bench_start_us;
    if (frame_count > 0) {
        result->avg_generate_us /= (int64_t)frame_count;
        result->avg_process_us /= (int64_t)frame_count;
        result->avg_output_us /= (int64_t)frame_count;
    }

    free(source);
    free(processed);
    return ESP_OK;
}
