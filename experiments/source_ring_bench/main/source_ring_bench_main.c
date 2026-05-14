#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "gbc_lcd_source.h"
#include "lcdcam_raw.h"

#define SOURCE_RING_BENCH_FRAMES 120U
#define SOURCE_RING_BENCH_TIMEOUT_MS GBC_LCD_SOURCE_DEFAULT_TIMEOUT_MS
#define SOURCE_RING_BENCH_TARGET_FPS 59.73

static const char *TAG = "source_ring_bench";

static void print_ready(void)
{
    printf("{\"event\":\"ready\","
           "\"app\":\"source_ring_bench\","
           "\"purpose\":\"isolated_lcdcam_gdma_source_ingress_benchmark\","
           "\"lab_protocol\":false,"
           "\"tinyusb\":false,"
           "\"destination_spi\":false,"
           "\"browser_stream\":false,"
           "\"source_profile\":\"gbc_lcd\","
           "\"data_mode\":\"RGB565\","
           "\"frames_per_run\":%" PRIu32 ","
           "\"timeout_ms\":%" PRIu32 "}\n",
           (uint32_t)SOURCE_RING_BENCH_FRAMES,
           (uint32_t)SOURCE_RING_BENCH_TIMEOUT_MS);
    fflush(stdout);
}

static void print_rearm_result(uint32_t run_index, const lcdcam_raw_rearm_stats_t *stats, esp_err_t err)
{
    double elapsed_s = (double)stats->elapsed_us / 1000000.0;
    double completed_fps = elapsed_s > 0.0 ? (double)stats->completed_chunks / elapsed_s : 0.0;
    double payload_mbytes_s = elapsed_s > 0.0 ?
                              ((double)stats->completed_chunks * (double)stats->chunk_bytes) /
                                  (elapsed_s * 1000000.0) :
                              0.0;
    double target_frame_us = 1000000.0 / SOURCE_RING_BENCH_TARGET_FPS;
    double avg_capture_budget_pct = target_frame_us > 0.0 ?
                                    ((double)stats->avg_chunk_us / target_frame_us) * 100.0 :
                                    0.0;
    bool target_rate_met = completed_fps >= (SOURCE_RING_BENCH_TARGET_FPS * 0.98) &&
                           stats->completed_chunks == stats->requested_chunks;

    printf("{\"ok\":%s,\"command\":\"SOURCE_RING_BENCH_AUTO\","
           "\"schema\":\"esp32_mod_lab.benchmark.source_ring.v1\","
           "\"run_index\":%" PRIu32 ","
           "\"source_profile\":\"gbc_lcd\","
           "\"mode\":\"isolated_source_ingress_counters_only\","
           "\"performance_path\":\"lcdcam_gdma_double_buffer_rearm\","
           "\"next_performance_path\":\"persistent_continuous_descriptor_ring\","
           "\"hot_path_excludes\":[\"lab_protocol\",\"browser_frame_stream\",\"spi_lcd_draw\",\"tinyusb\",\"png_render\"],"
           "\"requested_frames\":%" PRIu32 ","
           "\"completed_frames\":%" PRIu32 ","
           "\"dropped_frames\":0,"
           "\"partial_frames\":%" PRIu32 ","
           "\"sync_loss_count\":%" PRIu32 ","
           "\"dma_errors\":%" PRIu32 ","
           "\"ring_slots\":2,"
           "\"ring_high_water_mark\":1,"
           "\"data_mode\":\"RGB565\","
           "\"capture_width\":%" PRIu32 ","
           "\"capture_height\":%" PRIu32 ","
           "\"bytes_per_sample\":%" PRIu32 ","
           "\"frame_bytes\":%" PRIu32 ","
           "\"timeout_ms\":%" PRIu32 ","
           "\"pclk_invert\":false,"
           "\"start_trigger\":\"SPS_RISING_THEN_SPL_FALLING\","
           "\"start_trigger_seen\":%s,"
           "\"target_source_fps\":%.3f,"
           "\"target_frame_us\":%.1f,"
           "\"target_rate_met\":%s,"
           "\"avg_capture_budget_pct\":%.1f,"
           "\"elapsed_us\":%" PRId64 ","
           "\"completed_fps\":%.3f,"
           "\"payload_mbytes_per_s\":%.3f,"
           "\"first_frame_us\":%" PRId64 ","
           "\"avg_capture_us\":%" PRId64 ","
           "\"max_capture_us\":%" PRId64 ","
           "\"failure_stage\":\"%s\","
           "\"last_esp_err\":%d,"
           "\"run_esp_err\":%d,"
           "\"checksum\":%" PRIu32 "}\n",
           err == ESP_OK ? "true" : "false",
           run_index,
           stats->requested_chunks,
           stats->completed_chunks,
           stats->requested_chunks - stats->completed_chunks,
           (uint32_t)(stats->start_trigger_seen ? 0U : 1U),
           stats->failed_rearms,
           stats->h_res,
           stats->v_res,
           stats->bytes_per_sample,
           stats->chunk_bytes,
           stats->timeout_ms,
           stats->start_trigger_seen ? "true" : "false",
           SOURCE_RING_BENCH_TARGET_FPS,
           target_frame_us,
           target_rate_met ? "true" : "false",
           avg_capture_budget_pct,
           stats->elapsed_us,
           completed_fps,
           payload_mbytes_s,
           stats->first_chunk_us,
           stats->avg_chunk_us,
           stats->max_chunk_us,
           stats->failure_stage == NULL ? "unknown" : stats->failure_stage,
           stats->last_esp_err,
           err,
           stats->checksum);
    fflush(stdout);
}

static void print_ctlr_result(uint32_t run_index, const lcdcam_raw_ctlr_stats_t *stats, esp_err_t err)
{
    double elapsed_s = (double)stats->elapsed_us / 1000000.0;
    double completed_fps = elapsed_s > 0.0 ? (double)stats->completed_frames / elapsed_s : 0.0;
    double payload_mbytes_s = elapsed_s > 0.0 ?
                              ((double)stats->completed_frames * (double)stats->frame_bytes) /
                                  (elapsed_s * 1000000.0) :
                              0.0;
    double target_frame_us = 1000000.0 / SOURCE_RING_BENCH_TARGET_FPS;
    double avg_capture_budget_pct = target_frame_us > 0.0 ?
                                    ((double)stats->avg_frame_interval_us / target_frame_us) * 100.0 :
                                    0.0;
    bool target_rate_met = completed_fps >= (SOURCE_RING_BENCH_TARGET_FPS * 0.98) &&
                           stats->completed_frames == stats->requested_frames;

    printf("{\"ok\":%s,\"command\":\"SOURCE_RING_CTLR_BENCH_AUTO\","
           "\"schema\":\"esp32_mod_lab.benchmark.source_ring.v1\","
           "\"run_index\":%" PRIu32 ","
           "\"source_profile\":\"gbc_lcd\","
           "\"mode\":\"isolated_source_ingress_counters_only\","
           "\"performance_path\":\"esp_cam_ctlr_dvp_cyclic_buffers\","
           "\"next_performance_path\":\"low_level_persistent_continuous_descriptor_ring\","
           "\"hot_path_excludes\":[\"lab_protocol\",\"browser_frame_stream\",\"spi_lcd_draw\",\"tinyusb\",\"png_render\"],"
           "\"requested_frames\":%" PRIu32 ","
           "\"completed_frames\":%" PRIu32 ","
           "\"dropped_frames\":%" PRIu32 ","
           "\"partial_frames\":%" PRIu32 ","
           "\"sync_loss_count\":%" PRIu32 ","
           "\"dma_errors\":%d,"
           "\"ring_slots\":%" PRIu32 ","
           "\"data_mode\":\"RGB565\","
           "\"capture_width\":%" PRIu32 ","
           "\"capture_height\":%" PRIu32 ","
           "\"bytes_per_sample\":%" PRIu32 ","
           "\"frame_bytes\":%" PRIu32 ","
           "\"timeout_ms\":%" PRIu32 ","
           "\"pclk_invert\":false,"
           "\"start_trigger\":\"SPS_RISING_THEN_SPL_FALLING\","
           "\"start_trigger_seen\":%s,"
           "\"target_source_fps\":%.3f,"
           "\"target_frame_us\":%.1f,"
           "\"target_rate_met\":%s,"
           "\"avg_capture_budget_pct\":%.1f,"
           "\"elapsed_us\":%" PRId64 ","
           "\"completed_fps\":%.3f,"
           "\"payload_mbytes_per_s\":%.3f,"
           "\"first_frame_us\":%" PRId64 ","
           "\"avg_capture_us\":%" PRId64 ","
           "\"max_capture_us\":%" PRId64 ","
           "\"failure_stage\":\"%s\","
           "\"last_esp_err\":%d,"
           "\"run_esp_err\":%d,"
           "\"checksum\":%" PRIu32 "}\n",
           err == ESP_OK ? "true" : "false",
           run_index,
           stats->requested_frames,
           stats->completed_frames,
           stats->dropped_frames,
           stats->requested_frames - stats->completed_frames,
           (uint32_t)(stats->start_trigger_seen ? 0U : 1U),
           stats->last_esp_err == ESP_OK ? 0 : 1,
           stats->buffer_slots,
           stats->h_res,
           stats->v_res,
           stats->bytes_per_sample,
           stats->frame_bytes,
           stats->timeout_ms,
           stats->start_trigger_seen ? "true" : "false",
           SOURCE_RING_BENCH_TARGET_FPS,
           target_frame_us,
           target_rate_met ? "true" : "false",
           avg_capture_budget_pct,
           stats->elapsed_us,
           completed_fps,
           payload_mbytes_s,
           stats->first_frame_us,
           stats->avg_frame_interval_us,
           stats->max_frame_interval_us,
           stats->failure_stage == NULL ? "unknown" : stats->failure_stage,
           stats->last_esp_err,
           err,
           stats->checksum);
    fflush(stdout);
}

static void print_ring_result(uint32_t run_index, const lcdcam_raw_ring_stats_t *stats, esp_err_t err)
{
    double elapsed_s = (double)stats->elapsed_us / 1000000.0;
    double completed_fps = elapsed_s > 0.0 ? (double)stats->completed_frames / elapsed_s : 0.0;
    double payload_mbytes_s = elapsed_s > 0.0 ?
                              ((double)stats->completed_frames * (double)stats->frame_bytes) /
                                  (elapsed_s * 1000000.0) :
                              0.0;
    double target_frame_us = 1000000.0 / SOURCE_RING_BENCH_TARGET_FPS;
    double avg_capture_budget_pct = target_frame_us > 0.0 ?
                                    ((double)stats->avg_frame_interval_us / target_frame_us) * 100.0 :
                                    0.0;
    bool target_rate_met = completed_fps >= (SOURCE_RING_BENCH_TARGET_FPS * 0.98) &&
                           stats->completed_frames == stats->requested_frames &&
                           stats->ring_rearm_failures == 0 &&
                           stats->unknown_eof_desc == 0;

    printf("{\"ok\":%s,\"command\":\"SOURCE_RING_LOWLEVEL_BENCH_AUTO\","
           "\"schema\":\"esp32_mod_lab.benchmark.source_ring.v1\","
           "\"run_index\":%" PRIu32 ","
           "\"source_profile\":\"gbc_lcd\","
           "\"mode\":\"isolated_source_ingress_counters_only\","
           "\"performance_path\":\"low_level_cyclic_lcdcam_gdma_descriptor_ring\","
           "\"next_performance_path\":\"production_source_frame_ring\","
           "\"hot_path_excludes\":[\"lab_protocol\",\"browser_frame_stream\",\"spi_lcd_draw\",\"tinyusb\",\"png_render\"],"
           "\"requested_frames\":%" PRIu32 ","
           "\"completed_frames\":%" PRIu32 ","
           "\"dropped_frames\":0,"
           "\"partial_frames\":%" PRIu32 ","
           "\"sync_loss_count\":%" PRIu32 ","
           "\"dma_errors\":%" PRIu32 ","
           "\"ring_slots\":%" PRIu32 ","
           "\"descriptor_count_per_slot\":%" PRIu32 ","
           "\"ring_rearms\":%" PRIu32 ","
           "\"ring_rearm_failures\":%" PRIu32 ","
           "\"unknown_eof_desc\":%" PRIu32 ","
           "\"data_mode\":\"RGB565\","
           "\"capture_width\":%" PRIu32 ","
           "\"capture_height\":%" PRIu32 ","
           "\"bytes_per_sample\":%" PRIu32 ","
           "\"frame_bytes\":%" PRIu32 ","
           "\"timeout_ms\":%" PRIu32 ","
           "\"pclk_invert\":false,"
           "\"start_trigger\":\"SPS_RISING_THEN_SPL_FALLING\","
           "\"start_trigger_seen\":%s,"
           "\"target_source_fps\":%.3f,"
           "\"target_frame_us\":%.1f,"
           "\"target_rate_met\":%s,"
           "\"avg_capture_budget_pct\":%.1f,"
           "\"elapsed_us\":%" PRId64 ","
           "\"completed_fps\":%.3f,"
           "\"payload_mbytes_per_s\":%.3f,"
           "\"first_frame_us\":%" PRId64 ","
           "\"avg_capture_us\":%" PRId64 ","
           "\"max_capture_us\":%" PRId64 ","
           "\"failure_stage\":\"%s\","
           "\"last_esp_err\":%d,"
           "\"run_esp_err\":%d,"
           "\"checksum\":%" PRIu32 "}\n",
           err == ESP_OK ? "true" : "false",
           run_index,
           stats->requested_frames,
           stats->completed_frames,
           stats->requested_frames - stats->completed_frames,
           (uint32_t)(stats->start_trigger_seen ? 0U : 1U),
           stats->ring_rearm_failures,
           stats->ring_slots,
           stats->descriptor_count_per_slot,
           stats->ring_rearms,
           stats->ring_rearm_failures,
           stats->unknown_eof_desc,
           stats->h_res,
           stats->v_res,
           stats->bytes_per_sample,
           stats->frame_bytes,
           stats->timeout_ms,
           stats->start_trigger_seen ? "true" : "false",
           SOURCE_RING_BENCH_TARGET_FPS,
           target_frame_us,
           target_rate_met ? "true" : "false",
           avg_capture_budget_pct,
           stats->elapsed_us,
           completed_fps,
           payload_mbytes_s,
           stats->first_frame_us,
           stats->avg_frame_interval_us,
           stats->max_frame_interval_us,
           stats->failure_stage == NULL ? "unknown" : stats->failure_stage,
           stats->last_esp_err,
           err,
           stats->checksum);
    fflush(stdout);
}

static void run_lowlevel_ring_variant(uint32_t *run_index, uint32_t width, uint32_t height)
{
    lcdcam_raw_ring_stats_t ring_stats = {0};
    esp_err_t err = lcdcam_raw_ring_bench(LCDCAM_RAW_DE_HIGH,
                                          width,
                                          height,
                                          SOURCE_RING_BENCH_TIMEOUT_MS,
                                          false,
                                          false,
                                          false,
                                          true,
                                          LCDCAM_RAW_START_AFTER_SPS_THEN_SPL_FALLING,
                                          false,
                                          LCDCAM_RAW_DATA_RGB565,
                                          SOURCE_RING_BENCH_FRAMES,
                                          &ring_stats);
    print_ring_result((*run_index)++, &ring_stats, err);
}

void app_main(void)
{
    ESP_LOGI(TAG, "isolated source-ring benchmark starting");
    ESP_LOGI(TAG, "no lab protocol, browser stream, destination SPI, or TinyUSB is initialized");
    ESP_ERROR_CHECK(lcdcam_raw_enter_safe_idle());
    print_ready();

    uint32_t run_index = 0;
    while (true) {
        lcdcam_raw_rearm_stats_t stats = {0};
        esp_err_t err = lcdcam_raw_rearm_bench(LCDCAM_RAW_DE_HIGH,
                                               GBC_LCD_SOURCE_CAPTURE_WIDTH,
                                               GBC_LCD_SOURCE_CAPTURE_HEIGHT,
                                               SOURCE_RING_BENCH_TIMEOUT_MS,
                                               false,
                                               false,
                                               false,
                                               true,
                                               LCDCAM_RAW_START_AFTER_SPS_THEN_SPL_FALLING,
                                               false,
                                               LCDCAM_RAW_DATA_RGB565,
                                               SOURCE_RING_BENCH_FRAMES,
                                               &stats);
        print_rearm_result(run_index, &stats, err);

        lcdcam_raw_ctlr_stats_t ctlr_stats = {0};
        err = lcdcam_raw_ctlr_bench(LCDCAM_RAW_DE_HIGH,
                                    GBC_LCD_SOURCE_CAPTURE_WIDTH,
                                    GBC_LCD_SOURCE_CAPTURE_HEIGHT,
                                    SOURCE_RING_BENCH_TIMEOUT_MS,
                                    false,
                                    false,
                                    false,
                                    true,
                                    LCDCAM_RAW_START_AFTER_SPS_THEN_SPL_FALLING,
                                    false,
                                    LCDCAM_RAW_DATA_RGB565,
                                    SOURCE_RING_BENCH_FRAMES,
                                    &ctlr_stats);
        print_ctlr_result(run_index++, &ctlr_stats, err);

        run_lowlevel_ring_variant(&run_index, GBC_LCD_SOURCE_CAPTURE_WIDTH, GBC_LCD_SOURCE_CAPTURE_HEIGHT);
        run_lowlevel_ring_variant(&run_index, GBC_LCD_SOURCE_VISIBLE_WIDTH, GBC_LCD_SOURCE_VISIBLE_HEIGHT);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
