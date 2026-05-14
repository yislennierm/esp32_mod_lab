#include "production_mirror.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/ppa.h"
#include "esp_cache.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_private/esp_cache_private.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "destination_spi_lcd.h"
#include "gbc_lcd_source.h"
#include "lcdcam_raw.h"
#include "pinmap_current.h"

static const char *TAG = "production_mirror";

#define PRODUCTION_FRAME_SLOTS 4U
#ifndef PRODUCTION_CAPTURE_WINDOW_FRAMES
#define PRODUCTION_CAPTURE_WINDOW_FRAMES 120U
#endif
#ifndef PRODUCTION_SKIP_SUSPECT_ROWS
#define PRODUCTION_SKIP_SUSPECT_ROWS 0
#endif
#ifndef PRODUCTION_ENABLE_ROW_DIAGNOSTICS
#define PRODUCTION_ENABLE_ROW_DIAGNOSTICS 0
#endif
#ifndef PRODUCTION_FRAME_SLOT_CAPS
#define PRODUCTION_FRAME_SLOT_CAPS (MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT)
#endif

#define PRODUCTION_MIRROR_MODE_SYNC_EACH_FRAME 1
#define PRODUCTION_MIRROR_MODE_OVERLAP 2
#define PRODUCTION_MIRROR_MODE_PPA_2X_SYNC 3
#define PRODUCTION_MIRROR_MODE_PPA_1X_SYNC 4
#define PRODUCTION_MIRROR_MODE_PPA_RING_2X 5
#define PRODUCTION_MIRROR_MODE_PPA_RING_1X 6
#define PRODUCTION_MIRROR_MODE_PPA_RING_STREAM_1X 7
#define PRODUCTION_MIRROR_MODE_FREEZE_FRAME 8
#define PRODUCTION_MIRROR_MODE_RING_DIRECT_1X 9
#define PRODUCTION_MIRROR_MODE_DESTINATION_PATTERN 10
#define PRODUCTION_MIRROR_MODE_GBC_WINDOW_PATTERN 11

#ifndef PRODUCTION_MIRROR_MODE
#define PRODUCTION_MIRROR_MODE PRODUCTION_MIRROR_MODE_SYNC_EACH_FRAME
#endif

#if PRODUCTION_MIRROR_MODE != PRODUCTION_MIRROR_MODE_DESTINATION_PATTERN && \
    PRODUCTION_MIRROR_MODE != PRODUCTION_MIRROR_MODE_GBC_WINDOW_PATTERN
static esp_err_t draw_gbc_visible_rgb565(const uint8_t *source, size_t source_len)
{
    return destination_spi_lcd_draw_gbc_rgb565_1x_shifted_no_clear(source,
                                                                  source_len,
                                                                  GBC_LCD_SOURCE_STREAM_WIDTH,
                                                                  GBC_LCD_SOURCE_VISIBLE_WIDTH,
                                                                  GBC_LCD_SOURCE_VISIBLE_HEIGHT,
                                                                  GBC_LCD_SOURCE_VISIBLE_LINEAR_SHIFT_PIXELS);
}
#endif

#if PRODUCTION_MIRROR_MODE == PRODUCTION_MIRROR_MODE_OVERLAP || \
    PRODUCTION_MIRROR_MODE == PRODUCTION_MIRROR_MODE_RING_DIRECT_1X
#if PRODUCTION_MIRROR_MODE == PRODUCTION_MIRROR_MODE_RING_DIRECT_1X
#define PRODUCTION_OVERLAP_MODE_NAME "production_mirror_ring_direct_1x"
#else
#define PRODUCTION_OVERLAP_MODE_NAME "production_mirror_overlap"
#endif

typedef struct {
    uint8_t *buffer;
    size_t received_size;
} production_frame_slot_t;

typedef struct {
    QueueHandle_t free_slots;
    QueueHandle_t filled_slots;
    production_frame_slot_t slots[PRODUCTION_FRAME_SLOTS];
    size_t slot_len;
    uint32_t captured_frames;
    uint32_t dropped_frames;
    uint32_t displayed_frames;
    uint32_t suspect_frames;
    uint32_t suspect_rows;
    uint32_t skipped_suspect_frames;
    uint32_t capture_failures;
    uint32_t draw_failures;
    int64_t total_capture_us;
    int64_t total_draw_us;
    int64_t max_capture_us;
    int64_t max_draw_us;
    int64_t window_start_us;
    int64_t latest_fps_x1000;
    esp_err_t last_capture_err;
    esp_err_t last_draw_err;
} production_pipeline_t;

static production_pipeline_t s_pipeline;

static void reset_metrics_window(production_pipeline_t *pipeline, int64_t now_us)
{
    pipeline->captured_frames = 0;
    pipeline->dropped_frames = 0;
    pipeline->displayed_frames = 0;
    pipeline->suspect_frames = 0;
    pipeline->suspect_rows = 0;
    pipeline->skipped_suspect_frames = 0;
    pipeline->capture_failures = 0;
    pipeline->draw_failures = 0;
    pipeline->total_capture_us = 0;
    pipeline->total_draw_us = 0;
    pipeline->max_capture_us = 0;
    pipeline->max_draw_us = 0;
    pipeline->window_start_us = now_us;
    pipeline->last_capture_err = ESP_OK;
    pipeline->last_draw_err = ESP_OK;
}

static bool production_pipeline_init(production_pipeline_t *pipeline)
{
    memset(pipeline, 0, sizeof(*pipeline));
    pipeline->slot_len = GBC_LCD_SOURCE_CAPTURE_WIDTH * GBC_LCD_SOURCE_CAPTURE_HEIGHT * 2U;
    pipeline->free_slots = xQueueCreate(PRODUCTION_FRAME_SLOTS, sizeof(uint32_t));
    pipeline->filled_slots = xQueueCreate(PRODUCTION_FRAME_SLOTS, sizeof(uint32_t));
    if (pipeline->free_slots == NULL || pipeline->filled_slots == NULL) {
        return false;
    }

    for (uint32_t i = 0; i < PRODUCTION_FRAME_SLOTS; ++i) {
        pipeline->slots[i].buffer = heap_caps_malloc(pipeline->slot_len, PRODUCTION_FRAME_SLOT_CAPS);
        if (pipeline->slots[i].buffer == NULL) {
            return false;
        }
        pipeline->slots[i].received_size = 0;
        if (xQueueSend(pipeline->free_slots, &i, 0) != pdTRUE) {
            return false;
        }
    }
    reset_metrics_window(pipeline, esp_timer_get_time());
    return true;
}

#if PRODUCTION_ENABLE_ROW_DIAGNOSTICS
static uint32_t count_mostly_black_visible_rows(const uint8_t *source, size_t source_len)
{
    if (source == NULL || source_len < 2U) {
        return GBC_LCD_SOURCE_VISIBLE_HEIGHT;
    }

    const int32_t source_pixels = (int32_t)(source_len / 2U);
    uint32_t suspect_rows = 0;
    for (uint32_t y = 0; y < GBC_LCD_SOURCE_VISIBLE_HEIGHT; ++y) {
        uint32_t black_pixels = 0;
        for (uint32_t x = 0; x < GBC_LCD_SOURCE_VISIBLE_WIDTH; ++x) {
            const int32_t src_pixel = (int32_t)(y * GBC_LCD_SOURCE_STREAM_WIDTH + x) +
                                      GBC_LCD_SOURCE_VISIBLE_LINEAR_SHIFT_PIXELS;
            if (src_pixel < 0 || src_pixel >= source_pixels) {
                continue;
            }
            const size_t src = (size_t)src_pixel * 2U;
            const uint16_t pixel = (uint16_t)source[src] | ((uint16_t)source[src + 1U] << 8);
            if (pixel == 0U) {
                ++black_pixels;
            }
        }
        if (black_pixels >= (GBC_LCD_SOURCE_VISIBLE_WIDTH - 2U)) {
            ++suspect_rows;
        }
    }
    return suspect_rows;
}
#endif

static void draw_frame_callback(const lcdcam_raw_result_t *result, int64_t capture_us, void *user_data)
{
    production_pipeline_t *pipeline = (production_pipeline_t *)user_data;
    uint32_t slot_index = 0;
    if (xQueueReceive(pipeline->free_slots, &slot_index, 0) != pdTRUE ||
        slot_index >= PRODUCTION_FRAME_SLOTS) {
        ++pipeline->dropped_frames;
        return;
    }

    production_frame_slot_t *slot = &pipeline->slots[slot_index];
    const size_t copy_len = result->buffer_len < pipeline->slot_len ? result->buffer_len : pipeline->slot_len;
    memcpy(slot->buffer, result->buffer, copy_len);
    slot->received_size = result->received_size < copy_len ? result->received_size : copy_len;

    ++pipeline->captured_frames;
    pipeline->total_capture_us += capture_us;
    if (capture_us > pipeline->max_capture_us) {
        pipeline->max_capture_us = capture_us;
    }

    if (xQueueSend(pipeline->filled_slots, &slot_index, 0) != pdTRUE) {
        ++pipeline->dropped_frames;
        (void)xQueueSend(pipeline->free_slots, &slot_index, 0);
    }
}

static void draw_task(void *arg)
{
    production_pipeline_t *pipeline = (production_pipeline_t *)arg;
    while (true) {
        uint32_t slot_index = 0;
        if (xQueueReceive(pipeline->filled_slots, &slot_index, portMAX_DELAY) != pdTRUE ||
            slot_index >= PRODUCTION_FRAME_SLOTS) {
            continue;
        }

        production_frame_slot_t *slot = &pipeline->slots[slot_index];
#if PRODUCTION_ENABLE_ROW_DIAGNOSTICS
        const uint32_t suspect_rows = count_mostly_black_visible_rows(slot->buffer, slot->received_size);
        if (suspect_rows > 0U) {
            ++pipeline->suspect_frames;
            pipeline->suspect_rows += suspect_rows;
#if PRODUCTION_SKIP_SUSPECT_ROWS
            ++pipeline->skipped_suspect_frames;
            (void)xQueueSend(pipeline->free_slots, &slot_index, portMAX_DELAY);
            continue;
#endif
        }
#endif

        const int64_t draw_start_us = esp_timer_get_time();
        esp_err_t draw_err = draw_gbc_visible_rgb565(slot->buffer, slot->received_size);
        const int64_t draw_us = esp_timer_get_time() - draw_start_us;

        pipeline->total_draw_us += draw_us;
        if (draw_us > pipeline->max_draw_us) {
            pipeline->max_draw_us = draw_us;
        }
        if (draw_err == ESP_OK) {
            ++pipeline->displayed_frames;
        } else {
            ++pipeline->draw_failures;
            pipeline->last_draw_err = draw_err;
        }

        (void)xQueueSend(pipeline->free_slots, &slot_index, portMAX_DELAY);
        vTaskDelay(1);

        const int64_t now_us = esp_timer_get_time();
        const int64_t elapsed_us = now_us - pipeline->window_start_us;
        if (elapsed_us >= 1000000) {
            const uint32_t displayed = pipeline->displayed_frames;
            const uint32_t captured = pipeline->captured_frames;
            const int64_t fps_x1000 = elapsed_us > 0 ? ((int64_t)displayed * 1000000000LL) / elapsed_us : 0;
            pipeline->latest_fps_x1000 = fps_x1000;
            printf("{\"mode\":\"%s\",\"displayed\":%" PRIu32
                   ",\"captured\":%" PRIu32 ",\"fps_x1000\":%" PRId64
                   ",\"avg_capture_us\":%" PRId64 ",\"avg_draw_us\":%" PRId64
                   ",\"max_capture_us\":%" PRId64 ",\"max_draw_us\":%" PRId64
                   ",\"dropped_frames\":%" PRIu32 ",\"capture_failures\":%" PRIu32
                   ",\"draw_failures\":%" PRIu32
                   ",\"suspect_frames\":%" PRIu32 ",\"suspect_rows\":%" PRIu32
                   ",\"skipped_suspect_frames\":%" PRIu32
                   ",\"source_stream_width\":%u,\"source_visible_shift_pixels\":%d"
                   ",\"capture_error\":\"%s\",\"draw_error\":\"%s\"}\n",
                   PRODUCTION_OVERLAP_MODE_NAME,
                   displayed,
                   captured,
                   fps_x1000,
                   captured > 0 ? pipeline->total_capture_us / captured : 0,
                   displayed > 0 ? pipeline->total_draw_us / displayed : 0,
                   pipeline->max_capture_us,
                   pipeline->max_draw_us,
                   pipeline->dropped_frames,
                   pipeline->capture_failures,
                   pipeline->draw_failures,
                   pipeline->suspect_frames,
                   pipeline->suspect_rows,
                   pipeline->skipped_suspect_frames,
                   (unsigned)GBC_LCD_SOURCE_STREAM_WIDTH,
                   (int)GBC_LCD_SOURCE_VISIBLE_LINEAR_SHIFT_PIXELS,
                   pipeline->last_capture_err == ESP_OK ? "none" : esp_err_to_name(pipeline->last_capture_err),
                   pipeline->last_draw_err == ESP_OK ? "none" : esp_err_to_name(pipeline->last_draw_err));
            reset_metrics_window(pipeline, now_us);
        }
    }
}

static void record_capture_window_result(production_pipeline_t *pipeline, const lcdcam_raw_loop_stats_t *stats, esp_err_t err)
{
    if (stats->failed_frames > 0 || err != ESP_OK) {
        pipeline->capture_failures += stats->failed_frames > 0 ? stats->failed_frames : 1U;
        pipeline->last_capture_err = err;
    }
}

static void start_draw_task_once(production_pipeline_t *pipeline)
{
    static bool task_started;
    if (!task_started) {
        xTaskCreatePinnedToCore(draw_task, "production_draw", 8192, pipeline, 8, NULL, 1);
        task_started = true;
    }
}

static esp_err_t run_capture_window(production_pipeline_t *pipeline)
{
    lcdcam_raw_loop_stats_t stats = {0};
    esp_err_t err = lcdcam_raw_capture_loop(LCDCAM_RAW_DE_HIGH,
                                            GBC_LCD_SOURCE_CAPTURE_WIDTH,
                                            GBC_LCD_SOURCE_CAPTURE_HEIGHT,
                                            GBC_LCD_SOURCE_DEFAULT_TIMEOUT_MS,
                                            false,
                                            false,
                                            false,
                                            true,
                                            LCDCAM_RAW_START_AFTER_SPS_RISING,
                                            false,
                                            LCDCAM_RAW_DATA_RGB565,
                                            PRODUCTION_CAPTURE_WINDOW_FRAMES,
                                            draw_frame_callback,
                                            pipeline,
                                            &stats);
    record_capture_window_result(pipeline, &stats, err);
    return err;
}

#if PRODUCTION_MIRROR_MODE == PRODUCTION_MIRROR_MODE_RING_DIRECT_1X
static void record_ring_capture_window_result(production_pipeline_t *pipeline, const lcdcam_raw_ring_stats_t *stats, esp_err_t err)
{
    if (stats->ring_rearm_failures > 0 || stats->unknown_eof_desc > 0 || err != ESP_OK) {
        pipeline->capture_failures += (stats->ring_rearm_failures > 0 || stats->unknown_eof_desc > 0) ? 1U : 0U;
        pipeline->last_capture_err = err;
    }
}

static esp_err_t run_ring_capture_window(production_pipeline_t *pipeline)
{
    lcdcam_raw_ring_stats_t stats = {0};
    esp_err_t err = lcdcam_raw_ring_capture_loop(LCDCAM_RAW_DE_HIGH,
                                                 GBC_LCD_SOURCE_CAPTURE_WIDTH,
                                                 GBC_LCD_SOURCE_CAPTURE_HEIGHT,
                                                 GBC_LCD_SOURCE_DEFAULT_TIMEOUT_MS,
                                                 false,
                                                 false,
                                                 false,
                                                 true,
                                                 LCDCAM_RAW_START_AFTER_SPS_RISING,
                                                 false,
                                                 LCDCAM_RAW_DATA_RGB565,
                                                 PRODUCTION_CAPTURE_WINDOW_FRAMES,
                                                 draw_frame_callback,
                                                 pipeline,
                                                 &stats);
    record_ring_capture_window_result(pipeline, &stats, err);
    return err;
}
#endif
#endif

#if PRODUCTION_MIRROR_MODE == PRODUCTION_MIRROR_MODE_PPA_2X_SYNC || \
    PRODUCTION_MIRROR_MODE == PRODUCTION_MIRROR_MODE_PPA_1X_SYNC || \
    PRODUCTION_MIRROR_MODE == PRODUCTION_MIRROR_MODE_PPA_RING_2X || \
    PRODUCTION_MIRROR_MODE == PRODUCTION_MIRROR_MODE_PPA_RING_1X || \
    PRODUCTION_MIRROR_MODE == PRODUCTION_MIRROR_MODE_PPA_RING_STREAM_1X
#define PPA_SOURCE_WIDTH GBC_LCD_SOURCE_VISIBLE_WIDTH
#define PPA_SOURCE_HEIGHT GBC_LCD_SOURCE_VISIBLE_HEIGHT
#if PRODUCTION_MIRROR_MODE == PRODUCTION_MIRROR_MODE_PPA_2X_SYNC || \
    PRODUCTION_MIRROR_MODE == PRODUCTION_MIRROR_MODE_PPA_RING_2X
#define PPA_DEST_WIDTH (GBC_LCD_SOURCE_VISIBLE_WIDTH * 2U)
#define PPA_DEST_HEIGHT (GBC_LCD_SOURCE_VISIBLE_HEIGHT * 2U)
#define PPA_SCALE_X 2.0f
#define PPA_SCALE_Y 2.0f
#if PRODUCTION_MIRROR_MODE == PRODUCTION_MIRROR_MODE_PPA_RING_2X
#define PPA_PRODUCTION_MODE_NAME "production_mirror_ring_ppa_2x"
#else
#define PPA_PRODUCTION_MODE_NAME "production_mirror_ppa_2x_sync"
#endif
#else
#define PPA_DEST_WIDTH GBC_LCD_SOURCE_VISIBLE_WIDTH
#define PPA_DEST_HEIGHT GBC_LCD_SOURCE_VISIBLE_HEIGHT
#define PPA_SCALE_X 1.0f
#define PPA_SCALE_Y 1.0f
#if PRODUCTION_MIRROR_MODE == PRODUCTION_MIRROR_MODE_PPA_RING_STREAM_1X
#define PPA_PRODUCTION_MODE_NAME "production_mirror_ring_stream_ppa_1x"
#elif PRODUCTION_MIRROR_MODE == PRODUCTION_MIRROR_MODE_PPA_RING_1X
#define PPA_PRODUCTION_MODE_NAME "production_mirror_ring_ppa_1x"
#else
#define PPA_PRODUCTION_MODE_NAME "production_mirror_ppa_1x_sync"
#endif
#endif
#if PRODUCTION_MIRROR_MODE == PRODUCTION_MIRROR_MODE_PPA_RING_STREAM_1X
#define PPA_RING_CAPTURE_WIDTH GBC_LCD_SOURCE_STREAM_WIDTH
#define PPA_RING_CAPTURE_HEIGHT GBC_LCD_SOURCE_STREAM_HEIGHT
#else
#define PPA_RING_CAPTURE_WIDTH PPA_SOURCE_WIDTH
#define PPA_RING_CAPTURE_HEIGHT PPA_SOURCE_HEIGHT
#endif
#define RGB565_BYTES_PER_PIXEL 2U

typedef struct {
    ppa_client_handle_t client;
    uint16_t *source_rgb565;
    uint16_t *scaled_rgb565;
    size_t source_bytes;
    size_t scaled_bytes;
} production_ppa_context_t;

static esp_err_t production_ppa_init(production_ppa_context_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->source_bytes = PPA_SOURCE_WIDTH * PPA_SOURCE_HEIGHT * RGB565_BYTES_PER_PIXEL;
    ctx->scaled_bytes = PPA_DEST_WIDTH * PPA_DEST_HEIGHT * RGB565_BYTES_PER_PIXEL;

    size_t alignment = 0;
    esp_err_t err = esp_cache_get_alignment(MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA, &alignment);
    if (err != ESP_OK) {
        return err;
    }
    ctx->source_rgb565 = heap_caps_aligned_calloc(alignment,
                                                  1,
                                                  ctx->source_bytes,
                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    ctx->scaled_rgb565 = heap_caps_aligned_calloc(alignment,
                                                  1,
                                                  ctx->scaled_bytes,
                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (ctx->source_rgb565 == NULL || ctx->scaled_rgb565 == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ppa_client_config_t client_config = {
        .oper_type = PPA_OPERATION_SRM,
        .max_pending_trans_num = 1,
        .data_burst_length = PPA_DATA_BURST_LENGTH_128,
    };
    return ppa_register_client(&client_config, &ctx->client);
}

#if PRODUCTION_MIRROR_MODE == PRODUCTION_MIRROR_MODE_PPA_2X_SYNC || \
    PRODUCTION_MIRROR_MODE == PRODUCTION_MIRROR_MODE_PPA_1X_SYNC
static esp_err_t copy_visible_to_contiguous_rgb565(const uint8_t *source,
                                                   size_t source_len,
                                                   uint16_t *dest)
{
    return gbc_lcd_source_copy_visible_rgb565(source,
                                             source_len,
                                             (uint8_t *)dest,
                                             gbc_lcd_source_visible_len(LCDCAM_RAW_DATA_RGB565));
}
#endif

static esp_err_t production_ppa_process_from(production_ppa_context_t *ctx, uint16_t *source_rgb565)
{
    ppa_srm_oper_config_t config = {
        .in = {
            .buffer = source_rgb565,
            .pic_w = PPA_SOURCE_WIDTH,
            .pic_h = PPA_SOURCE_HEIGHT,
            .block_w = PPA_SOURCE_WIDTH,
            .block_h = PPA_SOURCE_HEIGHT,
            .block_offset_x = 0,
            .block_offset_y = 0,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .out = {
            .buffer = ctx->scaled_rgb565,
            .buffer_size = ctx->scaled_bytes,
            .pic_w = PPA_DEST_WIDTH,
            .pic_h = PPA_DEST_HEIGHT,
            .block_offset_x = 0,
            .block_offset_y = 0,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
        .scale_x = PPA_SCALE_X,
        .scale_y = PPA_SCALE_Y,
        .mirror_x = false,
        .mirror_y = false,
        .rgb_swap = false,
        .byte_swap = false,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };

    esp_err_t err = esp_cache_msync(source_rgb565, ctx->source_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    if (err != ESP_OK) {
        return err;
    }
    err = ppa_do_scale_rotate_mirror(ctx->client, &config);
    if (err != ESP_OK) {
        return err;
    }
    return esp_cache_msync(ctx->scaled_rgb565, ctx->scaled_bytes, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
}

#if PRODUCTION_MIRROR_MODE == PRODUCTION_MIRROR_MODE_PPA_2X_SYNC || \
    PRODUCTION_MIRROR_MODE == PRODUCTION_MIRROR_MODE_PPA_1X_SYNC
static esp_err_t production_ppa_process(production_ppa_context_t *ctx)
{
    return production_ppa_process_from(ctx, ctx->source_rgb565);
}
#endif
#endif

#if PRODUCTION_MIRROR_MODE == PRODUCTION_MIRROR_MODE_PPA_RING_2X || \
    PRODUCTION_MIRROR_MODE == PRODUCTION_MIRROR_MODE_PPA_RING_1X || \
    PRODUCTION_MIRROR_MODE == PRODUCTION_MIRROR_MODE_PPA_RING_STREAM_1X
typedef struct {
    production_ppa_context_t ppa;
    QueueHandle_t free_slots;
    QueueHandle_t filled_slots;
    uint16_t *source_slots[PRODUCTION_FRAME_SLOTS];
    size_t source_bytes;
    uint32_t capture_width;
    uint32_t capture_height;
    uint32_t captured_frames;
    uint32_t copied_frames;
    uint32_t displayed_frames;
    uint32_t dropped_frames;
    uint32_t capture_failures;
    uint32_t ppa_failures;
    uint32_t draw_failures;
    int64_t total_capture_us;
    int64_t total_copy_us;
    int64_t total_ppa_us;
    int64_t total_draw_us;
    int64_t max_capture_us;
    int64_t max_copy_us;
    int64_t max_ppa_us;
    int64_t max_draw_us;
    int64_t window_start_us;
    esp_err_t last_capture_err;
    esp_err_t last_ppa_err;
    esp_err_t last_draw_err;
} production_ring_ppa_pipeline_t;

static production_ring_ppa_pipeline_t s_ring_ppa;

static void reset_ring_ppa_metrics(production_ring_ppa_pipeline_t *pipeline, int64_t now_us)
{
    pipeline->captured_frames = 0;
    pipeline->copied_frames = 0;
    pipeline->displayed_frames = 0;
    pipeline->dropped_frames = 0;
    pipeline->capture_failures = 0;
    pipeline->ppa_failures = 0;
    pipeline->draw_failures = 0;
    pipeline->total_capture_us = 0;
    pipeline->total_copy_us = 0;
    pipeline->total_ppa_us = 0;
    pipeline->total_draw_us = 0;
    pipeline->max_capture_us = 0;
    pipeline->max_copy_us = 0;
    pipeline->max_ppa_us = 0;
    pipeline->max_draw_us = 0;
    pipeline->window_start_us = now_us;
    pipeline->last_capture_err = ESP_OK;
    pipeline->last_ppa_err = ESP_OK;
    pipeline->last_draw_err = ESP_OK;
}

static bool production_ring_ppa_init(production_ring_ppa_pipeline_t *pipeline)
{
    memset(pipeline, 0, sizeof(*pipeline));
    if (production_ppa_init(&pipeline->ppa) != ESP_OK) {
        return false;
    }
    pipeline->source_bytes = PPA_SOURCE_WIDTH * PPA_SOURCE_HEIGHT * RGB565_BYTES_PER_PIXEL;
    pipeline->capture_width = PPA_RING_CAPTURE_WIDTH;
    pipeline->capture_height = PPA_RING_CAPTURE_HEIGHT;
    pipeline->free_slots = xQueueCreate(PRODUCTION_FRAME_SLOTS, sizeof(uint32_t));
    pipeline->filled_slots = xQueueCreate(PRODUCTION_FRAME_SLOTS, sizeof(uint32_t));
    if (pipeline->free_slots == NULL || pipeline->filled_slots == NULL) {
        return false;
    }

    size_t alignment = 0;
    if (esp_cache_get_alignment(MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA, &alignment) != ESP_OK) {
        return false;
    }
    for (uint32_t i = 0; i < PRODUCTION_FRAME_SLOTS; ++i) {
        pipeline->source_slots[i] = heap_caps_aligned_calloc(alignment,
                                                             1,
                                                             pipeline->source_bytes,
                                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
        if (pipeline->source_slots[i] == NULL) {
            return false;
        }
        if (xQueueSend(pipeline->free_slots, &i, 0) != pdTRUE) {
            return false;
        }
    }
    reset_ring_ppa_metrics(pipeline, esp_timer_get_time());
    return true;
}

static void ring_ppa_capture_callback(const lcdcam_raw_result_t *capture, int64_t capture_us, void *user_data)
{
    production_ring_ppa_pipeline_t *pipeline = (production_ring_ppa_pipeline_t *)user_data;
    ++pipeline->captured_frames;
    pipeline->total_capture_us += capture_us;
    if (capture_us > pipeline->max_capture_us) {
        pipeline->max_capture_us = capture_us;
    }

    uint32_t slot = 0;
    if (xQueueReceive(pipeline->free_slots, &slot, 0) != pdTRUE || slot >= PRODUCTION_FRAME_SLOTS) {
        ++pipeline->dropped_frames;
        return;
    }

    const int64_t copy_start_us = esp_timer_get_time();
    if (pipeline->capture_width == PPA_SOURCE_WIDTH && pipeline->capture_height == PPA_SOURCE_HEIGHT) {
        memcpy(pipeline->source_slots[slot], capture->buffer, pipeline->source_bytes);
    } else {
        for (uint32_t y = 0; y < PPA_SOURCE_HEIGHT; ++y) {
            const uint8_t *src = capture->buffer + ((size_t)y * (size_t)pipeline->capture_width * RGB565_BYTES_PER_PIXEL);
            uint8_t *dst = (uint8_t *)(pipeline->source_slots[slot] + ((size_t)y * PPA_SOURCE_WIDTH));
            memcpy(dst, src, PPA_SOURCE_WIDTH * RGB565_BYTES_PER_PIXEL);
        }
    }
    const int64_t copy_us = esp_timer_get_time() - copy_start_us;
    pipeline->total_copy_us += copy_us;
    if (copy_us > pipeline->max_copy_us) {
        pipeline->max_copy_us = copy_us;
    }
    ++pipeline->copied_frames;

    if (xQueueSend(pipeline->filled_slots, &slot, 0) != pdTRUE) {
        ++pipeline->dropped_frames;
        (void)xQueueSend(pipeline->free_slots, &slot, 0);
    }
}

static void ring_ppa_draw_task(void *arg)
{
    production_ring_ppa_pipeline_t *pipeline = (production_ring_ppa_pipeline_t *)arg;
    while (true) {
        uint32_t slot = 0;
        if (xQueueReceive(pipeline->filled_slots, &slot, portMAX_DELAY) != pdTRUE ||
            slot >= PRODUCTION_FRAME_SLOTS) {
            continue;
        }

        const int64_t ppa_start_us = esp_timer_get_time();
        esp_err_t ppa_err = production_ppa_process_from(&pipeline->ppa, pipeline->source_slots[slot]);
        const int64_t ppa_us = esp_timer_get_time() - ppa_start_us;
        pipeline->total_ppa_us += ppa_us;
        if (ppa_us > pipeline->max_ppa_us) {
            pipeline->max_ppa_us = ppa_us;
        }

        if (ppa_err != ESP_OK) {
            ++pipeline->ppa_failures;
            pipeline->last_ppa_err = ppa_err;
            (void)xQueueSend(pipeline->free_slots, &slot, portMAX_DELAY);
            continue;
        }

        const int64_t draw_start_us = esp_timer_get_time();
        esp_err_t draw_err = destination_spi_lcd_draw_gbc_rgb565_1x_no_clear((const uint8_t *)pipeline->ppa.scaled_rgb565,
                                                                              pipeline->ppa.scaled_bytes,
                                                                              PPA_DEST_WIDTH,
                                                                              PPA_DEST_WIDTH,
                                                                              PPA_DEST_HEIGHT);
        const int64_t draw_us = esp_timer_get_time() - draw_start_us;
        pipeline->total_draw_us += draw_us;
        if (draw_us > pipeline->max_draw_us) {
            pipeline->max_draw_us = draw_us;
        }
        if (draw_err == ESP_OK) {
            ++pipeline->displayed_frames;
        } else {
            ++pipeline->draw_failures;
            pipeline->last_draw_err = draw_err;
        }

        (void)xQueueSend(pipeline->free_slots, &slot, portMAX_DELAY);

        const int64_t now_us = esp_timer_get_time();
        const int64_t elapsed_us = now_us - pipeline->window_start_us;
        if (elapsed_us >= 1000000) {
            const uint32_t displayed = pipeline->displayed_frames;
            const uint32_t captured = pipeline->captured_frames;
            const uint32_t copied = pipeline->copied_frames;
            const int64_t fps_x1000 = elapsed_us > 0 ? ((int64_t)displayed * 1000000000LL) / elapsed_us : 0;
            printf("{\"mode\":\"%s\",\"displayed\":%" PRIu32 ",\"captured\":%" PRIu32
                   ",\"copied\":%" PRIu32 ",\"fps_x1000\":%" PRId64
                   ",\"avg_capture_us\":%" PRId64 ",\"avg_copy_us\":%" PRId64
                   ",\"avg_ppa_us\":%" PRId64 ",\"avg_draw_us\":%" PRId64
                   ",\"max_capture_us\":%" PRId64 ",\"max_copy_us\":%" PRId64
                   ",\"max_ppa_us\":%" PRId64 ",\"max_draw_us\":%" PRId64
                   ",\"dropped_frames\":%" PRIu32 ",\"capture_failures\":%" PRIu32
                   ",\"ppa_failures\":%" PRIu32 ",\"draw_failures\":%" PRIu32
                   ",\"cpu_scaling\":false,\"source_path\":\"lcdcam_raw_ring_capture_loop\","
                   "\"capture_width\":%" PRIu32 ",\"capture_height\":%" PRIu32 ","
                   "\"visible_width\":%u,\"visible_height\":%u,"
                   "\"capture_error\":\"%s\",\"ppa_error\":\"%s\",\"draw_error\":\"%s\"}\n",
                   PPA_PRODUCTION_MODE_NAME,
                   displayed,
                   captured,
                   copied,
                   fps_x1000,
                   captured > 0 ? pipeline->total_capture_us / captured : 0,
                   copied > 0 ? pipeline->total_copy_us / copied : 0,
                   displayed > 0 ? pipeline->total_ppa_us / displayed : 0,
                   displayed > 0 ? pipeline->total_draw_us / displayed : 0,
                   pipeline->max_capture_us,
                   pipeline->max_copy_us,
                   pipeline->max_ppa_us,
                   pipeline->max_draw_us,
                   pipeline->dropped_frames,
                   pipeline->capture_failures,
                   pipeline->ppa_failures,
                   pipeline->draw_failures,
                   pipeline->capture_width,
                   pipeline->capture_height,
                   (unsigned)PPA_SOURCE_WIDTH,
                   (unsigned)PPA_SOURCE_HEIGHT,
                   pipeline->last_capture_err == ESP_OK ? "none" : esp_err_to_name(pipeline->last_capture_err),
                   pipeline->last_ppa_err == ESP_OK ? "none" : esp_err_to_name(pipeline->last_ppa_err),
                   pipeline->last_draw_err == ESP_OK ? "none" : esp_err_to_name(pipeline->last_draw_err));
            reset_ring_ppa_metrics(pipeline, now_us);
        }
    }
}
#endif

static void production_mirror_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "starting early production GBC source -> SPI LCD mirror");
    ESP_LOGI(TAG, "policy: RGB565 source converted to RGB666 SPI destination; no lab command transport");

#if PRODUCTION_MIRROR_MODE == PRODUCTION_MIRROR_MODE_SYNC_EACH_FRAME
    esp_err_t err = destination_spi_lcd_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "destination init failed: %s", esp_err_to_name(err));
    } else {
        esp_err_t clear_err = destination_spi_lcd_clear_black();
        if (clear_err != ESP_OK) {
            ESP_LOGW(TAG, "one-time destination clear failed: %s", esp_err_to_name(clear_err));
        }
    }

    uint32_t frames = 0;
    uint32_t capture_failures = 0;
    uint32_t draw_failures = 0;
    int64_t total_capture_us = 0;
    int64_t total_draw_us = 0;
    int64_t max_capture_us = 0;
    int64_t max_draw_us = 0;
    int64_t window_start_us = esp_timer_get_time();
    esp_err_t last_capture_err = ESP_OK;
    esp_err_t last_draw_err = ESP_OK;

    while (true) {
        if (err != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(250));
            err = destination_spi_lcd_init();
            if (err != ESP_OK) {
                continue;
            }
            esp_err_t clear_err = destination_spi_lcd_clear_black();
            if (clear_err != ESP_OK) {
                ESP_LOGW(TAG, "one-time destination clear failed: %s", esp_err_to_name(clear_err));
            }
        }

        lcdcam_raw_result_t result = {0};
        int64_t capture_us = 0;
        err = gbc_lcd_source_capture_frame(GBC_LCD_SOURCE_DEFAULT_TIMEOUT_MS,
                                           LCDCAM_RAW_DATA_RGB565,
                                           false,
                                           &result,
                                           &capture_us);
        if (err != ESP_OK) {
            ++capture_failures;
            last_capture_err = err;
            lcdcam_raw_result_free(&result);
            continue;
        }

        const int64_t draw_start_us = esp_timer_get_time();
        esp_err_t draw_err = draw_gbc_visible_rgb565(result.buffer, result.received_size);
        const int64_t draw_us = esp_timer_get_time() - draw_start_us;
        lcdcam_raw_result_free(&result);

        total_capture_us += capture_us;
        total_draw_us += draw_us;
        if (capture_us > max_capture_us) {
            max_capture_us = capture_us;
        }
        if (draw_us > max_draw_us) {
            max_draw_us = draw_us;
        }
        if (draw_err == ESP_OK) {
            ++frames;
        } else {
            ++draw_failures;
            last_draw_err = draw_err;
        }

        const int64_t now_us = esp_timer_get_time();
        const int64_t elapsed_us = now_us - window_start_us;
        if (elapsed_us >= 1000000) {
            const int64_t fps_x1000 = elapsed_us > 0 ? ((int64_t)frames * 1000000000LL) / elapsed_us : 0;
            printf("{\"mode\":\"production_mirror_sync_each_frame\",\"frames\":%" PRIu32
                   ",\"fps_x1000\":%" PRId64
                   ",\"avg_capture_us\":%" PRId64 ",\"avg_draw_us\":%" PRId64
                   ",\"max_capture_us\":%" PRId64 ",\"max_draw_us\":%" PRId64
                   ",\"capture_failures\":%" PRIu32 ",\"draw_failures\":%" PRIu32
                   ",\"capture_error\":\"%s\",\"draw_error\":\"%s\"}\n",
                   frames,
                   fps_x1000,
                   frames > 0 ? total_capture_us / frames : 0,
                   frames > 0 ? total_draw_us / frames : 0,
                   max_capture_us,
                   max_draw_us,
                   capture_failures,
                   draw_failures,
                   last_capture_err == ESP_OK ? "none" : esp_err_to_name(last_capture_err),
                   last_draw_err == ESP_OK ? "none" : esp_err_to_name(last_draw_err));
            frames = 0;
            capture_failures = 0;
            draw_failures = 0;
            total_capture_us = 0;
            total_draw_us = 0;
            max_capture_us = 0;
            max_draw_us = 0;
            window_start_us = now_us;
            last_capture_err = ESP_OK;
            last_draw_err = ESP_OK;
        }
    }
#elif PRODUCTION_MIRROR_MODE == PRODUCTION_MIRROR_MODE_DESTINATION_PATTERN
    esp_err_t err = destination_spi_lcd_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "destination init failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    uint32_t frames = 0;
    uint32_t draw_failures = 0;
    int64_t total_draw_us = 0;
    int64_t max_draw_us = 0;
    int64_t window_start_us = esp_timer_get_time();
    int64_t madctl_start_us = window_start_us;
    esp_err_t last_draw_err = ESP_OK;
    char pattern_cmd[48] = {0};
    const uint8_t madctl_value = 0xE8;
    char madctl_cmd[40] = {0};
    (void)snprintf(madctl_cmd, sizeof(madctl_cmd), "DEST_SPI_LCD_SET_MADCTL %02x", madctl_value);
    destination_spi_lcd_handle_set_madctl(madctl_cmd);
    (void)snprintf(pattern_cmd, sizeof(pattern_cmd), "DEST_SPI_LCD_TEST_PATTERN geometry_7");
    (void)pattern_cmd;

    const size_t pattern_len = destination_spi_lcd_rgb666_frame_size();
    uint8_t *pattern_frame = heap_caps_malloc(pattern_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (pattern_frame == NULL) {
        ESP_LOGE(TAG, "destination pattern allocation failed");
        vTaskDelete(NULL);
        return;
    }
    err = destination_spi_lcd_generate_geometry_rgb666(pattern_frame, pattern_len, 7);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "destination pattern generation failed: %s", esp_err_to_name(err));
        heap_caps_free(pattern_frame);
        vTaskDelete(NULL);
        return;
    }

    while (true) {
        const int64_t draw_start_us = esp_timer_get_time();
        esp_err_t draw_err = destination_spi_lcd_draw_rgb666_frame(pattern_frame, pattern_len);
        const int64_t draw_us = esp_timer_get_time() - draw_start_us;

        total_draw_us += draw_us;
        if (draw_us > max_draw_us) {
            max_draw_us = draw_us;
        }
        if (draw_err == ESP_OK) {
            ++frames;
        } else {
            ++draw_failures;
            last_draw_err = draw_err;
        }

        const int64_t now_us = esp_timer_get_time();
        (void)madctl_start_us;
        const int64_t elapsed_us = now_us - window_start_us;
        if (elapsed_us >= 1000000) {
            const int64_t fps_x1000 = elapsed_us > 0 ? ((int64_t)frames * 1000000000LL) / elapsed_us : 0;
            printf("{\"mode\":\"production_destination_pattern\","
                   "\"pattern\":\"geometry\",\"madctl\":\"0x%02x\",\"frames\":%" PRIu32
                   ",\"fps_x1000\":%" PRId64
                   ",\"avg_draw_us\":%" PRId64
                   ",\"max_draw_us\":%" PRId64
                   ",\"draw_failures\":%" PRIu32
                   ",\"draw_error\":\"%s\"}\n",
                   madctl_value,
                   frames,
                   fps_x1000,
                   frames > 0 ? total_draw_us / frames : 0,
                   max_draw_us,
                   draw_failures,
                   last_draw_err == ESP_OK ? "none" : esp_err_to_name(last_draw_err));
            frames = 0;
            draw_failures = 0;
            total_draw_us = 0;
            max_draw_us = 0;
            last_draw_err = ESP_OK;
            window_start_us = now_us;
        }
        vTaskDelay(1);
    }
#elif PRODUCTION_MIRROR_MODE == PRODUCTION_MIRROR_MODE_GBC_WINDOW_PATTERN
    esp_err_t err = destination_spi_lcd_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "destination init failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    const size_t source_len = GBC_LCD_SOURCE_STREAM_WIDTH * GBC_LCD_SOURCE_STREAM_HEIGHT * 2U;
    uint8_t *source = heap_caps_malloc(source_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (source == NULL) {
        ESP_LOGE(TAG, "GBC window pattern allocation failed");
        vTaskDelete(NULL);
        return;
    }

    for (uint32_t y = 0; y < GBC_LCD_SOURCE_STREAM_HEIGHT; ++y) {
        for (uint32_t x = 0; x < GBC_LCD_SOURCE_STREAM_WIDTH; ++x) {
            uint16_t color = 0xffffU;
            if (x < 16U && y < 16U) {
                color = 0xf800U;
            } else if (x >= 144U && y < 16U) {
                color = 0x07e0U;
            } else if (x < 16U && y >= 128U) {
                color = 0x001fU;
            } else if (((x / 8U) ^ (y / 8U)) & 1U) {
                color = 0xbdf7U;
            }
            if ((y % 16U) == 0U) {
                color = 0xffe0U;
            }
            const size_t index = ((size_t)y * GBC_LCD_SOURCE_STREAM_WIDTH + x) * 2U;
            source[index] = (uint8_t)(color & 0xffU);
            source[index + 1U] = (uint8_t)(color >> 8);
        }
    }

    uint32_t frames = 0;
    uint32_t draw_failures = 0;
    int64_t total_draw_us = 0;
    int64_t max_draw_us = 0;
    int64_t window_start_us = esp_timer_get_time();
    esp_err_t last_draw_err = ESP_OK;

    while (true) {
        const int64_t draw_start_us = esp_timer_get_time();
        esp_err_t draw_err = draw_gbc_visible_rgb565(source, source_len);
        const int64_t draw_us = esp_timer_get_time() - draw_start_us;
        total_draw_us += draw_us;
        if (draw_us > max_draw_us) {
            max_draw_us = draw_us;
        }
        if (draw_err == ESP_OK) {
            ++frames;
        } else {
            ++draw_failures;
            last_draw_err = draw_err;
        }

        const int64_t now_us = esp_timer_get_time();
        const int64_t elapsed_us = now_us - window_start_us;
        if (elapsed_us >= 1000000) {
            const int64_t fps_x1000 = elapsed_us > 0 ? ((int64_t)frames * 1000000000LL) / elapsed_us : 0;
            printf("{\"mode\":\"production_gbc_window_pattern\","
                   "\"frames\":%" PRIu32 ",\"fps_x1000\":%" PRId64
                   ",\"avg_draw_us\":%" PRId64 ",\"max_draw_us\":%" PRId64
                   ",\"draw_failures\":%" PRIu32
                   ",\"source_stream_width\":%u,\"source_stream_height\":%u,"
                   "\"source_visible_shift_pixels\":%d,"
                   "\"draw_error\":\"%s\"}\n",
                   frames,
                   fps_x1000,
                   frames > 0 ? total_draw_us / frames : 0,
                   max_draw_us,
                   draw_failures,
                   (unsigned)GBC_LCD_SOURCE_STREAM_WIDTH,
                   (unsigned)GBC_LCD_SOURCE_STREAM_HEIGHT,
                   (int)GBC_LCD_SOURCE_VISIBLE_LINEAR_SHIFT_PIXELS,
                   last_draw_err == ESP_OK ? "none" : esp_err_to_name(last_draw_err));
            frames = 0;
            draw_failures = 0;
            total_draw_us = 0;
            max_draw_us = 0;
            last_draw_err = ESP_OK;
            window_start_us = now_us;
        }
        vTaskDelay(1);
    }
#elif PRODUCTION_MIRROR_MODE == PRODUCTION_MIRROR_MODE_FREEZE_FRAME
    esp_err_t err = destination_spi_lcd_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "destination init failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    lcdcam_raw_result_t result = {0};
    int64_t capture_us = 0;
    err = gbc_lcd_source_capture_frame(GBC_LCD_SOURCE_DEFAULT_TIMEOUT_MS,
                                       LCDCAM_RAW_DATA_RGB565,
                                       false,
                                       &result,
                                       &capture_us);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "freeze capture failed: %s", esp_err_to_name(err));
        lcdcam_raw_result_free(&result);
        vTaskDelete(NULL);
        return;
    }

    uint8_t *frozen = heap_caps_malloc(result.received_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (frozen == NULL) {
        ESP_LOGE(TAG, "freeze buffer allocation failed");
        lcdcam_raw_result_free(&result);
        vTaskDelete(NULL);
        return;
    }
    memcpy(frozen, result.buffer, result.received_size);
    const size_t frozen_size = result.received_size;
    lcdcam_raw_result_free(&result);

    uint32_t frames = 0;
    uint32_t draw_failures = 0;
    int64_t total_draw_us = 0;
    int64_t max_draw_us = 0;
    int64_t window_start_us = esp_timer_get_time();
    esp_err_t last_draw_err = ESP_OK;

    while (true) {
        const int64_t draw_start_us = esp_timer_get_time();
        esp_err_t draw_err = draw_gbc_visible_rgb565(frozen, frozen_size);
        const int64_t draw_us = esp_timer_get_time() - draw_start_us;
        total_draw_us += draw_us;
        if (draw_us > max_draw_us) {
            max_draw_us = draw_us;
        }
        if (draw_err == ESP_OK) {
            ++frames;
        } else {
            ++draw_failures;
            last_draw_err = draw_err;
        }

        const int64_t now_us = esp_timer_get_time();
        const int64_t elapsed_us = now_us - window_start_us;
        if (elapsed_us >= 1000000) {
            const int64_t fps_x1000 = elapsed_us > 0 ? ((int64_t)frames * 1000000000LL) / elapsed_us : 0;
            printf("{\"mode\":\"production_mirror_freeze_frame\",\"frames\":%" PRIu32
                   ",\"fps_x1000\":%" PRId64 ",\"capture_us\":%" PRId64
                   ",\"avg_draw_us\":%" PRId64 ",\"max_draw_us\":%" PRId64
                   ",\"draw_failures\":%" PRIu32 ",\"frozen_size\":%zu,"
                   "\"source_stream_width\":%u,"
                   "\"source_visible_shift_pixels\":%d,"
                   "\"draw_error\":\"%s\"}\n",
                   frames,
                   fps_x1000,
                   capture_us,
                   frames > 0 ? total_draw_us / frames : 0,
                   max_draw_us,
                   draw_failures,
                   frozen_size,
                   (unsigned)GBC_LCD_SOURCE_STREAM_WIDTH,
                   (int)GBC_LCD_SOURCE_VISIBLE_LINEAR_SHIFT_PIXELS,
                   last_draw_err == ESP_OK ? "none" : esp_err_to_name(last_draw_err));
            frames = 0;
            draw_failures = 0;
            total_draw_us = 0;
            max_draw_us = 0;
            window_start_us = now_us;
            last_draw_err = ESP_OK;
        }
    }
#elif PRODUCTION_MIRROR_MODE == PRODUCTION_MIRROR_MODE_OVERLAP || \
      PRODUCTION_MIRROR_MODE == PRODUCTION_MIRROR_MODE_RING_DIRECT_1X
    if (!production_pipeline_init(&s_pipeline)) {
        ESP_LOGE(TAG, "production pipeline allocation failed");
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = destination_spi_lcd_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "destination init failed: %s", esp_err_to_name(err));
    } else {
        esp_err_t clear_err = destination_spi_lcd_clear_black();
        if (clear_err != ESP_OK) {
            ESP_LOGW(TAG, "one-time destination clear failed: %s", esp_err_to_name(clear_err));
        }
    }
    start_draw_task_once(&s_pipeline);

    while (true) {
        if (err != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(250));
            err = destination_spi_lcd_init();
            if (err != ESP_OK) {
                continue;
            }
            esp_err_t clear_err = destination_spi_lcd_clear_black();
            if (clear_err != ESP_OK) {
                ESP_LOGW(TAG, "one-time destination clear failed: %s", esp_err_to_name(clear_err));
            }
        }

#if PRODUCTION_MIRROR_MODE == PRODUCTION_MIRROR_MODE_RING_DIRECT_1X
        err = run_ring_capture_window(&s_pipeline);
#else
        err = run_capture_window(&s_pipeline);
#endif
        if (err != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(20));
            err = ESP_OK;
        }
    }
#elif PRODUCTION_MIRROR_MODE == PRODUCTION_MIRROR_MODE_PPA_2X_SYNC || \
      PRODUCTION_MIRROR_MODE == PRODUCTION_MIRROR_MODE_PPA_1X_SYNC
    production_ppa_context_t ppa = {0};
    esp_err_t ppa_err = production_ppa_init(&ppa);
    if (ppa_err != ESP_OK) {
        ESP_LOGE(TAG, "PPA init failed: %s", esp_err_to_name(ppa_err));
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = destination_spi_lcd_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "destination init failed: %s", esp_err_to_name(err));
    } else {
        esp_err_t clear_err = destination_spi_lcd_clear_black();
        if (clear_err != ESP_OK) {
            ESP_LOGW(TAG, "one-time destination clear failed: %s", esp_err_to_name(clear_err));
        }
    }

    uint32_t frames = 0;
    uint32_t capture_failures = 0;
    uint32_t ppa_failures = 0;
    uint32_t draw_failures = 0;
    int64_t total_capture_us = 0;
    int64_t total_crop_us = 0;
    int64_t total_ppa_us = 0;
    int64_t total_draw_us = 0;
    int64_t max_capture_us = 0;
    int64_t max_crop_us = 0;
    int64_t max_ppa_us = 0;
    int64_t max_draw_us = 0;
    int64_t window_start_us = esp_timer_get_time();
    esp_err_t last_capture_err = ESP_OK;
    esp_err_t last_ppa_err = ESP_OK;
    esp_err_t last_draw_err = ESP_OK;

    while (true) {
        if (err != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(250));
            err = destination_spi_lcd_init();
            if (err != ESP_OK) {
                continue;
            }
            esp_err_t clear_err = destination_spi_lcd_clear_black();
            if (clear_err != ESP_OK) {
                ESP_LOGW(TAG, "one-time destination clear failed: %s", esp_err_to_name(clear_err));
            }
        }

        lcdcam_raw_result_t result = {0};
        int64_t capture_us = 0;
        err = gbc_lcd_source_capture_frame(GBC_LCD_SOURCE_DEFAULT_TIMEOUT_MS,
                                           LCDCAM_RAW_DATA_RGB565,
                                           false,
                                           &result,
                                           &capture_us);
        if (err != ESP_OK) {
            ++capture_failures;
            last_capture_err = err;
            lcdcam_raw_result_free(&result);
            continue;
        }

        const int64_t crop_start_us = esp_timer_get_time();
        esp_err_t crop_err = copy_visible_to_contiguous_rgb565(result.buffer, result.received_size, ppa.source_rgb565);
        const int64_t crop_us = esp_timer_get_time() - crop_start_us;
        lcdcam_raw_result_free(&result);
        if (crop_err != ESP_OK) {
            ++capture_failures;
            last_capture_err = crop_err;
            continue;
        }

        const int64_t ppa_start_us = esp_timer_get_time();
        esp_err_t scale_err = production_ppa_process(&ppa);
        const int64_t ppa_us = esp_timer_get_time() - ppa_start_us;
        if (scale_err != ESP_OK) {
            ++ppa_failures;
            last_ppa_err = scale_err;
            continue;
        }

        const int64_t draw_start_us = esp_timer_get_time();
        esp_err_t draw_err = destination_spi_lcd_draw_gbc_rgb565_1x_no_clear((const uint8_t *)ppa.scaled_rgb565,
                                                                              ppa.scaled_bytes,
                                                                              PPA_DEST_WIDTH,
                                                                              PPA_DEST_WIDTH,
                                                                              PPA_DEST_HEIGHT);
        const int64_t draw_us = esp_timer_get_time() - draw_start_us;

        total_capture_us += capture_us;
        total_crop_us += crop_us;
        total_ppa_us += ppa_us;
        total_draw_us += draw_us;
        if (capture_us > max_capture_us) {
            max_capture_us = capture_us;
        }
        if (crop_us > max_crop_us) {
            max_crop_us = crop_us;
        }
        if (ppa_us > max_ppa_us) {
            max_ppa_us = ppa_us;
        }
        if (draw_us > max_draw_us) {
            max_draw_us = draw_us;
        }
        if (draw_err == ESP_OK) {
            ++frames;
        } else {
            ++draw_failures;
            last_draw_err = draw_err;
        }

        const int64_t now_us = esp_timer_get_time();
        const int64_t elapsed_us = now_us - window_start_us;
        if (elapsed_us >= 1000000) {
            const int64_t fps_x1000 = elapsed_us > 0 ? ((int64_t)frames * 1000000000LL) / elapsed_us : 0;
            printf("{\"mode\":\"%s\",\"frames\":%" PRIu32
                   ",\"fps_x1000\":%" PRId64
                   ",\"avg_capture_us\":%" PRId64 ",\"avg_crop_us\":%" PRId64
                   ",\"avg_ppa_us\":%" PRId64 ",\"avg_draw_us\":%" PRId64
                   ",\"max_capture_us\":%" PRId64 ",\"max_crop_us\":%" PRId64
                   ",\"max_ppa_us\":%" PRId64 ",\"max_draw_us\":%" PRId64
                   ",\"capture_failures\":%" PRIu32 ",\"ppa_failures\":%" PRIu32
                   ",\"draw_failures\":%" PRIu32 ",\"capture_error\":\"%s\","
                   "\"ppa_error\":\"%s\",\"draw_error\":\"%s\"}\n",
                   PPA_PRODUCTION_MODE_NAME,
                   frames,
                   fps_x1000,
                   frames > 0 ? total_capture_us / frames : 0,
                   frames > 0 ? total_crop_us / frames : 0,
                   frames > 0 ? total_ppa_us / frames : 0,
                   frames > 0 ? total_draw_us / frames : 0,
                   max_capture_us,
                   max_crop_us,
                   max_ppa_us,
                   max_draw_us,
                   capture_failures,
                   ppa_failures,
                   draw_failures,
                   last_capture_err == ESP_OK ? "none" : esp_err_to_name(last_capture_err),
                   last_ppa_err == ESP_OK ? "none" : esp_err_to_name(last_ppa_err),
                   last_draw_err == ESP_OK ? "none" : esp_err_to_name(last_draw_err));
            frames = 0;
            capture_failures = 0;
            ppa_failures = 0;
            draw_failures = 0;
            total_capture_us = 0;
            total_crop_us = 0;
            total_ppa_us = 0;
            total_draw_us = 0;
            max_capture_us = 0;
            max_crop_us = 0;
            max_ppa_us = 0;
            max_draw_us = 0;
            window_start_us = now_us;
            last_capture_err = ESP_OK;
            last_ppa_err = ESP_OK;
            last_draw_err = ESP_OK;
        }
    }
#elif PRODUCTION_MIRROR_MODE == PRODUCTION_MIRROR_MODE_PPA_RING_2X || \
      PRODUCTION_MIRROR_MODE == PRODUCTION_MIRROR_MODE_PPA_RING_1X || \
      PRODUCTION_MIRROR_MODE == PRODUCTION_MIRROR_MODE_PPA_RING_STREAM_1X
    if (!production_ring_ppa_init(&s_ring_ppa)) {
        ESP_LOGE(TAG, "ring PPA production pipeline allocation failed");
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = destination_spi_lcd_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "destination init failed: %s", esp_err_to_name(err));
    } else {
        esp_err_t clear_err = destination_spi_lcd_clear_black();
        if (clear_err != ESP_OK) {
            ESP_LOGW(TAG, "one-time destination clear failed: %s", esp_err_to_name(clear_err));
        }
    }

    xTaskCreatePinnedToCore(ring_ppa_draw_task, "ring_ppa_draw", 8192, &s_ring_ppa, 11, NULL, 1);

    while (true) {
        if (err != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(250));
            err = destination_spi_lcd_init();
            if (err != ESP_OK) {
                continue;
            }
            esp_err_t clear_err = destination_spi_lcd_clear_black();
            if (clear_err != ESP_OK) {
                ESP_LOGW(TAG, "one-time destination clear failed: %s", esp_err_to_name(clear_err));
            }
        }

        lcdcam_raw_ring_stats_t stats = {0};
        err = lcdcam_raw_ring_capture_loop(LCDCAM_RAW_DE_HIGH,
                                           PPA_RING_CAPTURE_WIDTH,
                                           PPA_RING_CAPTURE_HEIGHT,
                                           GBC_LCD_SOURCE_DEFAULT_TIMEOUT_MS,
                                           false,
                                           false,
                                           false,
                                           true,
                                           LCDCAM_RAW_START_AFTER_SPS_RISING,
                                           false,
                                           LCDCAM_RAW_DATA_RGB565,
                                           PRODUCTION_CAPTURE_WINDOW_FRAMES,
                                           ring_ppa_capture_callback,
                                           &s_ring_ppa,
                                           &stats);
        if (err != ESP_OK) {
            ++s_ring_ppa.capture_failures;
            s_ring_ppa.last_capture_err = err;
            ESP_LOGW(TAG,
                     "ring capture window failed at %s: %s",
                     stats.failure_stage == NULL ? "unknown" : stats.failure_stage,
                     esp_err_to_name(err));
            destination_spi_lcd_safe_off();
        }
    }
#else
#error "Unsupported PRODUCTION_MIRROR_MODE"
#endif
}

void production_mirror_start(void)
{
    xTaskCreatePinnedToCore(production_mirror_task,
                            "production_mirror",
                            8192,
                            NULL,
                            10,
                            NULL,
                            1);
}
