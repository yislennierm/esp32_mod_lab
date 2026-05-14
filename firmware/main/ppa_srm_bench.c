#include "ppa_srm_bench.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/ppa.h"
#include "esp_cache.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_private/esp_cache_private.h"
#include "esp_timer.h"

#define PPA_BENCH_SRC_W 160U
#define PPA_BENCH_SRC_H 144U
#define PPA_BENCH_DST_W 320U
#define PPA_BENCH_DST_H 288U
#define PPA_BENCH_BYTES_PER_PIXEL 2U
#define PPA_BENCH_DEFAULT_FRAMES 120U
#define PPA_BENCH_MAX_FRAMES 1000U
#define PPA_BENCH_TARGET_FPS 59.73

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)((r >> 3) << 11) | (uint16_t)((g >> 2) << 5) | (uint16_t)(b >> 3);
}

static void fill_source(uint16_t *src)
{
    for (uint32_t y = 0; y < PPA_BENCH_SRC_H; ++y) {
        for (uint32_t x = 0; x < PPA_BENCH_SRC_W; ++x) {
            const uint8_t r = (uint8_t)((x * 255U) / (PPA_BENCH_SRC_W - 1U));
            const uint8_t g = (uint8_t)((y * 255U) / (PPA_BENCH_SRC_H - 1U));
            const uint8_t b = ((x / 8U) ^ (y / 8U)) & 1U ? 255U : 32U;
            src[(y * PPA_BENCH_SRC_W) + x] = rgb565(r, g, b);
        }
    }
}

static uint32_t checksum_sample(const uint16_t *buf, size_t pixels)
{
    uint32_t checksum = 5381U;
    const size_t step = pixels > 512U ? pixels / 512U : 1U;
    for (size_t i = 0; i < pixels; i += step) {
        checksum = ((checksum << 5) + checksum) ^ buf[i];
    }
    return checksum;
}

static int64_t bench_cpu_scale2x(const uint16_t *src, uint16_t *dst, uint32_t frames)
{
    const int64_t start_us = esp_timer_get_time();
    for (uint32_t frame = 0; frame < frames; ++frame) {
        for (uint32_t y = 0; y < PPA_BENCH_SRC_H; ++y) {
            const uint16_t *in = src + (y * PPA_BENCH_SRC_W);
            uint16_t *out0 = dst + ((y * 2U) * PPA_BENCH_DST_W);
            uint16_t *out1 = out0 + PPA_BENCH_DST_W;
            for (uint32_t x = 0; x < PPA_BENCH_SRC_W; ++x) {
                const uint16_t p = in[x];
                const uint32_t dx = x * 2U;
                out0[dx] = p;
                out0[dx + 1U] = p;
                out1[dx] = p;
                out1[dx + 1U] = p;
            }
        }
    }
    return esp_timer_get_time() - start_us;
}

static esp_err_t bench_ppa_scale2x(ppa_client_handle_t client,
                                   const uint16_t *src,
                                   uint16_t *dst,
                                   size_t src_bytes,
                                   size_t dst_bytes,
                                   uint32_t frames,
                                   int64_t *elapsed_us)
{
    ppa_srm_oper_config_t config = {
        .in = {
            .buffer = src,
            .pic_w = PPA_BENCH_SRC_W,
            .pic_h = PPA_BENCH_SRC_H,
            .block_w = PPA_BENCH_SRC_W,
            .block_h = PPA_BENCH_SRC_H,
            .block_offset_x = 0,
            .block_offset_y = 0,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .out = {
            .buffer = dst,
            .buffer_size = dst_bytes,
            .pic_w = PPA_BENCH_DST_W,
            .pic_h = PPA_BENCH_DST_H,
            .block_offset_x = 0,
            .block_offset_y = 0,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
        .scale_x = 2.0f,
        .scale_y = 2.0f,
        .mirror_x = false,
        .mirror_y = false,
        .rgb_swap = false,
        .byte_swap = false,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };

    esp_err_t err = ESP_OK;
    const int64_t start_us = esp_timer_get_time();
    for (uint32_t frame = 0; frame < frames; ++frame) {
        err = esp_cache_msync((void *)src, src_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
        if (err != ESP_OK) {
            break;
        }
        err = ppa_do_scale_rotate_mirror(client, &config);
        if (err != ESP_OK) {
            break;
        }
        err = esp_cache_msync(dst, dst_bytes, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
        if (err != ESP_OK) {
            break;
        }
    }
    *elapsed_us = esp_timer_get_time() - start_us;
    return err;
}

static void print_result(const char *command,
                         esp_err_t err,
                         uint32_t frames,
                         size_t src_bytes,
                         size_t dst_bytes,
                         int64_t elapsed_us,
                         uint32_t checksum)
{
    const double fps = elapsed_us > 0 ? ((double)frames * 1000000.0) / (double)elapsed_us : 0.0;
    const double avg_us = frames > 0 ? (double)elapsed_us / (double)frames : 0.0;
    const double target_frame_us = 1000000.0 / PPA_BENCH_TARGET_FPS;
    const bool target_rate_met = fps >= (PPA_BENCH_TARGET_FPS * 0.98);
    printf("{\"ok\":%s,\"command\":\"%s\",\"schema\":\"esp32_mod_lab.benchmark.ppa_srm.v1\","
           "\"mode\":\"lab_firmware_synthetic_rgb565_scale2x\","
           "\"source_width\":%u,\"source_height\":%u,\"dest_width\":%u,\"dest_height\":%u,"
           "\"source_bytes\":%zu,\"dest_bytes\":%zu,\"frames\":%" PRIu32 ","
           "\"elapsed_us\":%" PRId64 ",\"avg_us\":%.1f,\"fps\":%.3f,"
           "\"target_fps\":%.3f,\"target_rate_met\":%s,\"target_frame_us\":%.1f,"
           "\"checksum\":%" PRIu32 ",\"error\":\"%s\",\"esp_err\":%d}\n",
           err == ESP_OK ? "true" : "false",
           command,
           (unsigned)PPA_BENCH_SRC_W,
           (unsigned)PPA_BENCH_SRC_H,
           (unsigned)PPA_BENCH_DST_W,
           (unsigned)PPA_BENCH_DST_H,
           src_bytes,
           dst_bytes,
           frames,
           elapsed_us,
           avg_us,
           fps,
           PPA_BENCH_TARGET_FPS,
           target_rate_met ? "true" : "false",
           target_frame_us,
           checksum,
           err == ESP_OK ? "none" : esp_err_to_name(err),
           err);
}

void ppa_srm_bench_handle_command(const char *line)
{
    uint32_t frames = PPA_BENCH_DEFAULT_FRAMES;
    int parsed = sscanf(line, "PPA_SRM_BENCH %" SCNu32, &frames);
    if (parsed < 0) {
        parsed = 0;
    }
    if (frames == 0 || frames > PPA_BENCH_MAX_FRAMES) {
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_arguments\","
               "\"usage\":\"PPA_SRM_BENCH [frame_count_1_to_%u]\"}\n",
               line,
               (unsigned)PPA_BENCH_MAX_FRAMES);
        return;
    }

    const size_t src_bytes = PPA_BENCH_SRC_W * PPA_BENCH_SRC_H * PPA_BENCH_BYTES_PER_PIXEL;
    const size_t dst_bytes = PPA_BENCH_DST_W * PPA_BENCH_DST_H * PPA_BENCH_BYTES_PER_PIXEL;
    size_t alignment = 0;
    esp_err_t err = esp_cache_get_alignment(MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA, &alignment);
    if (err != ESP_OK) {
        print_result("PPA_SRM_SETUP", err, 0, src_bytes, dst_bytes, 0, 0);
        return;
    }

    uint16_t *src = heap_caps_aligned_calloc(alignment, 1, src_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    uint16_t *ppa_dst = heap_caps_aligned_calloc(alignment, 1, dst_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    uint16_t *cpu_dst = heap_caps_aligned_calloc(alignment, 1, dst_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (src == NULL || ppa_dst == NULL || cpu_dst == NULL) {
        print_result("PPA_SRM_SETUP", ESP_ERR_NO_MEM, 0, src_bytes, dst_bytes, 0, 0);
        heap_caps_free(src);
        heap_caps_free(ppa_dst);
        heap_caps_free(cpu_dst);
        return;
    }
    fill_source(src);

    ppa_client_handle_t client = NULL;
    ppa_client_config_t client_config = {
        .oper_type = PPA_OPERATION_SRM,
        .max_pending_trans_num = 1,
        .data_burst_length = PPA_DATA_BURST_LENGTH_128,
    };
    err = ppa_register_client(&client_config, &client);
    if (err != ESP_OK) {
        print_result("PPA_SRM_SETUP", err, 0, src_bytes, dst_bytes, 0, 0);
        heap_caps_free(src);
        heap_caps_free(ppa_dst);
        heap_caps_free(cpu_dst);
        return;
    }

    int64_t ppa_elapsed_us = 0;
    err = bench_ppa_scale2x(client, src, ppa_dst, src_bytes, dst_bytes, frames, &ppa_elapsed_us);
    const uint32_t ppa_checksum = checksum_sample(ppa_dst, PPA_BENCH_DST_W * PPA_BENCH_DST_H);
    print_result("PPA_SRM_SCALE2X_BENCH", err, frames, src_bytes, dst_bytes, ppa_elapsed_us, ppa_checksum);

    memset(cpu_dst, 0, dst_bytes);
    const int64_t cpu_elapsed_us = bench_cpu_scale2x(src, cpu_dst, frames);
    const uint32_t cpu_checksum = checksum_sample(cpu_dst, PPA_BENCH_DST_W * PPA_BENCH_DST_H);
    print_result("CPU_SCALE2X_BENCH", ESP_OK, frames, src_bytes, dst_bytes, cpu_elapsed_us, cpu_checksum);

    esp_err_t unregister_err = ppa_unregister_client(client);
    if (unregister_err != ESP_OK) {
        print_result("PPA_SRM_UNREGISTER", unregister_err, 0, src_bytes, dst_bytes, 0, 0);
    }
    heap_caps_free(src);
    heap_caps_free(ppa_dst);
    heap_caps_free(cpu_dst);
}
