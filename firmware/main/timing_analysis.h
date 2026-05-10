#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint32_t t_us;
    uint8_t gpio;
    uint8_t level;
    uint8_t red6;
} timing_edge_event_t;

typedef struct {
    const timing_edge_event_t *events;
    size_t event_count;
    uint32_t overflow_count;
    uint32_t duration_ms;
    int64_t start_time_us;
} timing_edge_capture_result_t;

typedef struct {
    uint32_t t_us;
    uint16_t index;
    uint8_t red6;
} red_dclk_sample_t;

typedef struct {
    const red_dclk_sample_t *samples;
    size_t sample_count;
    uint32_t requested_sample_count;
    uint32_t timeout_ms;
    int64_t trigger_time_us;
    bool trigger_seen;
    bool timeout;
} red_dclk_capture_result_t;

typedef struct {
    uint16_t index;
    uint32_t t_us;
    int32_t dclk_delta;
    int32_t dclk_total;
} line_clock_sample_t;

typedef struct {
    const line_clock_sample_t *samples;
    size_t sample_count;
    uint32_t requested_line_count;
    uint32_t timeout_ms;
    int marker_gpio;
    bool marker_falling_edge;
    bool frame_sync_seen;
    bool timeout;
    int32_t min_delta;
    int32_t max_delta;
    double mean_delta;
} line_clock_capture_result_t;

typedef struct {
    const uint8_t *pixels;
    const uint16_t *line_sample_counts;
    uint32_t width;
    uint32_t height;
    uint32_t captured_lines;
    uint32_t timeout_ms;
    bool frame_sync_seen;
    bool timeout;
    uint32_t checksum;
    uint32_t transition_count;
    uint32_t min_value;
    uint32_t max_value;
    bool sample_falling_edge;
    int marker_gpio;
    uint32_t skipped_markers;
    uint32_t dclk_delay_edges;
    uint32_t marker_stride;
    uint32_t marker_phase;
    bool stop_on_next_frame;
    bool next_frame_seen;
    uint32_t observed_markers;
} rg_line_burst_capture_result_t;

typedef struct {
    const uint8_t *pixels_rgb666;
    const uint16_t *line_sample_counts;
    uint32_t width;
    uint32_t height;
    uint32_t captured_lines;
    uint32_t timeout_ms;
    bool frame_sync_seen;
    bool timeout;
    uint32_t checksum;
    uint32_t transition_count;
    uint32_t min_value;
    uint32_t max_value;
    bool sample_falling_edge;
    int marker_gpio;
    uint32_t skipped_markers;
    uint32_t dclk_delay_edges;
    uint32_t marker_stride;
    uint32_t marker_phase;
    bool stop_on_next_frame;
    bool next_frame_seen;
    uint32_t observed_markers;
} rgb666_line_burst_capture_result_t;

esp_err_t timing_analysis_capture_edges(uint32_t duration_ms, timing_edge_capture_result_t *result);
esp_err_t timing_analysis_capture_red_on_dclk(uint32_t requested_sample_count, uint32_t timeout_ms, red_dclk_capture_result_t *result);
esp_err_t timing_analysis_capture_line_clocks(int marker_gpio,
                                              bool marker_falling_edge,
                                              uint32_t requested_line_count,
                                              uint32_t timeout_ms,
                                              line_clock_capture_result_t *result);
esp_err_t timing_analysis_capture_rg_line_bursts(uint32_t width,
                                                 uint32_t height,
                                                 uint32_t timeout_ms,
                                                 bool sample_falling_edge,
                                                 int marker_gpio,
                                                 uint32_t skip_markers,
                                                 uint32_t dclk_delay_edges,
                                                 uint32_t marker_stride,
                                                 uint32_t marker_phase,
                                                 bool stop_on_next_frame,
                                                 rg_line_burst_capture_result_t *result);
esp_err_t timing_analysis_capture_rgb666_line_bursts(uint32_t width,
                                                     uint32_t height,
                                                     uint32_t timeout_ms,
                                                     bool sample_falling_edge,
                                                     int marker_gpio,
                                                     uint32_t skip_markers,
                                                     uint32_t dclk_delay_edges,
                                                     uint32_t marker_stride,
                                                     uint32_t marker_phase,
                                                     bool stop_on_next_frame,
                                                     rgb666_line_burst_capture_result_t *result);
const char *timing_analysis_signal_name_for_gpio(int gpio_num);
bool timing_analysis_is_phase1_signal_gpio(int gpio_num);
