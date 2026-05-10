#include "source_pipeline_bench.h"

#include <string.h>

#include "esp_timer.h"

#include "gbc_lcd_source.h"

static uint32_t bytes_per_sample(lcdcam_raw_data_mode_t data_mode)
{
    return (data_mode == LCDCAM_RAW_DATA_RGB664 || data_mode == LCDCAM_RAW_DATA_RGB565) ? 2U : 1U;
}

static uint32_t process_frame_bytes(const uint8_t *buffer, size_t len, uint32_t frame_index)
{
    uint32_t acc = 0x9e3779b9U ^ frame_index;
    for (size_t i = 0; i < len; ++i) {
        acc ^= ((uint32_t)buffer[i] << ((i & 3U) * 8U));
        acc = (acc << 5) | (acc >> 27);
        acc += 0x7f4a7c15U;
    }
    return acc;
}

static uint32_t output_sink_sample(const uint8_t *buffer, size_t len, uint32_t frame_index)
{
    if (buffer == NULL || len == 0) {
        return frame_index;
    }

    uint32_t acc = 0xa5a50000U ^ frame_index;
    size_t stride = len / 32U;
    if (stride == 0) {
        stride = 1U;
    }
    for (size_t i = 0; i < len; i += stride) {
        acc = (acc * 33U) ^ buffer[i];
    }
    acc ^= buffer[len - 1U];
    return acc;
}

typedef struct {
    source_pipeline_bench_result_t *result;
    uint32_t frame_index;
    int64_t frame_budget_us;
    int64_t total_process_us;
    int64_t total_output_us;
} persistent_callback_context_t;

static void persistent_frame_callback(const lcdcam_raw_result_t *capture, int64_t capture_us, void *user_data)
{
    persistent_callback_context_t *ctx = (persistent_callback_context_t *)user_data;
    source_pipeline_bench_result_t *result = ctx->result;
    int64_t process_start_us = esp_timer_get_time();
    uint32_t processed_checksum = process_frame_bytes(capture->buffer, capture->received_size, ctx->frame_index);
    int64_t process_us = esp_timer_get_time() - process_start_us;
    ctx->total_process_us += process_us;
    if (process_us > result->max_process_us) {
        result->max_process_us = process_us;
    }
    result->processed_frames++;

    int64_t output_start_us = esp_timer_get_time();
    uint32_t output_checksum = output_sink_sample(capture->buffer, capture->received_size, ctx->frame_index);
    int64_t output_us = esp_timer_get_time() - output_start_us;
    ctx->total_output_us += output_us;
    if (output_us > result->max_output_us) {
        result->max_output_us = output_us;
    }
    result->output_frames++;
    result->checksum ^= capture->checksum ^ processed_checksum ^ output_checksum;

    int64_t frame_us = capture_us + process_us + output_us;
    if (frame_us > result->max_frame_us) {
        result->max_frame_us = frame_us;
    }
    if (ctx->frame_budget_us > 0 && frame_us > ctx->frame_budget_us) {
        result->budget_miss_frames++;
        result->target_met = false;
    }
    ctx->frame_index++;
}

esp_err_t source_pipeline_bench_run(uint32_t frame_count,
                                    uint32_t timeout_ms,
                                    lcdcam_raw_data_mode_t data_mode,
                                    bool pclk_invert,
                                    uint32_t target_fps,
                                    source_pipeline_bench_result_t *result)
{
    if (result == NULL ||
        frame_count == 0 ||
        frame_count > 512 ||
        timeout_ms == 0 ||
        timeout_ms > 5000 ||
        target_fps > 1000 ||
        data_mode != LCDCAM_RAW_DATA_RGB565) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(result, 0, sizeof(*result));
    result->requested_frames = frame_count;
    result->capture_width = GBC_LCD_SOURCE_CAPTURE_WIDTH;
    result->capture_height = GBC_LCD_SOURCE_CAPTURE_HEIGHT;
    result->stream_width = GBC_LCD_SOURCE_STREAM_WIDTH;
    result->stream_height = GBC_LCD_SOURCE_STREAM_HEIGHT;
    result->bytes_per_sample = bytes_per_sample(data_mode);
    result->capture_frame_bytes = result->capture_width * result->capture_height * result->bytes_per_sample;
    result->emit_frame_bytes = gbc_lcd_source_emit_len(data_mode);
    result->target_fps = target_fps;
    result->pclk_invert = pclk_invert;
    result->data_mode = data_mode;
    result->last_esp_err = ESP_OK;
    result->target_met = true;

    const int64_t frame_budget_us = target_fps > 0 ? 1000000LL / (int64_t)target_fps : 0;
    int64_t total_capture_us = 0;
    int64_t total_process_us = 0;
    int64_t total_output_us = 0;
    int64_t started_us = esp_timer_get_time();

    for (uint32_t i = 0; i < frame_count; ++i) {
        int64_t frame_start_us = esp_timer_get_time();
        lcdcam_raw_result_t capture = {0};
        int64_t capture_us = 0;
        esp_err_t err = gbc_lcd_source_capture_frame(timeout_ms,
                                                     data_mode,
                                                     pclk_invert,
                                                     &capture,
                                                     &capture_us);
        result->last_esp_err = err;
        total_capture_us += capture_us;
        if (capture_us > result->max_capture_us) {
            result->max_capture_us = capture_us;
        }

        if (err != ESP_OK) {
            result->failed_frames++;
            lcdcam_raw_result_free(&capture);
            continue;
        }

        result->captured_frames++;

        int64_t process_start_us = esp_timer_get_time();
        uint32_t processed_checksum = process_frame_bytes(capture.buffer, capture.received_size, i);
        int64_t process_us = esp_timer_get_time() - process_start_us;
        total_process_us += process_us;
        if (process_us > result->max_process_us) {
            result->max_process_us = process_us;
        }
        result->processed_frames++;

        int64_t output_start_us = esp_timer_get_time();
        uint32_t output_checksum = output_sink_sample(capture.buffer, capture.received_size, i);
        int64_t output_us = esp_timer_get_time() - output_start_us;
        total_output_us += output_us;
        if (output_us > result->max_output_us) {
            result->max_output_us = output_us;
        }
        result->output_frames++;
        result->checksum ^= capture.checksum ^ processed_checksum ^ output_checksum;

        lcdcam_raw_result_free(&capture);

        int64_t frame_us = esp_timer_get_time() - frame_start_us;
        if (frame_us > result->max_frame_us) {
            result->max_frame_us = frame_us;
        }
        if (frame_budget_us > 0 && frame_us > frame_budget_us) {
            result->budget_miss_frames++;
            result->target_met = false;
        }
    }

    result->elapsed_us = esp_timer_get_time() - started_us;
    result->avg_capture_us = frame_count > 0 ? total_capture_us / (int64_t)frame_count : 0;
    result->avg_process_us = frame_count > 0 ? total_process_us / (int64_t)frame_count : 0;
    result->avg_output_us = frame_count > 0 ? total_output_us / (int64_t)frame_count : 0;
    if (result->failed_frames > 0) {
        result->target_met = false;
    }

    return result->failed_frames == 0 ? ESP_OK : result->last_esp_err;
}

esp_err_t source_pipeline_bench_run_persistent(uint32_t frame_count,
                                               uint32_t timeout_ms,
                                               lcdcam_raw_data_mode_t data_mode,
                                               bool pclk_invert,
                                               uint32_t target_fps,
                                               source_pipeline_bench_result_t *result)
{
    if (result == NULL ||
        frame_count == 0 ||
        frame_count > 512 ||
        timeout_ms == 0 ||
        timeout_ms > 5000 ||
        target_fps > 1000 ||
        data_mode != LCDCAM_RAW_DATA_RGB565) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(result, 0, sizeof(*result));
    result->requested_frames = frame_count;
    result->capture_width = GBC_LCD_SOURCE_CAPTURE_WIDTH;
    result->capture_height = GBC_LCD_SOURCE_CAPTURE_HEIGHT;
    result->stream_width = GBC_LCD_SOURCE_STREAM_WIDTH;
    result->stream_height = GBC_LCD_SOURCE_STREAM_HEIGHT;
    result->bytes_per_sample = bytes_per_sample(data_mode);
    result->capture_frame_bytes = result->capture_width * result->capture_height * result->bytes_per_sample;
    result->emit_frame_bytes = gbc_lcd_source_emit_len(data_mode);
    result->target_fps = target_fps;
    result->pclk_invert = pclk_invert;
    result->persistent_capture = true;
    result->data_mode = data_mode;
    result->last_esp_err = ESP_OK;
    result->target_met = true;

    persistent_callback_context_t ctx = {
        .result = result,
        .frame_budget_us = target_fps > 0 ? 1000000LL / (int64_t)target_fps : 0,
    };
    lcdcam_raw_loop_stats_t stats = {0};
    esp_err_t err = lcdcam_raw_capture_loop(LCDCAM_RAW_DE_HIGH,
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
                                            frame_count,
                                            persistent_frame_callback,
                                            &ctx,
                                            &stats);

    result->captured_frames = stats.captured_frames;
    result->failed_frames = stats.failed_frames;
    result->elapsed_us = stats.elapsed_us;
    result->avg_capture_us = stats.avg_capture_us;
    result->max_capture_us = stats.max_capture_us;
    result->last_esp_err = stats.last_esp_err;
    result->avg_process_us = frame_count > 0 ? ctx.total_process_us / (int64_t)frame_count : 0;
    result->avg_output_us = frame_count > 0 ? ctx.total_output_us / (int64_t)frame_count : 0;
    if (result->failed_frames > 0) {
        result->target_met = false;
    }

    return err;
}
