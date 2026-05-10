#include "usb_protocol.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "app_version.h"
#include "diagnostics.h"
#include "dvp_probe.h"
#include "gbc_lcd_source.h"
#include "gpio_sampler.h"
#include "lcdcam_raw.h"
#include "pinmap_gbc.h"
#include "pipeline_bench.h"
#include "source_pipeline_bench.h"
#include "timing_analysis.h"

static const char *TAG = "usb_protocol";

#if CONFIG_FREERTOS_NUMBER_OF_CORES > 1
#define USB_PROTOCOL_TASK_CORE 1
#else
#define USB_PROTOCOL_TASK_CORE 0
#endif

#define USB_PROTOCOL_TASK_STACK_WORDS 4096
#define USB_PROTOCOL_TASK_PRIORITY 5
#define USB_BENCH_MAX_FRAMES 256
#define USB_BENCH_MAX_PAYLOAD_LEN (256U * 1024U)
#define USB_BENCH_CHUNK_LEN 1024U
#define GBC_CAPCARD_FRAME_HEADER_LEN 20U

static void trim_line(char *line)
{
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' || isspace((unsigned char)line[len - 1]))) {
        line[len - 1] = '\0';
        len--;
    }
}

static void print_no_capture_pins_configured(const char *command)
{
    diagnostics_record_unsupported_command();
    printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"no_capture_pins_configured\","
           "\"phase\":\"%s\",\"capture_pin_count\":%u}\n",
           command,
           GBC_P4_PROBE_PHASE,
           (unsigned)GBC_CAPTURE_PIN_COUNT);
}

static bool command_has_prefix(const char *line, const char *command)
{
    size_t command_len = strlen(command);
    return strncmp(line, command, command_len) == 0 &&
           (line[command_len] == '\0' || isspace((unsigned char)line[command_len]));
}

static const char *bool_json(bool value)
{
    return value ? "true" : "false";
}

static void handle_transport_status(void)
{
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    const bool console_usb_serial_jtag = true;
#else
    const bool console_usb_serial_jtag = false;
#endif

#if CONFIG_ESP_CONSOLE_UART_DEFAULT || CONFIG_ESP_CONSOLE_UART_CUSTOM
    const bool console_uart = true;
#else
    const bool console_uart = false;
#endif

#if CONFIG_ESP_CONSOLE_SECONDARY_NONE
    const bool secondary_console_none = true;
#else
    const bool secondary_console_none = false;
#endif

    printf("{\"ok\":true,\"command\":\"TRANSPORT_STATUS\","
           "\"app_data_plane\":\"native_usb_serial_jtag\","
           "\"recovery_plane\":\"wch_uart_rom\","
           "\"uart_frame_streaming_allowed\":false,"
           "\"console_usb_serial_jtag\":%s,"
           "\"console_uart\":%s,"
           "\"secondary_console_none\":%s,"
           "\"binary_stream_rule\":\"no_logs_on_data_plane\","
           "\"performance_target\":\"gbc_source_rate_59_7fps\"}\n",
           bool_json(console_usb_serial_jtag),
           bool_json(console_uart),
           bool_json(secondary_console_none));
}

static void print_pinmap(void)
{
    printf("{\"ok\":true,\"phase\":\"%s\",\"capture_pin_count\":%u,\"pins\":[",
           GBC_P4_PROBE_PHASE,
           (unsigned)GBC_CAPTURE_PIN_COUNT);

#if GBC_CAPTURE_PIN_COUNT > 0
    for (size_t i = 0; i < GBC_CAPTURE_PIN_COUNT; ++i) {
        printf("%s{\"name\":\"%s\",\"gpio\":%d,\"mode\":\"input_only\"}",
               i == 0 ? "" : ",",
               GBC_CAPTURE_PINS[i].name,
               GBC_CAPTURE_PINS[i].gpio);
    }
#endif

    printf("]}\n");
}

static void print_core_status(void)
{
    printf("{\"ok\":true,\"command\":\"CORE_STATUS\","
           "\"freertos_cores\":%d,"
           "\"usb_protocol_task_pinned_core\":%d,"
           "\"current_core\":%d,"
           "\"main_task_affinity\":\"%s\","
           "\"esp_timer_task_affinity\":\"%s\","
           "\"esp_timer_isr_affinity\":\"%s\"}\n",
           CONFIG_FREERTOS_NUMBER_OF_CORES,
           USB_PROTOCOL_TASK_CORE,
           (int)xPortGetCoreID(),
#if CONFIG_ESP_MAIN_TASK_AFFINITY_CPU0
           "CPU0",
#elif CONFIG_ESP_MAIN_TASK_AFFINITY_CPU1
           "CPU1",
#else
           "NO_AFFINITY",
#endif
#if CONFIG_ESP_TIMER_TASK_AFFINITY_CPU0
           "CPU0",
#elif CONFIG_ESP_TIMER_TASK_AFFINITY_CPU1
           "CPU1",
#else
           "NO_AFFINITY",
#endif
#if CONFIG_ESP_TIMER_ISR_AFFINITY_CPU0
           "CPU0"
#elif CONFIG_ESP_TIMER_ISR_AFFINITY_CPU1
           "CPU1"
#else
           "NO_AFFINITY"
#endif
    );
}

static void handle_read_gpio(const char *line)
{
    int gpio_num = -1;
    if (sscanf(line, "READ_GPIO %d", &gpio_num) != 1) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_arguments\"}\n", line);
        return;
    }

    if (!gpio_sampler_is_test_gpio_allowed(gpio_num)) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"gpio_not_allowlisted\","
               "\"gpio\":%d,\"mode\":\"input_only\"}\n",
               line,
               gpio_num);
        return;
    }

    int level = 0;
    esp_err_t err = gpio_sampler_read_test_gpio(gpio_num, &level);
    if (err != ESP_OK) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"gpio_read_failed\","
               "\"gpio\":%d,\"esp_err\":%d}\n",
               line,
               gpio_num,
               err);
        return;
    }

    printf("{\"ok\":true,\"command\":\"READ_GPIO\",\"gpio\":%d,\"mode\":\"input_only\","
           "\"pull_up\":false,\"pull_down\":false,\"level\":%d}\n",
           gpio_num,
           level);
}

static void handle_count_gpio_edges(const char *line)
{
    int gpio_num = -1;
    int duration_ms = 0;
    if (sscanf(line, "COUNT_GPIO_EDGES %d %d", &gpio_num, &duration_ms) != 2 || duration_ms <= 0) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_arguments\"}\n", line);
        return;
    }

    if (!gpio_sampler_is_test_gpio_allowed(gpio_num)) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"gpio_not_allowlisted\","
               "\"gpio\":%d,\"mode\":\"input_only\"}\n",
               line,
               gpio_num);
        return;
    }

    uint32_t rising_edges = 0;
    uint32_t falling_edges = 0;
    esp_err_t err = gpio_sampler_count_test_gpio_edges(gpio_num, (uint32_t)duration_ms, &rising_edges, &falling_edges);
    if (err != ESP_OK) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"edge_count_failed\","
               "\"gpio\":%d,\"esp_err\":%d}\n",
               line,
               gpio_num,
               err);
        return;
    }

    uint32_t total_edges = rising_edges + falling_edges;
    double rising_hz = ((double)rising_edges * 1000.0) / (double)duration_ms;
    double falling_hz = ((double)falling_edges * 1000.0) / (double)duration_ms;
    printf("{\"ok\":true,\"command\":\"COUNT_GPIO_EDGES\",\"gpio\":%d,\"mode\":\"input_only\","
           "\"pull_up\":false,\"pull_down\":false,\"duration_ms\":%d,"
           "\"rising_edges\":%lu,\"falling_edges\":%lu,\"total_edges\":%lu,"
           "\"rising_hz\":%.3f,\"falling_hz\":%.3f}\n",
           gpio_num,
           duration_ms,
           (unsigned long)rising_edges,
           (unsigned long)falling_edges,
           (unsigned long)total_edges,
           rising_hz,
           falling_hz);
}

static void handle_measure_dclk(const char *line)
{
    int gpio_num = -1;
    int duration_ms = 0;
    if (sscanf(line, "MEASURE_DCLK %d %d", &gpio_num, &duration_ms) != 2 || duration_ms <= 0 || duration_ms > 10000) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_arguments\","
               "\"usage\":\"MEASURE_DCLK <gpio> <duration_ms_1_to_10000>\"}\n",
               line);
        return;
    }

    if (!gpio_sampler_is_test_gpio_allowed(gpio_num)) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"gpio_not_allowlisted\","
               "\"gpio\":%d,\"mode\":\"input_only\"}\n",
               line,
               gpio_num);
        return;
    }

    int edge_count = 0;
    esp_err_t err = gpio_sampler_measure_clock_pcnt(gpio_num, (uint32_t)duration_ms, &edge_count);
    if (err != ESP_OK) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"pcnt_measure_failed\","
               "\"gpio\":%d,\"esp_err\":%d}\n",
               line,
               gpio_num,
               err);
        return;
    }

    double rising_edge_hz = ((double)edge_count * 1000.0) / (double)duration_ms;
    printf("{\"ok\":true,\"command\":\"MEASURE_DCLK\",\"gpio\":%d,\"mode\":\"input_only\","
           "\"method\":\"esp_idf_pcnt_rising_edges\",\"pull_up\":false,\"pull_down\":false,"
           "\"duration_ms\":%d,\"rising_edges\":%d,\"rising_edge_hz\":%.3f}\n",
           gpio_num,
           duration_ms,
           edge_count,
           rising_edge_hz);
}

static void handle_capture_timing_edges(const char *line)
{
    int duration_ms = 0;
    if (sscanf(line, "CAPTURE_TIMING_EDGES %d", &duration_ms) != 1 || duration_ms <= 0 || duration_ms > 250) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_arguments\","
               "\"usage\":\"CAPTURE_TIMING_EDGES <duration_ms_1_to_250>\"}\n",
               line);
        return;
    }

    timing_edge_capture_result_t result = {0};
    esp_err_t err = timing_analysis_capture_edges((uint32_t)duration_ms, &result);
    if (err != ESP_OK) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"timing_capture_failed\","
               "\"esp_err\":%d}\n",
               line,
               err);
        return;
    }

    printf("{\"ok\":true,\"command\":\"CAPTURE_TIMING_EDGES\","
           "\"mode\":\"input_only\",\"duration_ms\":%lu,\"start_time_us\":%lld,"
           "\"event_count\":%lu,\"overflow_count\":%lu,"
           "\"red_mapping\":["
           "{\"name\":\"R0\",\"gpio\":18,\"bit\":0},"
           "{\"name\":\"R1\",\"gpio\":17,\"bit\":1},"
           "{\"name\":\"R2\",\"gpio\":16,\"bit\":2},"
           "{\"name\":\"R3\",\"gpio\":15,\"bit\":3},"
           "{\"name\":\"R4\",\"gpio\":14,\"bit\":4},"
           "{\"name\":\"R5\",\"gpio\":13,\"bit\":5}],"
           "\"signals\":["
           "{\"name\":\"SPL\",\"gpio\":19},"
           "{\"name\":\"PS\",\"gpio\":20},"
           "{\"name\":\"LP\",\"gpio\":21},"
           "{\"name\":\"CLS\",\"gpio\":3},"
           "{\"name\":\"SPS\",\"gpio\":33}],"
           "\"events\":[",
           (unsigned long)result.duration_ms,
           (long long)result.start_time_us,
           (unsigned long)result.event_count,
           (unsigned long)result.overflow_count);

    for (size_t i = 0; i < result.event_count; ++i) {
        const timing_edge_event_t *event = &result.events[i];
        printf("%s{\"t_us\":%lu,\"gpio\":%u,\"signal\":\"%s\",\"level\":%u,\"red6\":%u}",
               i == 0 ? "" : ",",
               (unsigned long)event->t_us,
               (unsigned)event->gpio,
               timing_analysis_signal_name_for_gpio(event->gpio),
               (unsigned)event->level,
               (unsigned)event->red6);
    }

    printf("]}\n");
}

static void handle_capture_red_dclk(const char *line)
{
    int sample_count = 0;
    int timeout_ms = 0;
    if (sscanf(line, "CAPTURE_RED_DCLK %d %d", &sample_count, &timeout_ms) != 2 ||
        sample_count <= 0 || sample_count > 2048 || timeout_ms <= 0 || timeout_ms > 1000) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_arguments\","
               "\"usage\":\"CAPTURE_RED_DCLK <sample_count_1_to_2048> <timeout_ms_1_to_1000>\"}\n",
               line);
        return;
    }

    red_dclk_capture_result_t result = {0};
    esp_err_t err = timing_analysis_capture_red_on_dclk((uint32_t)sample_count, (uint32_t)timeout_ms, &result);
    if (err != ESP_OK) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"red_dclk_capture_failed\","
               "\"esp_err\":%d}\n",
               line,
               err);
        return;
    }

    printf("{\"ok\":true,\"command\":\"CAPTURE_RED_DCLK\","
           "\"mode\":\"input_only_polling\",\"trigger\":\"SPL falling\",\"sample_clock\":\"DCLK rising\","
           "\"requested_sample_count\":%lu,\"sample_count\":%lu,\"timeout_ms\":%lu,"
           "\"trigger_seen\":%s,\"timeout\":%s,\"trigger_time_us\":%lld,"
           "\"red_mapping\":["
           "{\"name\":\"R0\",\"gpio\":18,\"bit\":0},"
           "{\"name\":\"R1\",\"gpio\":17,\"bit\":1},"
           "{\"name\":\"R2\",\"gpio\":16,\"bit\":2},"
           "{\"name\":\"R3\",\"gpio\":15,\"bit\":3},"
           "{\"name\":\"R4\",\"gpio\":14,\"bit\":4},"
           "{\"name\":\"R5\",\"gpio\":13,\"bit\":5}],"
           "\"samples\":[",
           (unsigned long)result.requested_sample_count,
           (unsigned long)result.sample_count,
           (unsigned long)result.timeout_ms,
           result.trigger_seen ? "true" : "false",
           result.timeout ? "true" : "false",
           (long long)result.trigger_time_us);

    for (size_t i = 0; i < result.sample_count; ++i) {
        const red_dclk_sample_t *sample = &result.samples[i];
        printf("%s{\"index\":%u,\"t_us\":%lu,\"red6\":%u}",
               i == 0 ? "" : ",",
               (unsigned)sample->index,
               (unsigned long)sample->t_us,
               (unsigned)sample->red6);
    }

    printf("]}\n");
}

static bool parse_line_marker(const char *name, int *gpio_num)
{
    if (strcmp(name, "SPL") == 0) {
        *gpio_num = 19;
        return true;
    }
    if (strcmp(name, "LP") == 0) {
        *gpio_num = 21;
        return true;
    }
    return false;
}

static void handle_capture_line_clocks(const char *line)
{
    char marker_name[8] = {0};
    char edge_name[12] = {0};
    int line_count = 0;
    int timeout_ms = 0;
    if (sscanf(line, "CAPTURE_LINE_CLOCKS %7s %11s %d %d", marker_name, edge_name, &line_count, &timeout_ms) != 4 ||
        line_count <= 0 || line_count > 256 || timeout_ms <= 0 || timeout_ms > 5000) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_arguments\","
               "\"usage\":\"CAPTURE_LINE_CLOCKS <LP|SPL> <falling|rising> <line_count_1_to_256> <timeout_ms_1_to_5000>\"}\n",
               line);
        return;
    }

    int marker_gpio = -1;
    if (!parse_line_marker(marker_name, &marker_gpio)) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_marker\","
               "\"allowed\":[\"LP\",\"SPL\"]}\n",
               line);
        return;
    }

    bool falling_edge = false;
    if (strcmp(edge_name, "falling") == 0) {
        falling_edge = true;
    } else if (strcmp(edge_name, "rising") == 0) {
        falling_edge = false;
    } else {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_edge\","
               "\"allowed\":[\"falling\",\"rising\"]}\n",
               line);
        return;
    }

    line_clock_capture_result_t result = {0};
    esp_err_t err = timing_analysis_capture_line_clocks(marker_gpio,
                                                        falling_edge,
                                                        (uint32_t)line_count,
                                                        (uint32_t)timeout_ms,
                                                        &result);
    if (err != ESP_OK) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"line_clock_capture_failed\","
               "\"esp_err\":%d}\n",
               line,
               err);
        return;
    }

    printf("{\"ok\":true,\"command\":\"CAPTURE_LINE_CLOCKS\","
           "\"mode\":\"input_only_pcnt_dclk_marker_snapshots\","
           "\"dclk\":\"DCLK\",\"dclk_gpio\":22,"
           "\"frame_sync\":\"SPS\",\"frame_sync_gpio\":33,\"frame_sync_edge\":\"rising\","
           "\"marker\":\"%s\",\"marker_gpio\":%d,\"marker_edge\":\"%s\","
           "\"requested_line_count\":%lu,\"sample_count\":%lu,\"timeout_ms\":%lu,"
           "\"frame_sync_seen\":%s,\"timeout\":%s,"
           "\"min_delta\":%ld,\"max_delta\":%ld,\"mean_delta\":%.3f,"
           "\"samples\":[",
           marker_name,
           marker_gpio,
           falling_edge ? "falling" : "rising",
           (unsigned long)result.requested_line_count,
           (unsigned long)result.sample_count,
           (unsigned long)result.timeout_ms,
           result.frame_sync_seen ? "true" : "false",
           result.timeout ? "true" : "false",
           (long)result.min_delta,
           (long)result.max_delta,
           result.mean_delta);

    for (size_t i = 0; i < result.sample_count; ++i) {
        const line_clock_sample_t *sample = &result.samples[i];
        printf("%s{\"index\":%u,\"t_us\":%lu,\"dclk_delta\":%ld,\"dclk_total\":%ld}",
               i == 0 ? "" : ",",
               (unsigned)sample->index,
               (unsigned long)sample->t_us,
               (long)sample->dclk_delta,
               (long)sample->dclk_total);
    }

    printf("]}\n");
}

static void handle_capture_rg_line_bursts(const char *line)
{
    int width = 0;
    int height = 0;
    int timeout_ms = 0;
    char edge_name[12] = "rising";
    char marker_name[8] = "SPL";
    int skip_markers = 0;
    int dclk_delay_edges = 0;
    int marker_stride = 1;
    int marker_phase = 0;
    int stop_on_next_frame = 0;
    int parsed = sscanf(line,
                        "CAPTURE_RG_LINE_BURSTS %d %d %d %11s %7s %d %d %d %d %d",
                        &width,
                        &height,
                        &timeout_ms,
                        edge_name,
                        marker_name,
                        &skip_markers,
                        &dclk_delay_edges,
                        &marker_stride,
                        &marker_phase,
                        &stop_on_next_frame);
    if (parsed < 3 ||
        width <= 0 || width > 160 || height <= 0 || height > 144 ||
        timeout_ms <= 0 || timeout_ms > 5000 ||
        skip_markers < 0 || skip_markers > 32 ||
        dclk_delay_edges < 0 || dclk_delay_edges > 64 ||
        marker_stride <= 0 || marker_stride > 16 ||
        marker_phase < 0 || marker_phase >= marker_stride ||
        stop_on_next_frame < 0 || stop_on_next_frame > 1) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_arguments\","
               "\"usage\":\"CAPTURE_RG_LINE_BURSTS <width_1_to_160> <height_1_to_144> <timeout_ms_1_to_5000> [rising|falling] [SPL|LP] [skip_markers_0_to_32] [dclk_delay_edges_0_to_64] [marker_stride_1_to_16] [marker_phase_0_to_stride_minus_1] [stop_on_next_frame_0_or_1]\"}\n",
               line);
        return;
    }
    bool sample_falling_edge = false;
    if (strcmp(edge_name, "rising") == 0) {
        sample_falling_edge = false;
    } else if (strcmp(edge_name, "falling") == 0) {
        sample_falling_edge = true;
    } else {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_edge\","
               "\"allowed\":[\"rising\",\"falling\"]}\n",
               line);
        return;
    }
    int marker_gpio = -1;
    if (!parse_line_marker(marker_name, &marker_gpio)) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_marker\","
               "\"allowed\":[\"LP\",\"SPL\"]}\n",
               line);
        return;
    }

    rg_line_burst_capture_result_t result = {0};
    esp_err_t err = timing_analysis_capture_rg_line_bursts((uint32_t)width,
                                                           (uint32_t)height,
                                                           (uint32_t)timeout_ms,
                                                           sample_falling_edge,
                                                           marker_gpio,
                                                           (uint32_t)skip_markers,
                                                           (uint32_t)dclk_delay_edges,
                                                           (uint32_t)marker_stride,
                                                           (uint32_t)marker_phase,
                                                           stop_on_next_frame != 0,
                                                           &result);
    if (err != ESP_OK) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"rg_line_burst_capture_failed\","
               "\"esp_err\":%d}\n",
               line,
               err);
        return;
    }

    printf("{\"ok\":true,\"command\":\"CAPTURE_RG_LINE_BURSTS\","
           "\"mode\":\"experimental_cpu_polled_spl_dclk_line_bursts\","
           "\"width\":%lu,\"height\":%lu,\"packing\":\"rg44_upper_bits\","
           "\"frame_sync\":\"SPS\",\"frame_sync_gpio\":33,\"frame_sync_edge\":\"rising\","
           "\"line_start\":\"%s\",\"line_start_gpio\":%d,\"line_start_edge\":\"falling\","
           "\"skip_markers\":%lu,"
           "\"dclk_delay_edges\":%lu,\"marker_stride\":%lu,\"marker_phase\":%lu,"
           "\"stop_on_next_frame\":%s,\"next_frame_seen\":%s,\"observed_markers\":%lu,"
           "\"sample_clock\":\"DCLK\",\"sample_clock_gpio\":22,\"sample_clock_edge\":\"%s\","
           "\"timeout_ms\":%lu,\"frame_sync_seen\":%s,\"timeout\":%s,"
           "\"captured_lines\":%lu,\"checksum\":%lu,\"transition_count\":%lu,"
           "\"min_value\":%lu,\"max_value\":%lu,"
           "\"data_mapping\":["
           "{\"bit\":0,\"signal\":\"R2\",\"gpio\":16},"
           "{\"bit\":1,\"signal\":\"R3\",\"gpio\":15},"
           "{\"bit\":2,\"signal\":\"R4\",\"gpio\":14},"
           "{\"bit\":3,\"signal\":\"R5\",\"gpio\":13},"
           "{\"bit\":4,\"signal\":\"G2\",\"gpio\":10},"
           "{\"bit\":5,\"signal\":\"G3\",\"gpio\":9},"
           "{\"bit\":6,\"signal\":\"G4\",\"gpio\":8},"
           "{\"bit\":7,\"signal\":\"G5\",\"gpio\":7}],"
           "\"line_sample_counts\":[",
           (unsigned long)result.width,
           (unsigned long)result.height,
           result.marker_gpio == 21 ? "LP" : "SPL",
           result.marker_gpio,
           (unsigned long)result.skipped_markers,
           (unsigned long)result.dclk_delay_edges,
           (unsigned long)result.marker_stride,
           (unsigned long)result.marker_phase,
           result.stop_on_next_frame ? "true" : "false",
           result.next_frame_seen ? "true" : "false",
           (unsigned long)result.observed_markers,
           result.sample_falling_edge ? "falling" : "rising",
           (unsigned long)result.timeout_ms,
           result.frame_sync_seen ? "true" : "false",
           result.timeout ? "true" : "false",
           (unsigned long)result.captured_lines,
           (unsigned long)result.checksum,
           (unsigned long)result.transition_count,
           (unsigned long)result.min_value,
           (unsigned long)result.max_value);

    for (uint32_t i = 0; i < result.height; ++i) {
        printf("%s%u", i == 0 ? "" : ",", (unsigned)result.line_sample_counts[i]);
    }

    printf("],\"data_hex\":\"");
    for (uint32_t i = 0; i < result.width * result.height; ++i) {
        printf("%02x", (unsigned)result.pixels[i]);
    }
    printf("\"}\n");
}

static void handle_capture_rgb666_line_bursts(const char *line)
{
    int width = 0;
    int height = 0;
    int timeout_ms = 0;
    char edge_name[12] = "rising";
    char marker_name[8] = "SPL";
    int skip_markers = 0;
    int dclk_delay_edges = 0;
    int marker_stride = 1;
    int marker_phase = 0;
    int stop_on_next_frame = 0;
    int parsed = sscanf(line,
                        "CAPTURE_RGB666_LINE_BURSTS %d %d %d %11s %7s %d %d %d %d %d",
                        &width,
                        &height,
                        &timeout_ms,
                        edge_name,
                        marker_name,
                        &skip_markers,
                        &dclk_delay_edges,
                        &marker_stride,
                        &marker_phase,
                        &stop_on_next_frame);
    if (parsed < 3 ||
        width <= 0 || width > 160 || height <= 0 || height > 144 ||
        timeout_ms <= 0 || timeout_ms > 5000 ||
        skip_markers < 0 || skip_markers > 32 ||
        dclk_delay_edges < 0 || dclk_delay_edges > 64 ||
        marker_stride <= 0 || marker_stride > 16 ||
        marker_phase < 0 || marker_phase >= marker_stride ||
        stop_on_next_frame < 0 || stop_on_next_frame > 1) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_arguments\","
               "\"usage\":\"CAPTURE_RGB666_LINE_BURSTS <width_1_to_160> <height_1_to_144> <timeout_ms_1_to_5000> [rising|falling] [SPL|LP] [skip_markers_0_to_32] [dclk_delay_edges_0_to_64] [marker_stride_1_to_16] [marker_phase_0_to_stride_minus_1] [stop_on_next_frame_0_or_1]\"}\n",
               line);
        return;
    }
    bool sample_falling_edge = false;
    if (strcmp(edge_name, "rising") == 0) {
        sample_falling_edge = false;
    } else if (strcmp(edge_name, "falling") == 0) {
        sample_falling_edge = true;
    } else {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_edge\","
               "\"allowed\":[\"rising\",\"falling\"]}\n",
               line);
        return;
    }
    int marker_gpio = -1;
    if (!parse_line_marker(marker_name, &marker_gpio)) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_marker\","
               "\"allowed\":[\"LP\",\"SPL\"]}\n",
               line);
        return;
    }

    rgb666_line_burst_capture_result_t result = {0};
    esp_err_t err = timing_analysis_capture_rgb666_line_bursts((uint32_t)width,
                                                               (uint32_t)height,
                                                               (uint32_t)timeout_ms,
                                                               sample_falling_edge,
                                                               marker_gpio,
                                                               (uint32_t)skip_markers,
                                                               (uint32_t)dclk_delay_edges,
                                                               (uint32_t)marker_stride,
                                                               (uint32_t)marker_phase,
                                                               stop_on_next_frame != 0,
                                                               &result);
    if (err != ESP_OK) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"rgb666_line_burst_capture_failed\","
               "\"esp_err\":%d}\n",
               line,
               err);
        return;
    }

    printf("{\"ok\":true,\"command\":\"CAPTURE_RGB666_LINE_BURSTS\","
           "\"mode\":\"experimental_cpu_polled_spl_dclk_rgb666_line_bursts\","
           "\"width\":%lu,\"height\":%lu,\"packing\":\"rgb666_3bytes_per_pixel_r6_g6_b6\","
           "\"bytes_per_pixel\":3,"
           "\"frame_sync\":\"SPS\",\"frame_sync_gpio\":33,\"frame_sync_edge\":\"rising\","
           "\"line_start\":\"%s\",\"line_start_gpio\":%d,\"line_start_edge\":\"falling\","
           "\"skip_markers\":%lu,"
           "\"dclk_delay_edges\":%lu,\"marker_stride\":%lu,\"marker_phase\":%lu,"
           "\"stop_on_next_frame\":%s,\"next_frame_seen\":%s,\"observed_markers\":%lu,"
           "\"sample_clock\":\"DCLK\",\"sample_clock_gpio\":22,\"sample_clock_edge\":\"%s\","
           "\"timeout_ms\":%lu,\"frame_sync_seen\":%s,\"timeout\":%s,"
           "\"captured_lines\":%lu,\"checksum\":%lu,\"transition_count\":%lu,"
           "\"min_value\":%lu,\"max_value\":%lu,"
           "\"data_mapping\":["
           "{\"byte\":0,\"signal\":\"R0-R5\",\"gpios\":[18,17,16,15,14,13]},"
           "{\"byte\":1,\"signal\":\"G0-G5\",\"gpios\":[12,11,10,9,8,7]},"
           "{\"byte\":2,\"signal\":\"B0-B5\",\"gpios\":[36,45,46,47,48,50]}],"
           "\"line_sample_counts\":[",
           (unsigned long)result.width,
           (unsigned long)result.height,
           result.marker_gpio == 21 ? "LP" : "SPL",
           result.marker_gpio,
           (unsigned long)result.skipped_markers,
           (unsigned long)result.dclk_delay_edges,
           (unsigned long)result.marker_stride,
           (unsigned long)result.marker_phase,
           result.stop_on_next_frame ? "true" : "false",
           result.next_frame_seen ? "true" : "false",
           (unsigned long)result.observed_markers,
           result.sample_falling_edge ? "falling" : "rising",
           (unsigned long)result.timeout_ms,
           result.frame_sync_seen ? "true" : "false",
           result.timeout ? "true" : "false",
           (unsigned long)result.captured_lines,
           (unsigned long)result.checksum,
           (unsigned long)result.transition_count,
           (unsigned long)result.min_value,
           (unsigned long)result.max_value);

    for (uint32_t i = 0; i < result.height; ++i) {
        printf("%s%u", i == 0 ? "" : ",", (unsigned)result.line_sample_counts[i]);
    }

    printf("],\"data_hex\":\"");
    uint32_t byte_count = result.width * result.height * 3U;
    for (uint32_t i = 0; i < byte_count; ++i) {
        printf("%02x", (unsigned)result.pixels_rgb666[i]);
    }
    printf("\"}\n");
}

static void handle_dvp_probe_alloc(void)
{
    dvp_probe_alloc_result_t result = {0};
    esp_err_t err = dvp_probe_allocate_raw8(&result);
    if (err != ESP_OK) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"DVP_PROBE_ALLOC\",\"error\":\"dvp_alloc_failed\","
               "\"esp_err\":%d}\n",
               err);
        return;
    }

    printf("{\"ok\":true,\"command\":\"DVP_PROBE_ALLOC\","
           "\"mode\":\"driver_allocation_only_no_gpio_routing\","
           "\"controller_count\":%lu,\"max_data_width\":%lu,"
           "\"configured_width\":%lu,\"color\":\"RAW8\","
           "\"h_res\":%lu,\"v_res\":%lu,\"frame_buffer_len\":%u,"
           "\"backup_buffer_disabled\":%s,"
           "\"notes\":\"generic_dvp_driver_requires_8_bit_data_width_gpio_pin_init_is_disabled_and_capture_must_supply_dma_buffers\"}\n",
           (unsigned long)result.controller_count,
           (unsigned long)result.max_data_width,
           (unsigned long)result.configured_width,
           (unsigned long)result.h_res,
           (unsigned long)result.v_res,
           (unsigned)result.frame_buffer_len,
           result.backup_buffer_disabled ? "true" : "false");
}

static bool parse_dvp_de_source(const char *name, dvp_probe_de_source_t *de_source)
{
    if (strcmp(name, "SPL") == 0) {
        *de_source = DVP_PROBE_DE_SPL;
        return true;
    }
    if (strcmp(name, "LP") == 0) {
        *de_source = DVP_PROBE_DE_LP;
        return true;
    }
    return false;
}

static const char *dvp_de_source_name(dvp_probe_de_source_t de_source)
{
    return de_source == DVP_PROBE_DE_LP ? "LP" : "SPL";
}

static bool parse_dvp_sync_source(const char *name, dvp_probe_sync_source_t *source)
{
    if (strcmp(name, "SPL") == 0) {
        *source = DVP_PROBE_SYNC_SPL;
        return true;
    }
    if (strcmp(name, "LP") == 0) {
        *source = DVP_PROBE_SYNC_LP;
        return true;
    }
    if (strcmp(name, "NC") == 0 || strcmp(name, "NONE") == 0) {
        *source = DVP_PROBE_SYNC_NONE;
        return true;
    }
    return false;
}

static const char *dvp_sync_source_name(dvp_probe_sync_source_t source)
{
    switch (source) {
    case DVP_PROBE_SYNC_SPL:
        return "SPL";
    case DVP_PROBE_SYNC_LP:
        return "LP";
    case DVP_PROBE_SYNC_NONE:
    default:
        return "NC";
    }
}

static int dvp_sync_source_gpio(dvp_probe_sync_source_t source)
{
    switch (source) {
    case DVP_PROBE_SYNC_SPL:
        return 19;
    case DVP_PROBE_SYNC_LP:
        return 21;
    case DVP_PROBE_SYNC_NONE:
    default:
        return -1;
    }
}

static const char *lcdcam_raw_de_source_name(lcdcam_raw_de_source_t de_source)
{
    if (de_source == LCDCAM_RAW_DE_LP) {
        return "LP";
    }
    if (de_source == LCDCAM_RAW_DE_HIGH) {
        return "HIGH";
    }
    return "SPL";
}

static int lcdcam_raw_de_source_gpio(lcdcam_raw_de_source_t de_source)
{
    if (de_source == LCDCAM_RAW_DE_LP) {
        return 21;
    }
    if (de_source == LCDCAM_RAW_DE_HIGH) {
        return -1;
    }
    return 19;
}

static bool parse_lcdcam_raw_de_source(const char *name, lcdcam_raw_de_source_t *de_source)
{
    if (strcmp(name, "SPL") == 0) {
        *de_source = LCDCAM_RAW_DE_SPL;
        return true;
    }
    if (strcmp(name, "LP") == 0) {
        *de_source = LCDCAM_RAW_DE_LP;
        return true;
    }
    if (strcmp(name, "HIGH") == 0) {
        *de_source = LCDCAM_RAW_DE_HIGH;
        return true;
    }
    return false;
}

static const char *lcdcam_raw_start_mode_name(lcdcam_raw_start_mode_t start_mode)
{
    switch (start_mode) {
    case LCDCAM_RAW_START_AFTER_SPS_RISING:
        return "AFTER_SPS";
    case LCDCAM_RAW_START_AFTER_SPS_THEN_SPL_FALLING:
        return "AFTER_SPS_SPL";
    case LCDCAM_RAW_START_IMMEDIATE:
    default:
        return "IMMEDIATE";
    }
}

static const char *lcdcam_raw_data_mode_color(lcdcam_raw_data_mode_t data_mode)
{
    return (data_mode == LCDCAM_RAW_DATA_RGB664 || data_mode == LCDCAM_RAW_DATA_RGB565) ? "RAW16" : "RAW8";
}

static const char *lcdcam_raw_data_mode_packing(lcdcam_raw_data_mode_t data_mode)
{
    switch (data_mode) {
    case LCDCAM_RAW_DATA_RGB332:
        return "rgb332_upper_bits";
    case LCDCAM_RAW_DATA_RGB664:
        return "rgb664_r0_r5_g0_g5_b2_b5";
    case LCDCAM_RAW_DATA_RGB565:
        return "rgb565_upper_bits_standard";
    case LCDCAM_RAW_DATA_RG44:
    default:
        return "rg44_upper_bits";
    }
}

static const char *lcdcam_raw_data_mode_name(lcdcam_raw_data_mode_t data_mode)
{
    switch (data_mode) {
    case LCDCAM_RAW_DATA_RGB332:
        return "RGB332";
    case LCDCAM_RAW_DATA_RGB664:
        return "RGB664";
    case LCDCAM_RAW_DATA_RGB565:
        return "RGB565";
    case LCDCAM_RAW_DATA_RG44:
    default:
        return "RG44";
    }
}

static int lcdcam_raw_data_mode_bytes_per_sample(lcdcam_raw_data_mode_t data_mode)
{
    return (data_mode == LCDCAM_RAW_DATA_RGB664 || data_mode == LCDCAM_RAW_DATA_RGB565) ? 2 : 1;
}

static bool parse_lcdcam_raw_data_mode(const char *name, lcdcam_raw_data_mode_t *data_mode)
{
    if (strcmp(name, "RG44") == 0) {
        *data_mode = LCDCAM_RAW_DATA_RG44;
        return true;
    }
    if (strcmp(name, "RGB332") == 0) {
        *data_mode = LCDCAM_RAW_DATA_RGB332;
        return true;
    }
    if (strcmp(name, "RGB664") == 0) {
        *data_mode = LCDCAM_RAW_DATA_RGB664;
        return true;
    }
    if (strcmp(name, "RGB565") == 0) {
        *data_mode = LCDCAM_RAW_DATA_RGB565;
        return true;
    }
    return false;
}

static void print_lcdcam_raw_result(lcdcam_raw_result_t *result,
                                    int h_res,
                                    int v_res,
                                    bool ok,
                                    esp_err_t err)
{
    printf("{\"ok\":%s,\"command\":\"LCDCAM_RAW_CAPTURE\","
           "\"mode\":\"experimental_private_lcdcam_gdma_raw8\","
           "\"error\":\"%s\",\"esp_err\":%d,"
           "\"width\":%d,\"height\":%d,\"color\":\"%s\",\"data_mode\":\"%s\","
           "\"bytes_per_sample\":%d,\"packing\":\"%s\","
           "\"pclk\":\"DCLK\",\"pclk_gpio\":22,\"pclk_invert\":%s,"
           "\"vsync\":\"SPS\",\"vsync_gpio\":33,\"vsync_invert\":%s,"
           "\"de\":\"%s\",\"de_gpio\":%d,\"de_invert\":%s,"
           "\"vh_de_mode\":%s,\"hsync\":\"%s\",\"hsync_gpio\":%d,"
           "\"byte_count_eof\":%s,"
           "\"start_mode\":\"%s\",\"start_trigger_seen\":%s,"
           "\"buffer_len\":%u,\"received_size\":%u,"
           "\"descriptor_count\":%u,\"completed_descriptors\":%u,"
           "\"descriptor_lengths\":[",
           ok ? "true" : "false",
           ok ? "none" : "lcdcam_raw_capture_failed",
           err,
           h_res,
           v_res,
           lcdcam_raw_data_mode_color(result->data_mode),
           lcdcam_raw_data_mode_name(result->data_mode),
           lcdcam_raw_data_mode_bytes_per_sample(result->data_mode),
           lcdcam_raw_data_mode_packing(result->data_mode),
           result->pclk_invert ? "true" : "false",
           result->vsync_invert ? "true" : "false",
           lcdcam_raw_de_source_name(result->de_source),
           lcdcam_raw_de_source_gpio(result->de_source),
           result->de_invert ? "true" : "false",
           result->vh_de_mode ? "true" : "false",
           result->vh_de_mode ? (result->hsync_gpio == 21 ? "LP" : "SPL") : "unused",
           result->hsync_gpio,
           result->byte_count_eof ? "true" : "false",
           lcdcam_raw_start_mode_name(result->start_mode),
           result->start_trigger_seen ? "true" : "false",
           (unsigned)result->buffer_len,
           (unsigned)result->received_size,
           (unsigned)result->descriptor_count,
           (unsigned)result->completed_descriptors);

    size_t report_count = result->descriptor_count < LCDCAM_RAW_MAX_REPORT_DESCRIPTORS
                              ? result->descriptor_count
                              : LCDCAM_RAW_MAX_REPORT_DESCRIPTORS;
    for (size_t i = 0; i < report_count; ++i) {
        printf("%s%lu", i == 0 ? "" : ",", (unsigned long)result->descriptor_lengths[i]);
    }
    printf("],\"descriptor_owners\":[");
    for (size_t i = 0; i < report_count; ++i) {
        printf("%s%u", i == 0 ? "" : ",", (unsigned)result->descriptor_owners[i]);
    }
    printf("],\"descriptor_suc_eof\":[");
    for (size_t i = 0; i < report_count; ++i) {
        printf("%s%u", i == 0 ? "" : ",", (unsigned)result->descriptor_suc_eof[i]);
    }
    printf("],\"descriptor_err_eof\":[");
    for (size_t i = 0; i < report_count; ++i) {
        printf("%s%u", i == 0 ? "" : ",", (unsigned)result->descriptor_err_eof[i]);
    }
    printf("],"
           "\"eof_seen\":%s,\"done_seen\":%s,\"eof_desc_addr\":%ld,"
           "\"failure_stage\":\"%s\",\"failure_err\":%d,"
           "\"checksum\":%lu,\"min_value\":%u,\"max_value\":%u,"
           "\"raw8_transitions\":%lu,\"data_hex\":\"",
           result->eof_seen ? "true" : "false",
           result->done_seen ? "true" : "false",
           (long)result->eof_desc_addr,
           result->failure_stage == NULL ? "unknown" : result->failure_stage,
           result->failure_err,
           (unsigned long)result->checksum,
           (unsigned)result->min_value,
           (unsigned)result->max_value,
           (unsigned long)result->raw8_transitions);

    if (result->buffer != NULL) {
        for (size_t i = 0; i < result->buffer_len; ++i) {
            printf("%02x", (unsigned)result->buffer[i]);
        }
    }
    printf("\"}\n");
}

static void write_stdout_binary_payload(const uint8_t *buffer, size_t len)
{
    while (len > 0) {
        size_t chunk = len > USB_BENCH_CHUNK_LEN ? USB_BENCH_CHUNK_LEN : len;
        size_t written = fwrite(buffer, 1, chunk, stdout);
        buffer += written;
        len -= written;
        fflush(stdout);
        if (written < chunk) {
            clearerr(stdout);
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

static void handle_usb_bench_stream_bin(const char *line)
{
    int frame_count = 0;
    int payload_len = 0;
    int parsed = sscanf(line,
                        "USB_BENCH_STREAM_BIN %d %d",
                        &frame_count,
                        &payload_len);
    if (parsed < 2 ||
        frame_count <= 0 ||
        frame_count > USB_BENCH_MAX_FRAMES ||
        payload_len < 0 ||
        payload_len > (int)USB_BENCH_MAX_PAYLOAD_LEN) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_arguments\","
               "\"usage\":\"USB_BENCH_STREAM_BIN <frame_count_1_to_256> <payload_len_0_to_262144>\","
               "\"binary_len\":0}\n",
               line);
        return;
    }

    uint8_t chunk[USB_BENCH_CHUNK_LEN];
    for (int frame = 0; frame < frame_count; ++frame) {
        uint32_t checksum = 0;
        for (uint32_t i = 0; i < (uint32_t)payload_len; ++i) {
            checksum = (checksum + ((i + (uint32_t)frame) & 0xffU)) & 0xffffffffU;
        }

        int64_t started_us = esp_timer_get_time();
        printf("{\"ok\":true,\"command\":\"USB_BENCH_STREAM_BIN\","
               "\"mode\":\"synthetic_native_usb_serial_jtag_payload\","
               "\"frame_index\":%d,\"binary_len\":%d,"
               "\"payload_pattern\":\"byte=(offset+frame_index)&0xff\","
               "\"checksum\":%lu}\n",
               frame,
               payload_len,
               (unsigned long)checksum);
        fflush(stdout);

        uint32_t remaining = (uint32_t)payload_len;
        uint32_t offset = 0;
        while (remaining > 0) {
            uint32_t chunk_len = remaining > USB_BENCH_CHUNK_LEN ? USB_BENCH_CHUNK_LEN : remaining;
            for (uint32_t i = 0; i < chunk_len; ++i) {
                chunk[i] = (uint8_t)((offset + i + (uint32_t)frame) & 0xffU);
            }
            write_stdout_binary_payload(chunk, chunk_len);
            offset += chunk_len;
            remaining -= chunk_len;
        }
        write_stdout_binary_payload((const uint8_t *)"\n", 1);
        fflush(stdout);

        int64_t elapsed_us = esp_timer_get_time() - started_us;
        (void)elapsed_us;
    }
}

static void handle_pipeline_bench(const char *line)
{
    int frame_count = 0;
    int width = GBC_LCD_SOURCE_VISIBLE_WIDTH;
    int height = GBC_LCD_SOURCE_VISIBLE_HEIGHT;
    int bytes_per_pixel = 2;
    int target_fps = 60;
    int parsed = sscanf(line,
                        "PIPELINE_BENCH %d %d %d %d %d",
                        &frame_count,
                        &width,
                        &height,
                        &bytes_per_pixel,
                        &target_fps);
    if (parsed < 1) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_arguments\","
               "\"usage\":\"PIPELINE_BENCH <frame_count> [width] [height] [bytes_per_pixel_1_or_2] [target_fps_0_unlimited]\"}\n",
               line);
        return;
    }

    pipeline_bench_result_t result;
    esp_err_t err = pipeline_bench_run((uint32_t)frame_count,
                                       (uint32_t)width,
                                       (uint32_t)height,
                                       (uint32_t)bytes_per_pixel,
                                       (uint32_t)target_fps,
                                       &result);
    if (err != ESP_OK) {
        printf("{\"ok\":false,\"command\":\"PIPELINE_BENCH\",\"error\":\"bench_failed\","
               "\"esp_err\":%d,\"usage\":\"PIPELINE_BENCH <frame_count> [width] [height] "
               "[bytes_per_pixel_1_or_2] [target_fps_0_unlimited]\"}\n",
               err);
        return;
    }

    double elapsed_s = (double)result.elapsed_us / 1000000.0;
    double source_fps = elapsed_s > 0.0 ? (double)result.generated_frames / elapsed_s : 0.0;
    double processed_fps = elapsed_s > 0.0 ? (double)result.processed_frames / elapsed_s : 0.0;
    double output_fps = elapsed_s > 0.0 ? (double)result.output_frames / elapsed_s : 0.0;
    double mbps = elapsed_s > 0.0 ?
                  ((double)result.output_frames * (double)result.frame_bytes) / (elapsed_s * 1000000.0) :
                  0.0;
    printf("{\"ok\":true,\"command\":\"PIPELINE_BENCH\","
           "\"mode\":\"synthetic_internal_no_gpio_no_usb_payload\","
           "\"requested_frames\":%lu,\"generated_frames\":%lu,"
           "\"processed_frames\":%lu,\"output_frames\":%lu,\"dropped_frames\":%lu,"
           "\"width\":%lu,\"height\":%lu,\"bytes_per_pixel\":%lu,"
           "\"frame_bytes\":%lu,\"target_fps\":%lu,\"target_met\":%s,"
           "\"elapsed_us\":%lld,\"source_fps\":%.3f,\"processed_fps\":%.3f,"
           "\"output_fps\":%.3f,\"payload_mbytes_per_s\":%.3f,"
           "\"avg_generate_us\":%lld,\"max_generate_us\":%lld,"
           "\"avg_process_us\":%lld,\"max_process_us\":%lld,"
           "\"avg_output_us\":%lld,\"max_output_us\":%lld,"
           "\"max_frame_us\":%lld,\"checksum\":%lu}\n",
           (unsigned long)result.requested_frames,
           (unsigned long)result.generated_frames,
           (unsigned long)result.processed_frames,
           (unsigned long)result.output_frames,
           (unsigned long)result.dropped_frames,
           (unsigned long)result.width,
           (unsigned long)result.height,
           (unsigned long)result.bytes_per_pixel,
           (unsigned long)result.frame_bytes,
           (unsigned long)result.target_fps,
           result.target_met ? "true" : "false",
           (long long)result.elapsed_us,
           source_fps,
           processed_fps,
           output_fps,
           mbps,
           (long long)result.avg_generate_us,
           (long long)result.max_generate_us,
           (long long)result.avg_process_us,
           (long long)result.max_process_us,
           (long long)result.avg_output_us,
           (long long)result.max_output_us,
           (long long)result.max_frame_us,
           (unsigned long)result.checksum);
}

static void handle_gbc_source_bench(const char *line)
{
    int frame_count = 0;
    int timeout_ms = GBC_LCD_SOURCE_DEFAULT_TIMEOUT_MS;
    char data_mode_name[12] = "RGB565";
    int pclk_invert = 0;
    int parsed = sscanf(line,
                        "GBC_SOURCE_BENCH %d %d %11s %d",
                        &frame_count,
                        &timeout_ms,
                        data_mode_name,
                        &pclk_invert);
    if (parsed < 1 || frame_count <= 0 || frame_count > 512 || timeout_ms <= 0 || timeout_ms > 5000) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_arguments\","
               "\"usage\":\"GBC_SOURCE_BENCH <frame_count_1_to_512> [timeout_ms] [RGB565] [pclk_invert_0_or_1]\"}\n",
               line);
        return;
    }

    lcdcam_raw_data_mode_t data_mode = LCDCAM_RAW_DATA_RGB565;
    if ((parsed >= 3 && !parse_lcdcam_raw_data_mode(data_mode_name, &data_mode)) ||
        data_mode != LCDCAM_RAW_DATA_RGB565) {
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_data_mode\","
               "\"allowed\":[\"RGB565\"]}\n",
               line);
        return;
    }

    uint32_t ok_frames = 0;
    uint32_t failed_frames = 0;
    uint32_t total_received = 0;
    uint32_t checksum = 0;
    int64_t total_capture_us = 0;
    int64_t max_capture_us = 0;
    esp_err_t last_err = ESP_OK;
    int64_t started_us = esp_timer_get_time();

    for (int i = 0; i < frame_count; ++i) {
        lcdcam_raw_result_t result = {0};
        int64_t capture_us = 0;
        esp_err_t err = gbc_lcd_source_capture_frame((uint32_t)timeout_ms,
                                                     data_mode,
                                                     pclk_invert != 0,
                                                     &result,
                                                     &capture_us);
        last_err = err;
        if (err == ESP_OK) {
            ok_frames++;
            total_received += (uint32_t)result.received_size;
            checksum ^= result.checksum;
        } else {
            failed_frames++;
        }
        total_capture_us += capture_us;
        if (capture_us > max_capture_us) {
            max_capture_us = capture_us;
        }
        lcdcam_raw_result_free(&result);
    }

    int64_t elapsed_us = esp_timer_get_time() - started_us;
    double elapsed_s = (double)elapsed_us / 1000000.0;
    double capture_fps = elapsed_s > 0.0 ? (double)ok_frames / elapsed_s : 0.0;
    int64_t avg_capture_us = frame_count > 0 ? total_capture_us / frame_count : 0;
    uint32_t expected_emit_bytes = gbc_lcd_source_emit_len(data_mode);
    uint32_t expected_capture_bytes = GBC_LCD_SOURCE_CAPTURE_WIDTH *
                                      GBC_LCD_SOURCE_CAPTURE_HEIGHT *
                                      (uint32_t)lcdcam_raw_data_mode_bytes_per_sample(data_mode);

    printf("{\"ok\":%s,\"command\":\"GBC_SOURCE_BENCH\","
           "\"mode\":\"real_source_capture_stats_no_frame_payload\","
           "\"requested_frames\":%d,\"captured_frames\":%lu,\"failed_frames\":%lu,"
           "\"data_mode\":\"%s\",\"expected_capture_bytes\":%lu,"
           "\"expected_emit_bytes\":%lu,"
           "\"total_received_bytes\":%lu,\"timeout_ms\":%d,\"pclk_invert\":%s,"
           "\"elapsed_us\":%lld,\"capture_fps\":%.3f,"
           "\"avg_capture_us\":%lld,\"max_capture_us\":%lld,"
           "\"last_esp_err\":%d,\"checksum\":%lu}\n",
           failed_frames == 0 ? "true" : "false",
           frame_count,
           (unsigned long)ok_frames,
           (unsigned long)failed_frames,
           lcdcam_raw_data_mode_name(data_mode),
           (unsigned long)expected_capture_bytes,
           (unsigned long)expected_emit_bytes,
           (unsigned long)total_received,
           timeout_ms,
           pclk_invert != 0 ? "true" : "false",
           (long long)elapsed_us,
           capture_fps,
           (long long)avg_capture_us,
           (long long)max_capture_us,
           last_err,
           (unsigned long)checksum);
}

static void handle_gbc_pipeline_bench(const char *line)
{
    bool persistent = command_has_prefix(line, "GBC_PIPELINE_BENCH_PERSIST");
    int frame_count = 0;
    int timeout_ms = GBC_LCD_SOURCE_DEFAULT_TIMEOUT_MS;
    char data_mode_name[12] = "RGB565";
    int pclk_invert = 0;
    int target_fps = 60;
    int parsed = persistent ?
                 sscanf(line,
                        "GBC_PIPELINE_BENCH_PERSIST %d %d %11s %d %d",
                        &frame_count,
                        &timeout_ms,
                        data_mode_name,
                        &pclk_invert,
                        &target_fps) :
                 sscanf(line,
                        "GBC_PIPELINE_BENCH %d %d %11s %d %d",
                        &frame_count,
                        &timeout_ms,
                        data_mode_name,
                        &pclk_invert,
                        &target_fps);
    if (parsed < 1 ||
        frame_count <= 0 ||
        frame_count > 512 ||
        timeout_ms <= 0 ||
        timeout_ms > 5000 ||
        target_fps < 0 ||
        target_fps > 1000) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_arguments\","
               "\"usage\":\"GBC_PIPELINE_BENCH[_PERSIST] <frame_count_1_to_512> [timeout_ms] [RGB565] [pclk_invert_0_or_1] [target_fps_0_to_1000]\"}\n",
               line);
        return;
    }

    lcdcam_raw_data_mode_t data_mode = LCDCAM_RAW_DATA_RGB565;
    if ((parsed >= 3 && !parse_lcdcam_raw_data_mode(data_mode_name, &data_mode)) ||
        data_mode != LCDCAM_RAW_DATA_RGB565) {
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_data_mode\","
               "\"allowed\":[\"RGB565\"]}\n",
               line);
        return;
    }

    source_pipeline_bench_result_t result = {0};
    esp_err_t err = persistent ?
                    source_pipeline_bench_run_persistent((uint32_t)frame_count,
                                                         (uint32_t)timeout_ms,
                                                         data_mode,
                                                         pclk_invert != 0,
                                                         (uint32_t)target_fps,
                                                         &result) :
                    source_pipeline_bench_run((uint32_t)frame_count,
                                              (uint32_t)timeout_ms,
                                              data_mode,
                                              pclk_invert != 0,
                                              (uint32_t)target_fps,
                                              &result);
    double elapsed_s = (double)result.elapsed_us / 1000000.0;
    double capture_fps = elapsed_s > 0.0 ? (double)result.captured_frames / elapsed_s : 0.0;
    double processed_fps = elapsed_s > 0.0 ? (double)result.processed_frames / elapsed_s : 0.0;
    double output_fps = elapsed_s > 0.0 ? (double)result.output_frames / elapsed_s : 0.0;
    double capture_mbytes_s = elapsed_s > 0.0 ?
                              ((double)result.captured_frames * (double)result.capture_frame_bytes) /
                                  (elapsed_s * 1000000.0) :
                              0.0;
    double emit_mbytes_s = elapsed_s > 0.0 ?
                           ((double)result.output_frames * (double)result.emit_frame_bytes) /
                               (elapsed_s * 1000000.0) :
                           0.0;

    printf("{\"ok\":%s,\"command\":\"%s\","
           "\"mode\":\"compat_real_source_pipeline_no_usb_payload\","
           "\"performance_path\":\"%s\","
           "\"next_performance_path\":\"%s\","
           "\"requested_frames\":%lu,\"captured_frames\":%lu,"
           "\"processed_frames\":%lu,\"output_frames\":%lu,"
           "\"failed_frames\":%lu,\"budget_miss_frames\":%lu,"
           "\"capture_width\":%lu,\"capture_height\":%lu,"
           "\"stream_width\":%lu,\"stream_height\":%lu,"
           "\"bytes_per_sample\":%lu,\"capture_frame_bytes\":%lu,"
           "\"emit_frame_bytes\":%lu,\"data_mode\":\"%s\","
           "\"timeout_ms\":%d,\"pclk_invert\":%s,"
           "\"target_fps\":%d,\"target_met\":%s,"
           "\"elapsed_us\":%lld,\"capture_fps\":%.3f,"
           "\"processed_fps\":%.3f,\"output_fps\":%.3f,"
           "\"capture_mbytes_per_s\":%.3f,\"emit_mbytes_per_s\":%.3f,"
           "\"avg_capture_us\":%lld,\"max_capture_us\":%lld,"
           "\"avg_process_us\":%lld,\"max_process_us\":%lld,"
           "\"avg_output_us\":%lld,\"max_output_us\":%lld,"
           "\"max_frame_us\":%lld,\"last_esp_err\":%d,"
           "\"run_esp_err\":%d,\"checksum\":%lu}\n",
           err == ESP_OK ? "true" : "false",
           persistent ? "GBC_PIPELINE_BENCH_PERSIST" : "GBC_PIPELINE_BENCH",
           persistent ? "persistent_lcdcam_gdma_reused_setup" : "per_frame_lcdcam_setup_teardown",
           persistent ? "continuous_ring_descriptor_stream" : "persistent_lcdcam_gdma_ring",
           (unsigned long)result.requested_frames,
           (unsigned long)result.captured_frames,
           (unsigned long)result.processed_frames,
           (unsigned long)result.output_frames,
           (unsigned long)result.failed_frames,
           (unsigned long)result.budget_miss_frames,
           (unsigned long)result.capture_width,
           (unsigned long)result.capture_height,
           (unsigned long)result.stream_width,
           (unsigned long)result.stream_height,
           (unsigned long)result.bytes_per_sample,
           (unsigned long)result.capture_frame_bytes,
           (unsigned long)result.emit_frame_bytes,
           lcdcam_raw_data_mode_name(result.data_mode),
           timeout_ms,
           result.pclk_invert ? "true" : "false",
           target_fps,
           result.target_met ? "true" : "false",
           (long long)result.elapsed_us,
           capture_fps,
           processed_fps,
           output_fps,
           capture_mbytes_s,
           emit_mbytes_s,
           (long long)result.avg_capture_us,
           (long long)result.max_capture_us,
           (long long)result.avg_process_us,
           (long long)result.max_process_us,
           (long long)result.avg_output_us,
           (long long)result.max_output_us,
           (long long)result.max_frame_us,
           result.last_esp_err,
           err,
           (unsigned long)result.checksum);
}

static void handle_gbc_rearm_bench(const char *line)
{
    bool frame_sync = command_has_prefix(line, "GBC_FRAME_REARM_BENCH");
    int chunk_count = 0;
    int timeout_ms = GBC_LCD_SOURCE_DEFAULT_TIMEOUT_MS;
    char data_mode_name[12] = "RGB565";
    int pclk_invert = 0;
    int parsed = frame_sync ?
                 sscanf(line,
                        "GBC_FRAME_REARM_BENCH %d %d %11s %d",
                        &chunk_count,
                        &timeout_ms,
                        data_mode_name,
                        &pclk_invert) :
                 sscanf(line,
                        "GBC_REARM_BENCH %d %d %11s %d",
                        &chunk_count,
                        &timeout_ms,
                        data_mode_name,
                        &pclk_invert);
    if (parsed < 1 || chunk_count <= 0 || chunk_count > 512 || timeout_ms <= 0 || timeout_ms > 5000) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_arguments\","
               "\"usage\":\"GBC_REARM_BENCH|GBC_FRAME_REARM_BENCH <chunk_count_1_to_512> [timeout_ms] [RGB565] [pclk_invert_0_or_1]\"}\n",
               line);
        return;
    }

    lcdcam_raw_data_mode_t data_mode = LCDCAM_RAW_DATA_RGB565;
    if ((parsed >= 3 && !parse_lcdcam_raw_data_mode(data_mode_name, &data_mode)) ||
        data_mode != LCDCAM_RAW_DATA_RGB565) {
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_data_mode\","
               "\"allowed\":[\"RGB565\"]}\n",
               line);
        return;
    }

    lcdcam_raw_rearm_stats_t stats = {0};
    esp_err_t err = lcdcam_raw_rearm_bench(LCDCAM_RAW_DE_HIGH,
                                           GBC_LCD_SOURCE_CAPTURE_WIDTH,
                                           GBC_LCD_SOURCE_CAPTURE_HEIGHT,
                                           (uint32_t)timeout_ms,
                                           false,
                                           false,
                                           pclk_invert != 0,
                                           true,
                                           frame_sync ? LCDCAM_RAW_START_AFTER_SPS_THEN_SPL_FALLING :
                                                        LCDCAM_RAW_START_AFTER_SPS_RISING,
                                           false,
                                           data_mode,
                                           (uint32_t)chunk_count,
                                           &stats);
    double elapsed_s = (double)stats.elapsed_us / 1000000.0;
    double chunk_fps = elapsed_s > 0.0 ? (double)stats.completed_chunks / elapsed_s : 0.0;
    double payload_mbytes_s = elapsed_s > 0.0 ?
                              ((double)stats.completed_chunks * (double)stats.chunk_bytes) /
                                  (elapsed_s * 1000000.0) :
                              0.0;
    double expected_frame_fps = 59.73;
    double expected_frame_us = 1000000.0 / expected_frame_fps;
    double avg_vs_expected_pct = expected_frame_us > 0.0 ?
                                 ((double)stats.avg_chunk_us / expected_frame_us) * 100.0 :
                                 0.0;
    printf("{\"ok\":%s,\"command\":\"%s\","
           "\"mode\":\"%s\","
           "\"performance_path\":\"isr_rearm_double_buffer_byte_count_eof\","
           "\"next_performance_path\":\"continuous_ring_descriptor_stream\","
           "\"requested_chunks\":%lu,\"completed_chunks\":%lu,"
           "\"failed_rearms\":%lu,\"data_mode\":\"%s\","
           "\"h_res\":%lu,\"v_res\":%lu,\"bytes_per_sample\":%lu,"
           "\"chunk_bytes\":%lu,\"timeout_ms\":%lu,\"pclk_invert\":%s,"
           "\"start_trigger\":\"%s\",\"start_trigger_seen\":%s,"
           "\"expected_frame_fps\":%.3f,\"expected_frame_us\":%.1f,"
           "\"avg_chunk_vs_expected_pct\":%.1f,"
           "\"elapsed_us\":%lld,\"chunk_fps\":%.3f,"
           "\"payload_mbytes_per_s\":%.3f,"
           "\"first_chunk_us\":%lld,\"avg_chunk_us\":%lld,"
           "\"max_chunk_us\":%lld,\"failure_stage\":\"%s\","
           "\"last_esp_err\":%d,\"run_esp_err\":%d,\"checksum\":%lu}\n",
           err == ESP_OK ? "true" : "false",
           frame_sync ? "GBC_FRAME_REARM_BENCH" : "GBC_REARM_BENCH",
           frame_sync ? "frame_phase_rearm_sps_then_spl_no_usb_payload" :
                        "raw_chunk_rearm_no_frame_alignment_no_usb_payload",
           (unsigned long)stats.requested_chunks,
           (unsigned long)stats.completed_chunks,
           (unsigned long)stats.failed_rearms,
           lcdcam_raw_data_mode_name(stats.data_mode),
           (unsigned long)stats.h_res,
           (unsigned long)stats.v_res,
           (unsigned long)stats.bytes_per_sample,
           (unsigned long)stats.chunk_bytes,
           (unsigned long)stats.timeout_ms,
           pclk_invert != 0 ? "true" : "false",
           frame_sync ? "SPS_RISING_THEN_SPL_FALLING" : "SPS_RISING",
           stats.start_trigger_seen ? "true" : "false",
           expected_frame_fps,
           expected_frame_us,
           avg_vs_expected_pct,
           (long long)stats.elapsed_us,
           chunk_fps,
           payload_mbytes_s,
           (long long)stats.first_chunk_us,
           (long long)stats.avg_chunk_us,
           (long long)stats.max_chunk_us,
           stats.failure_stage == NULL ? "unknown" : stats.failure_stage,
           stats.last_esp_err,
           err,
           (unsigned long)stats.checksum);
}

static void write_u32_le(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xffU);
    dst[1] = (uint8_t)((value >> 8) & 0xffU);
    dst[2] = (uint8_t)((value >> 16) & 0xffU);
    dst[3] = (uint8_t)((value >> 24) & 0xffU);
}

static void fwrite_chunked(const uint8_t *data, uint32_t len)
{
    uint32_t offset = 0;
    while (offset < len) {
        uint32_t chunk = MIN(len - offset, USB_BENCH_CHUNK_LEN);
        fwrite(data + offset, 1, chunk, stdout);
        offset += chunk;
    }
}

static void handle_gbc_capcard_stream_bin(const char *line)
{
    int frame_count = 0;
    int timeout_ms = GBC_LCD_SOURCE_DEFAULT_TIMEOUT_MS;
    int emit_len = (int)gbc_lcd_source_emit_len(LCDCAM_RAW_DATA_RGB565);
    int pclk_invert = 0;
    int parsed = sscanf(line,
                        "GBC_CAPCARD_STREAM_BIN %d %d %d %d",
                        &frame_count,
                        &timeout_ms,
                        &emit_len,
                        &pclk_invert);
    uint32_t max_emit_len = gbc_lcd_source_emit_len(LCDCAM_RAW_DATA_RGB565);
    if (parsed < 1 ||
        frame_count <= 0 ||
        frame_count > 64 ||
        timeout_ms <= 0 ||
        timeout_ms > 5000 ||
        emit_len < 0 ||
        (uint32_t)emit_len > max_emit_len) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_arguments\","
               "\"usage\":\"GBC_CAPCARD_STREAM_BIN <frame_count_1_to_64> [timeout_ms] [emit_len_0_to_46690] [pclk_invert_0_or_1]\","
               "\"binary_len\":0}\n",
               line);
        return;
    }

    uint32_t payload_len = (uint32_t)emit_len;
    uint32_t frame_record_len = GBC_CAPCARD_FRAME_HEADER_LEN + payload_len;
    uint32_t binary_len = (uint32_t)frame_count * frame_record_len;
    printf("{\"ok\":true,\"command\":\"GBC_CAPCARD_STREAM_BIN\","
           "\"mode\":\"capture_card_blob_compat_source_rgb565\","
           "\"frame_count\":%d,\"frame_header_len\":%lu,"
           "\"payload_len\":%lu,\"frame_record_len\":%lu,"
           "\"binary_len\":%lu,\"data_mode\":\"RGB565\","
           "\"timeout_ms\":%d,\"pclk_invert\":%s}\n",
           frame_count,
           (unsigned long)GBC_CAPCARD_FRAME_HEADER_LEN,
           (unsigned long)payload_len,
           (unsigned long)frame_record_len,
           (unsigned long)binary_len,
           timeout_ms,
           pclk_invert != 0 ? "true" : "false");
    fflush(stdout);

    uint8_t header[GBC_CAPCARD_FRAME_HEADER_LEN];
    for (int i = 0; i < frame_count; ++i) {
        lcdcam_raw_result_t result = {0};
        int64_t capture_us = 0;
        esp_err_t err = gbc_lcd_source_capture_frame((uint32_t)timeout_ms,
                                                     LCDCAM_RAW_DATA_RGB565,
                                                     pclk_invert != 0,
                                                     &result,
                                                     &capture_us);
        header[0] = 'G';
        header[1] = 'B';
        header[2] = 'C';
        header[3] = 'F';
        write_u32_le(&header[4], (uint32_t)i);
        write_u32_le(&header[8], err == ESP_OK ? 1U : 0U);
        write_u32_le(&header[12], (uint32_t)capture_us);
        write_u32_le(&header[16], result.checksum);
        fwrite(header, 1, sizeof(header), stdout);
        if (err == ESP_OK && result.buffer != NULL) {
            fwrite_chunked(result.buffer, payload_len);
        } else {
            static const uint8_t zeros[USB_BENCH_CHUNK_LEN] = {0};
            uint32_t remaining = payload_len;
            while (remaining > 0) {
                uint32_t chunk = MIN(remaining, (uint32_t)sizeof(zeros));
                fwrite(zeros, 1, chunk, stdout);
                remaining -= chunk;
            }
        }
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(1));
        lcdcam_raw_result_free(&result);
    }
    fflush(stdout);
}

static void print_lcdcam_raw_binary_result(const char *command_name,
                                           const char *mode_name,
                                           lcdcam_raw_result_t *result,
                                           int h_res,
                                           int v_res,
                                           bool ok,
                                           esp_err_t err,
                                           uint32_t emit_len,
                                           int64_t capture_us)
{
    size_t binary_len = ok && result->buffer != NULL ? result->buffer_len : 0;
    if (emit_len > 0 && emit_len < binary_len) {
        binary_len = emit_len;
    }
    printf("{\"ok\":%s,\"command\":\"%s\","
           "\"mode\":\"%s\","
           "\"error\":\"%s\",\"esp_err\":%d,"
           "\"width\":%d,\"height\":%d,\"color\":\"%s\",\"data_mode\":\"%s\","
           "\"bytes_per_sample\":%d,\"packing\":\"%s\","
           "\"pclk\":\"DCLK\",\"pclk_gpio\":22,\"pclk_invert\":%s,"
           "\"vsync\":\"SPS\",\"vsync_gpio\":33,\"vsync_invert\":%s,"
           "\"de\":\"%s\",\"de_gpio\":%d,\"de_invert\":%s,"
           "\"vh_de_mode\":%s,\"hsync\":\"%s\",\"hsync_gpio\":%d,"
           "\"byte_count_eof\":%s,"
           "\"start_mode\":\"%s\",\"start_trigger_seen\":%s,"
           "\"buffer_len\":%u,\"binary_len\":%u,\"received_size\":%u,"
           "\"capture_us\":%lld,"
           "\"descriptor_count\":%u,\"completed_descriptors\":%u,"
           "\"eof_seen\":%s,\"done_seen\":%s,"
           "\"failure_stage\":\"%s\",\"failure_err\":%d,"
           "\"checksum\":%lu,\"min_value\":%u,\"max_value\":%u,"
           "\"raw8_transitions\":%lu}\n",
           ok ? "true" : "false",
           command_name,
           mode_name,
           ok ? "none" : "lcdcam_raw_capture_failed",
           err,
           h_res,
           v_res,
           lcdcam_raw_data_mode_color(result->data_mode),
           lcdcam_raw_data_mode_name(result->data_mode),
           lcdcam_raw_data_mode_bytes_per_sample(result->data_mode),
           lcdcam_raw_data_mode_packing(result->data_mode),
           result->pclk_invert ? "true" : "false",
           result->vsync_invert ? "true" : "false",
           lcdcam_raw_de_source_name(result->de_source),
           lcdcam_raw_de_source_gpio(result->de_source),
           result->de_invert ? "true" : "false",
           result->vh_de_mode ? "true" : "false",
           result->vh_de_mode ? (result->hsync_gpio == 21 ? "LP" : "SPL") : "unused",
           result->hsync_gpio,
           result->byte_count_eof ? "true" : "false",
           lcdcam_raw_start_mode_name(result->start_mode),
           result->start_trigger_seen ? "true" : "false",
           (unsigned)result->buffer_len,
           (unsigned)binary_len,
           (unsigned)result->received_size,
           (long long)capture_us,
           (unsigned)result->descriptor_count,
           (unsigned)result->completed_descriptors,
           result->eof_seen ? "true" : "false",
           result->done_seen ? "true" : "false",
           result->failure_stage == NULL ? "unknown" : result->failure_stage,
           result->failure_err,
           (unsigned long)result->checksum,
           (unsigned)result->min_value,
           (unsigned)result->max_value,
           (unsigned long)result->raw8_transitions);
    fflush(stdout);
    if (binary_len > 0) {
        write_stdout_binary_payload(result->buffer, binary_len);
        /* Force a trailing USB serial short packet without changing binary_len. */
        write_stdout_binary_payload((const uint8_t *)"\n", 1);
        fflush(stdout);
    }
}

static void handle_lcdcam_raw_capture(const char *line)
{
    char de_name[8] = {0};
    int timeout_ms = 0;
    int vsync_invert = 0;
    int de_invert = 0;
    int pclk_invert = 0;
    int byte_count_eof = 0;
    int h_res = 160;
    int v_res = 144;
    int start_mode_arg = 0;
    int vh_de_mode = 0;
    char data_mode_name[12] = "RG44";
    int parsed = sscanf(line,
                        "LCDCAM_RAW_CAPTURE %7s %d %d %d %d %d %d %d %d %d %11s",
                        de_name,
                        &timeout_ms,
                        &vsync_invert,
                        &de_invert,
                        &pclk_invert,
                        &byte_count_eof,
                        &h_res,
                        &v_res,
                        &start_mode_arg,
                        &vh_de_mode,
                        data_mode_name);
    if (parsed < 2 ||
        timeout_ms <= 0 || timeout_ms > 5000 ||
        h_res <= 0 || h_res > 320 ||
        v_res <= 0 || v_res > 240) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_arguments\","
               "\"usage\":\"LCDCAM_RAW_CAPTURE <SPL|LP|HIGH> <timeout_ms_1_to_5000> [vsync_invert_0_or_1] [de_invert_0_or_1] [pclk_invert_0_or_1] [byte_count_eof_0_or_1] [h_res_1_to_320] [v_res_1_to_240] [start_mode_0_immediate_1_after_sps_2_after_sps_spl] [vh_de_mode_0_or_1] [RG44|RGB332|RGB664|RGB565]\"}\n",
               line);
        return;
    }
    if ((vsync_invert != 0 && vsync_invert != 1) ||
        (de_invert != 0 && de_invert != 1) ||
        (pclk_invert != 0 && pclk_invert != 1) ||
        (byte_count_eof != 0 && byte_count_eof != 1) ||
        (vh_de_mode != 0 && vh_de_mode != 1) ||
        start_mode_arg < 0 || start_mode_arg > 2) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_boolean_argument\"}\n", line);
        return;
    }

    lcdcam_raw_de_source_t de_source = LCDCAM_RAW_DE_SPL;
    if (!parse_lcdcam_raw_de_source(de_name, &de_source)) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_de_source\","
               "\"allowed\":[\"SPL\",\"LP\",\"HIGH\"]}\n",
               line);
        return;
    }

    lcdcam_raw_data_mode_t data_mode = LCDCAM_RAW_DATA_RG44;
    if (!parse_lcdcam_raw_data_mode(data_mode_name, &data_mode)) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_data_mode\","
               "\"allowed\":[\"RG44\",\"RGB332\",\"RGB664\",\"RGB565\"]}\n",
               line);
        return;
    }

    lcdcam_raw_result_t result = {0};
    esp_err_t err = lcdcam_raw_capture(de_source,
                                       (uint32_t)h_res,
                                       (uint32_t)v_res,
                                       (uint32_t)timeout_ms,
                                       vsync_invert != 0,
                                       de_invert != 0,
                                       pclk_invert != 0,
                                       byte_count_eof != 0,
                                       (lcdcam_raw_start_mode_t)start_mode_arg,
                                       vh_de_mode != 0,
                                       data_mode,
                                       &result);
    if (err != ESP_OK) {
        diagnostics_record_unsupported_command();
        print_lcdcam_raw_result(&result, h_res, v_res, false, err);
        lcdcam_raw_result_free(&result);
        return;
    }

    print_lcdcam_raw_result(&result, h_res, v_res, true, ESP_OK);
    lcdcam_raw_result_free(&result);
}

static void handle_lcdcam_raw_capture_bin(const char *line)
{
    char de_name[8] = {0};
    int timeout_ms = 0;
    int vsync_invert = 0;
    int de_invert = 0;
    int pclk_invert = 0;
    int byte_count_eof = 0;
    int h_res = 160;
    int v_res = 144;
    int start_mode_arg = 0;
    int vh_de_mode = 0;
    int emit_len = 0;
    char data_mode_name[12] = "RG44";
    int parsed = sscanf(line,
                        "LCDCAM_RAW_CAPTURE_BIN %7s %d %d %d %d %d %d %d %d %d %11s %d",
                        de_name,
                        &timeout_ms,
                        &vsync_invert,
                        &de_invert,
                        &pclk_invert,
                        &byte_count_eof,
                        &h_res,
                        &v_res,
                        &start_mode_arg,
                        &vh_de_mode,
                        data_mode_name,
                        &emit_len);
    if (parsed < 2 ||
        timeout_ms <= 0 || timeout_ms > 5000 ||
        h_res <= 0 || h_res > 320 ||
        v_res <= 0 || v_res > 240 ||
        emit_len < 0 || emit_len > (h_res * v_res * 2)) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_arguments\","
               "\"usage\":\"LCDCAM_RAW_CAPTURE_BIN <SPL|LP|HIGH> <timeout_ms_1_to_5000> [vsync_invert_0_or_1] [de_invert_0_or_1] [pclk_invert_0_or_1] [byte_count_eof_0_or_1] [h_res_1_to_320] [v_res_1_to_240] [start_mode_0_immediate_1_after_sps_2_after_sps_spl] [vh_de_mode_0_or_1] [RG44|RGB332|RGB664|RGB565]\"}\n",
               line);
        return;
    }
    if ((vsync_invert != 0 && vsync_invert != 1) ||
        (de_invert != 0 && de_invert != 1) ||
        (pclk_invert != 0 && pclk_invert != 1) ||
        (byte_count_eof != 0 && byte_count_eof != 1) ||
        (vh_de_mode != 0 && vh_de_mode != 1) ||
        start_mode_arg < 0 || start_mode_arg > 2) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_boolean_argument\",\"binary_len\":0}\n", line);
        return;
    }

    lcdcam_raw_de_source_t de_source = LCDCAM_RAW_DE_SPL;
    if (!parse_lcdcam_raw_de_source(de_name, &de_source)) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_de_source\","
               "\"allowed\":[\"SPL\",\"LP\",\"HIGH\"],\"binary_len\":0}\n",
               line);
        return;
    }

    lcdcam_raw_data_mode_t data_mode = LCDCAM_RAW_DATA_RG44;
    if (!parse_lcdcam_raw_data_mode(data_mode_name, &data_mode)) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_data_mode\","
               "\"allowed\":[\"RG44\",\"RGB332\",\"RGB664\",\"RGB565\"],\"binary_len\":0}\n",
               line);
        return;
    }

    lcdcam_raw_result_t result = {0};
    int64_t capture_start_us = esp_timer_get_time();
    esp_err_t err = lcdcam_raw_capture(de_source,
                                       (uint32_t)h_res,
                                       (uint32_t)v_res,
                                       (uint32_t)timeout_ms,
                                       vsync_invert != 0,
                                       de_invert != 0,
                                       pclk_invert != 0,
                                       byte_count_eof != 0,
                                       (lcdcam_raw_start_mode_t)start_mode_arg,
                                       vh_de_mode != 0,
                                       data_mode,
                                       &result);
    int64_t capture_us = esp_timer_get_time() - capture_start_us;
    print_lcdcam_raw_binary_result("LCDCAM_RAW_CAPTURE_BIN",
                                   "experimental_private_lcdcam_gdma_raw8_binary",
                                   &result,
                                   h_res,
                                   v_res,
                                   err == ESP_OK,
                                   err,
                                   (uint32_t)emit_len,
                                   capture_us);
    lcdcam_raw_result_free(&result);
}

static void handle_lcdcam_raw_capture_src_bin(void)
{
    lcdcam_raw_result_t result = {0};
    int64_t capture_us = 0;
    esp_err_t err = gbc_lcd_source_capture_frame(GBC_LCD_SOURCE_DEFAULT_TIMEOUT_MS,
                                                 LCDCAM_RAW_DATA_RGB565,
                                                 false,
                                                 &result,
                                                 &capture_us);
    print_lcdcam_raw_binary_result("LCDCAM_RAW_CAPTURE_SRC_BIN",
                                   "compat_gbc_source_rgb565",
                                   &result,
                                   GBC_LCD_SOURCE_CAPTURE_WIDTH,
                                   GBC_LCD_SOURCE_CAPTURE_HEIGHT,
                                   err == ESP_OK,
                                   err,
                                   gbc_lcd_source_emit_len(LCDCAM_RAW_DATA_RGB565),
                                   capture_us);
    lcdcam_raw_result_free(&result);
}

static void handle_lcdcam_raw_stream_bin(const char *line)
{
    int frame_count = 0;
    int pclk_invert = 0;
    int emit_len = 0;
    char data_mode_name[12] = "RGB332";
    int parsed = sscanf(line,
                        "LCDCAM_RAW_STREAM_BIN %d %d %11s %d",
                        &frame_count,
                        &pclk_invert,
                        data_mode_name,
                        &emit_len);
    if (parsed < 1 ||
        frame_count <= 0 ||
        frame_count > 64 ||
        (pclk_invert != 0 && pclk_invert != 1) ||
        emit_len < 0 ||
        emit_len > (192 * 145 * 2)) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_arguments\","
               "\"usage\":\"LCDCAM_RAW_STREAM_BIN <frame_count_1_to_64> [pclk_invert_0_or_1] [RG44|RGB332|RGB664|RGB565] [emit_len_0_to_frame_bytes]\",\"binary_len\":0}\n",
               line);
        return;
    }

    lcdcam_raw_data_mode_t data_mode = LCDCAM_RAW_DATA_RGB332;
    if (!parse_lcdcam_raw_data_mode(data_mode_name, &data_mode)) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_data_mode\","
               "\"allowed\":[\"RG44\",\"RGB332\",\"RGB664\",\"RGB565\"],\"binary_len\":0}\n",
               line);
        return;
    }

    for (int frame = 0; frame < frame_count; ++frame) {
        lcdcam_raw_result_t result = {0};
        int64_t capture_start_us = esp_timer_get_time();
        esp_err_t err = lcdcam_raw_capture(LCDCAM_RAW_DE_HIGH,
                                           192,
                                           145,
                                           2500,
                                           false,
                                           false,
                                           pclk_invert != 0,
                                           true,
                                           LCDCAM_RAW_START_AFTER_SPS_RISING,
                                           false,
                                           data_mode,
                                           &result);
        int64_t capture_us = esp_timer_get_time() - capture_start_us;
        print_lcdcam_raw_binary_result("LCDCAM_RAW_STREAM_BIN",
                                       "diagnostic_lcdcam_stream_frame",
                                       &result,
                                       192,
                                       145,
                                       err == ESP_OK,
                                       err,
                                       (uint32_t)emit_len,
                                       capture_us);
        lcdcam_raw_result_free(&result);
        if (err != ESP_OK) {
            break;
        }
    }
}

static void handle_gbc_source_status(void)
{
    gbc_lcd_source_status_t status = {0};
    gbc_lcd_source_get_status(&status);
    printf("{\"ok\":true,\"command\":\"GBC_SOURCE_STATUS\","
           "\"source\":\"gbc_lcd\",\"graduation_status\":\"source-driver\","
           "\"driver\":\"gbc_lcd_source\","
           "\"performance_path\":\"compat_lcdcam_per_frame\","
           "\"next_performance_path\":\"persistent_lcdcam_gdma_stream\","
           "\"capture_width\":%lu,\"capture_height\":%lu,"
           "\"stream_width\":%lu,\"stream_height\":%lu,"
           "\"visible_width\":%lu,\"visible_height\":%lu,"
           "\"default_timeout_ms\":%lu,"
           "\"default_data_mode\":\"%s\","
           "\"default_emit_len\":%lu,"
           "\"emit_len_rgb565\":%lu,"
           "\"pclk_invert\":%s,"
           "\"transport_data_plane\":\"native_usb_serial_jtag\","
           "\"recovery_plane\":\"wch_uart_rom\"}\n",
           (unsigned long)status.capture_width,
           (unsigned long)status.capture_height,
           (unsigned long)status.stream_width,
           (unsigned long)status.stream_height,
           (unsigned long)status.visible_width,
           (unsigned long)status.visible_height,
           (unsigned long)status.default_timeout_ms,
           lcdcam_raw_data_mode_name(status.default_data_mode),
           (unsigned long)gbc_lcd_source_emit_len(status.default_data_mode),
           (unsigned long)status.emit_len_rgb565,
           status.pclk_invert ? "true" : "false");
}

static void handle_gbc_source_frame_bin(const char *line)
{
    int timeout_ms = GBC_LCD_SOURCE_DEFAULT_TIMEOUT_MS;
    char data_mode_name[12] = "RGB565";
    int emit_len = 0;
    int pclk_invert = 0;
    int parsed = sscanf(line,
                        "GBC_SOURCE_FRAME_BIN %d %11s %d %d",
                        &timeout_ms,
                        data_mode_name,
                        &emit_len,
                        &pclk_invert);
    if (parsed == EOF) {
        parsed = 0;
    }
    if (parsed < 0 ||
        timeout_ms <= 0 ||
        timeout_ms > 5000 ||
        emit_len < 0 ||
        emit_len > (int)(GBC_LCD_SOURCE_CAPTURE_WIDTH * GBC_LCD_SOURCE_CAPTURE_HEIGHT * 2U) ||
        (pclk_invert != 0 && pclk_invert != 1)) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_arguments\","
               "\"usage\":\"GBC_SOURCE_FRAME_BIN [timeout_ms_1_to_5000] [RGB565] [emit_len_0_to_frame_bytes] [pclk_invert_0_or_1]\","
               "\"binary_len\":0}\n",
               line);
        return;
    }

    lcdcam_raw_data_mode_t data_mode = LCDCAM_RAW_DATA_RGB565;
    if (!parse_lcdcam_raw_data_mode(data_mode_name, &data_mode) ||
        data_mode != LCDCAM_RAW_DATA_RGB565) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_data_mode\","
               "\"allowed\":[\"RGB565\"],\"binary_len\":0}\n",
               line);
        return;
    }

    if (emit_len == 0) {
        emit_len = (int)gbc_lcd_source_emit_len(data_mode);
    }

    lcdcam_raw_result_t result = {0};
    int64_t capture_us = 0;
    esp_err_t err = gbc_lcd_source_capture_frame((uint32_t)timeout_ms,
                                                 data_mode,
                                                 pclk_invert != 0,
                                                 &result,
                                                 &capture_us);
    print_lcdcam_raw_binary_result("GBC_SOURCE_FRAME_BIN",
                                   "gbc_lcd_source_driver_compat_frame",
                                   &result,
                                   GBC_LCD_SOURCE_CAPTURE_WIDTH,
                                   GBC_LCD_SOURCE_CAPTURE_HEIGHT,
                                   err == ESP_OK,
                                   err,
                                   (uint32_t)emit_len,
                                   capture_us);
    lcdcam_raw_result_free(&result);
}

static void handle_gbc_source_stream_bin(const char *line)
{
    int frame_count = 0;
    int timeout_ms = GBC_LCD_SOURCE_DEFAULT_TIMEOUT_MS;
    char data_mode_name[12] = "RGB565";
    int emit_len = 0;
    int pclk_invert = 0;
    int parsed = sscanf(line,
                        "GBC_SOURCE_STREAM_BIN %d %d %11s %d %d",
                        &frame_count,
                        &timeout_ms,
                        data_mode_name,
                        &emit_len,
                        &pclk_invert);
    if (parsed < 1 ||
        frame_count <= 0 ||
        frame_count > 64 ||
        timeout_ms <= 0 ||
        timeout_ms > 5000 ||
        emit_len < 0 ||
        emit_len > (int)(GBC_LCD_SOURCE_CAPTURE_WIDTH * GBC_LCD_SOURCE_CAPTURE_HEIGHT * 2U) ||
        (pclk_invert != 0 && pclk_invert != 1)) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_arguments\","
               "\"usage\":\"GBC_SOURCE_STREAM_BIN <frame_count_1_to_64> [timeout_ms_1_to_5000] [RGB565] [emit_len_0_to_frame_bytes] [pclk_invert_0_or_1]\","
               "\"binary_len\":0}\n",
               line);
        return;
    }

    lcdcam_raw_data_mode_t data_mode = LCDCAM_RAW_DATA_RGB565;
    if (!parse_lcdcam_raw_data_mode(data_mode_name, &data_mode) ||
        data_mode != LCDCAM_RAW_DATA_RGB565) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_data_mode\","
               "\"allowed\":[\"RGB565\"],\"binary_len\":0}\n",
               line);
        return;
    }

    if (emit_len == 0) {
        emit_len = (int)gbc_lcd_source_emit_len(data_mode);
    }

    for (int frame = 0; frame < frame_count; ++frame) {
        lcdcam_raw_result_t result = {0};
        int64_t capture_us = 0;
        esp_err_t err = gbc_lcd_source_capture_frame((uint32_t)timeout_ms,
                                                     data_mode,
                                                     pclk_invert != 0,
                                                     &result,
                                                     &capture_us);
        print_lcdcam_raw_binary_result("GBC_SOURCE_STREAM_BIN",
                                       "gbc_lcd_source_driver_compat_stream_frame",
                                       &result,
                                       GBC_LCD_SOURCE_CAPTURE_WIDTH,
                                       GBC_LCD_SOURCE_CAPTURE_HEIGHT,
                                       err == ESP_OK,
                                       err,
                                       (uint32_t)emit_len,
                                       capture_us);
        lcdcam_raw_result_free(&result);
        if (err != ESP_OK) {
            break;
        }
    }
}


static void handle_safe_idle(void)
{
    esp_err_t err = lcdcam_raw_enter_safe_idle();
    printf("{\"ok\":%s,\"command\":\"SAFE_IDLE\",\"error\":\"%s\",\"err\":%d,"
           "\"mode\":\"lcdcam_detached_gpio_floating_input\"}\n",
           err == ESP_OK ? "true" : "false",
           err == ESP_OK ? "none" : esp_err_to_name(err),
           err);
}

static void handle_electrical_isolate(void)
{
    esp_err_t err = lcdcam_raw_enter_electrical_isolate();
    printf("{\"ok\":%s,\"command\":\"ELECTRICAL_ISOLATE\",\"error\":\"%s\",\"err\":%d,"
           "\"mode\":\"lcdcam_detached_gpio_disabled_no_pulls\","
           "\"note\":\"capture GPIO pads disabled; external leakage paths may still require hardware isolation\"}\n",
           err == ESP_OK ? "true" : "false",
           err == ESP_OK ? "none" : esp_err_to_name(err),
           err);
}

static void handle_dvp_capture_raw(const char *line, bool byte_count_eof)
{
    char de_name[8] = {0};
    int timeout_ms = 0;
    int vsync_invert = 1;
    int de_invert = 0;
    int pclk_invert = 0;
    int h_res = 160;
    int v_res = 144;
    int parsed = sscanf(line,
                        byte_count_eof
                            ? "DVP_CAPTURE_RAW_LEN %7s %d %d %d %d %d %d"
                            : "DVP_CAPTURE_RAW %7s %d %d %d %d %d %d",
                        de_name,
                        &timeout_ms,
                        &vsync_invert,
                        &de_invert,
                        &pclk_invert,
                        &h_res,
                        &v_res);
    if (parsed < 2 ||
        timeout_ms <= 0 || timeout_ms > 5000 ||
        h_res <= 0 || h_res > 320 ||
        v_res <= 0 || v_res > 240) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_arguments\","
               "\"usage\":\"%s <SPL|LP> <timeout_ms_1_to_5000> [vsync_invert_0_or_1] [de_invert_0_or_1] [pclk_invert_0_or_1] [h_res_1_to_320] [v_res_1_to_240]\"}\n",
               line,
               byte_count_eof ? "DVP_CAPTURE_RAW_LEN" : "DVP_CAPTURE_RAW");
        return;
    }
    if ((vsync_invert != 0 && vsync_invert != 1) ||
        (de_invert != 0 && de_invert != 1) ||
        (pclk_invert != 0 && pclk_invert != 1)) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_polarity\","
               "\"usage\":\"polarity values must be 0 or 1\"}\n",
               line);
        return;
    }

    dvp_probe_de_source_t de_source = DVP_PROBE_DE_SPL;
    if (!parse_dvp_de_source(de_name, &de_source)) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_de_source\","
               "\"allowed\":[\"SPL\",\"LP\"]}\n",
               line);
        return;
    }

    dvp_probe_capture_result_t result = {0};
    esp_err_t err = byte_count_eof
                        ? dvp_probe_capture_raw8_byte_count(de_source,
                                                            (uint32_t)h_res,
                                                            (uint32_t)v_res,
                                                            (uint32_t)timeout_ms,
                                                            vsync_invert != 0,
                                                            de_invert != 0,
                                                            pclk_invert != 0,
                                                            &result)
                        : dvp_probe_capture_raw8(de_source,
                                                 (uint32_t)h_res,
                                                 (uint32_t)v_res,
                                                 (uint32_t)timeout_ms,
                                                 vsync_invert != 0,
                                                 de_invert != 0,
                                                 pclk_invert != 0,
                                                 &result);
    if (err != ESP_OK) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"dvp_capture_failed\","
               "\"de\":\"%s\",\"vsync_invert\":%s,\"de_invert\":%s,\"pclk_invert\":%s,\"esp_err\":%d}\n",
               byte_count_eof ? "DVP_CAPTURE_RAW_LEN" : "DVP_CAPTURE_RAW",
               dvp_de_source_name(de_source),
               vsync_invert ? "true" : "false",
               de_invert ? "true" : "false",
               pclk_invert ? "true" : "false",
               err);
        return;
    }

    printf("{\"ok\":true,\"command\":\"%s\","
           "\"mode\":\"%s\","
           "\"width\":%d,\"height\":%d,\"color\":\"RAW8\","
           "\"packing\":\"rg44_upper_bits\","
           "\"pclk\":\"DCLK\",\"pclk_gpio\":22,"
           "\"pclk_invert\":%s,"
           "\"vsync\":\"SPS\",\"vsync_gpio\":33,"
           "\"vsync_invert\":%s,"
           "\"de\":\"%s\",\"de_gpio\":%d,"
           "\"de_invert\":%s,"
           "\"byte_count_eof\":%s,"
           "\"data_mapping\":["
           "{\"bit\":0,\"signal\":\"R2\",\"gpio\":16},"
           "{\"bit\":1,\"signal\":\"R3\",\"gpio\":15},"
           "{\"bit\":2,\"signal\":\"R4\",\"gpio\":14},"
           "{\"bit\":3,\"signal\":\"R5\",\"gpio\":13},"
           "{\"bit\":4,\"signal\":\"G2\",\"gpio\":10},"
           "{\"bit\":5,\"signal\":\"G3\",\"gpio\":9},"
           "{\"bit\":6,\"signal\":\"G4\",\"gpio\":8},"
           "{\"bit\":7,\"signal\":\"G5\",\"gpio\":7}],"
           "\"buffer_len\":%u,\"received_size\":%u,"
           "\"checksum\":%lu,\"min_value\":%u,\"max_value\":%u,"
           "\"lower6_transitions\":%lu,\"raw8_transitions\":%lu,\"data_hex\":\"",
           byte_count_eof ? "DVP_CAPTURE_RAW_LEN" : "DVP_CAPTURE_RAW",
           byte_count_eof ? "experimental_dvp_raw8_byte_count_eof" : "experimental_dvp_raw8_one_frame",
           h_res,
           v_res,
           result.pclk_invert ? "true" : "false",
           result.vsync_invert ? "true" : "false",
           dvp_de_source_name(result.de_source),
           result.de_source == DVP_PROBE_DE_LP ? 21 : 19,
           result.de_invert ? "true" : "false",
           result.byte_count_eof ? "true" : "false",
           (unsigned)result.buffer_len,
           (unsigned)result.received_size,
           (unsigned long)result.checksum,
           (unsigned)result.min_value,
           (unsigned)result.max_value,
           (unsigned long)result.lower6_transitions,
           (unsigned long)result.raw8_transitions);

    for (size_t i = 0; i < result.buffer_len; ++i) {
        printf("%02x", (unsigned)result.buffer[i]);
    }
    printf("\"}\n");
    dvp_probe_capture_result_free(&result);
}

static void handle_isp_dvp_capture(const char *line, bool output_rgb565)
{
    char hsync_name[8] = {0};
    char de_name[8] = {0};
    int timeout_ms = 0;
    int hsync_invert = 0;
    int vsync_invert = 0;
    int de_invert = 0;
    int pclk_invert = 0;
    int h_res = 160;
    int v_res = 144;
    int parsed = sscanf(line,
                        output_rgb565
                            ? "ISP_DVP_CAPTURE_RGB565 %7s %7s %d %d %d %d %d %d %d"
                            : "ISP_DVP_CAPTURE_RAW %7s %7s %d %d %d %d %d %d %d",
                        hsync_name,
                        de_name,
                        &timeout_ms,
                        &hsync_invert,
                        &vsync_invert,
                        &de_invert,
                        &pclk_invert,
                        &h_res,
                        &v_res);
    if (parsed < 3 ||
        timeout_ms <= 0 || timeout_ms > 5000 ||
        h_res <= 0 || h_res > 320 ||
        v_res <= 0 || v_res > 240) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_arguments\","
               "\"usage\":\"%s <HSYNC:SPL|LP|NC> <DE:SPL|LP|NC> <timeout_ms_1_to_5000> [hsync_invert_0_or_1] [vsync_invert_0_or_1] [de_invert_0_or_1] [pclk_invert_0_or_1] [h_res_1_to_320] [v_res_1_to_240]\"}\n",
               line,
               output_rgb565 ? "ISP_DVP_CAPTURE_RGB565" : "ISP_DVP_CAPTURE_RAW");
        return;
    }
    if ((hsync_invert != 0 && hsync_invert != 1) ||
        (vsync_invert != 0 && vsync_invert != 1) ||
        (de_invert != 0 && de_invert != 1) ||
        (pclk_invert != 0 && pclk_invert != 1)) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_polarity\","
               "\"usage\":\"polarity values must be 0 or 1\"}\n",
               line);
        return;
    }

    dvp_probe_sync_source_t hsync_source = DVP_PROBE_SYNC_NONE;
    dvp_probe_sync_source_t de_source = DVP_PROBE_SYNC_NONE;
    if (!parse_dvp_sync_source(hsync_name, &hsync_source) ||
        !parse_dvp_sync_source(de_name, &de_source)) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_sync_source\","
               "\"allowed\":[\"SPL\",\"LP\",\"NC\"]}\n",
               line);
        return;
    }

    dvp_probe_capture_result_t result = {0};
    esp_err_t err = output_rgb565
                        ? dvp_probe_capture_isp_rgb565(hsync_source,
                                                       de_source,
                                                       (uint32_t)h_res,
                                                       (uint32_t)v_res,
                                                       (uint32_t)timeout_ms,
                                                       hsync_invert != 0,
                                                       vsync_invert != 0,
                                                       de_invert != 0,
                                                       pclk_invert != 0,
                                                       &result)
                        : dvp_probe_capture_isp_raw8(hsync_source,
                                                     de_source,
                                                     (uint32_t)h_res,
                                                     (uint32_t)v_res,
                                                     (uint32_t)timeout_ms,
                                                     hsync_invert != 0,
                                                     vsync_invert != 0,
                                                     de_invert != 0,
                                                     pclk_invert != 0,
                                                     &result);
    if (err != ESP_OK) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"isp_dvp_capture_failed\","
               "\"hsync\":\"%s\",\"de\":\"%s\",\"hsync_invert\":%s,\"vsync_invert\":%s,"
               "\"de_invert\":%s,\"pclk_invert\":%s,\"failure_stage\":\"%s\","
               "\"failure_err\":%d,\"received_size\":%u,\"esp_err\":%d}\n",
               output_rgb565 ? "ISP_DVP_CAPTURE_RGB565" : "ISP_DVP_CAPTURE_RAW",
               dvp_sync_source_name(hsync_source),
               dvp_sync_source_name(de_source),
               hsync_invert ? "true" : "false",
               vsync_invert ? "true" : "false",
               de_invert ? "true" : "false",
               pclk_invert ? "true" : "false",
               result.failure_stage == NULL ? "unknown" : result.failure_stage,
               result.failure_err,
               (unsigned)result.received_size,
               err);
        return;
    }

    printf("{\"ok\":true,\"command\":\"%s\","
           "\"mode\":\"%s\","
           "\"width\":%d,\"height\":%d,\"color\":\"%s\","
           "\"packing\":\"%s\","
           "\"pclk\":\"DCLK\",\"pclk_gpio\":22,\"pclk_invert\":%s,"
           "\"vsync\":\"SPS\",\"vsync_gpio\":33,\"vsync_invert\":%s,"
           "\"hsync\":\"%s\",\"hsync_gpio\":%d,\"hsync_invert\":%s,"
           "\"de\":\"%s\",\"de_gpio\":%d,\"de_invert\":%s,"
           "\"failure_stage\":\"%s\",\"failure_err\":%d,"
           "\"buffer_len\":%u,\"received_size\":%u,"
           "\"checksum\":%lu,\"min_value\":%u,\"max_value\":%u,"
           "\"lower6_transitions\":%lu,\"raw8_transitions\":%lu,\"data_hex\":\"",
           output_rgb565 ? "ISP_DVP_CAPTURE_RGB565" : "ISP_DVP_CAPTURE_RAW",
           output_rgb565 ? "experimental_isp_dvp_rgb565_one_frame" : "experimental_isp_dvp_raw8_one_frame",
           h_res,
           v_res,
           output_rgb565 ? "RGB565" : "RAW8",
           output_rgb565 ? "isp_rgb565_from_rg44_raw8_input" : "rg44_upper_bits",
           pclk_invert ? "true" : "false",
           vsync_invert ? "true" : "false",
           dvp_sync_source_name(hsync_source),
           dvp_sync_source_gpio(hsync_source),
           hsync_invert ? "true" : "false",
           dvp_sync_source_name(de_source),
           dvp_sync_source_gpio(de_source),
           de_invert ? "true" : "false",
           result.failure_stage == NULL ? "none" : result.failure_stage,
           result.failure_err,
           (unsigned)result.buffer_len,
           (unsigned)result.received_size,
           (unsigned long)result.checksum,
           (unsigned)result.min_value,
           (unsigned)result.max_value,
           (unsigned long)result.lower6_transitions,
           (unsigned long)result.raw8_transitions);

    for (size_t i = 0; i < result.buffer_len; ++i) {
        printf("%02x", (unsigned)result.buffer[i]);
    }
    printf("\"}\n");
    dvp_probe_capture_result_free(&result);
}

static void handle_isp_dvp_capture_raw(const char *line)
{
    handle_isp_dvp_capture(line, false);
}

static void handle_isp_dvp_capture_rgb565(const char *line)
{
    handle_isp_dvp_capture(line, true);
}

static void handle_command(const char *line)
{
    diagnostics_record_command();

    if (strcmp(line, "PING") == 0) {
        printf("{\"ok\":true,\"response\":\"PONG\"}\n");
        return;
    }

    if (strcmp(line, "GET_VERSION") == 0) {
        printf("{\"ok\":true,\"name\":\"%s\",\"version\":\"%s\",\"phase\":\"%s\"}\n",
               GBC_P4_PROBE_NAME,
               GBC_P4_PROBE_VERSION,
               GBC_P4_PROBE_PHASE);
        return;
    }

    if (strcmp(line, "TRANSPORT_STATUS") == 0) {
        handle_transport_status();
        return;
    }

    if (strcmp(line, "CORE_STATUS") == 0) {
        print_core_status();
        return;
    }

    if (strcmp(line, "EXPORT_STATS") == 0) {
        char json[256];
        diagnostics_format_json(json, sizeof(json));
        printf("{\"ok\":true,\"stats\":%s}\n", json);
        return;
    }

    if (strcmp(line, "GET_PINMAP") == 0) {
        print_pinmap();
        return;
    }

    if (strcmp(line, "SAFE_IDLE") == 0) {
        handle_safe_idle();
        return;
    }

    if (strcmp(line, "ELECTRICAL_ISOLATE") == 0 || strcmp(line, "SAFE_ISOLATE") == 0) {
        handle_electrical_isolate();
        return;
    }

    if (command_has_prefix(line, "READ_GPIO")) {
        handle_read_gpio(line);
        return;
    }

    if (command_has_prefix(line, "COUNT_GPIO_EDGES")) {
        handle_count_gpio_edges(line);
        return;
    }

    if (command_has_prefix(line, "MEASURE_DCLK")) {
        handle_measure_dclk(line);
        return;
    }

    if (strcmp(line, "GBC_SOURCE_STATUS") == 0) {
        handle_gbc_source_status();
        return;
    }

    if (command_has_prefix(line, "USB_BENCH_STREAM_BIN")) {
        handle_usb_bench_stream_bin(line);
        return;
    }

    if (command_has_prefix(line, "PIPELINE_BENCH")) {
        handle_pipeline_bench(line);
        return;
    }

#ifdef GBC_P4_SAFE_RECOVERY
    if (command_has_prefix(line, "CAPTURE") ||
        command_has_prefix(line, "GBC_SOURCE_FRAME_BIN") ||
        command_has_prefix(line, "GBC_SOURCE_STREAM_BIN") ||
        command_has_prefix(line, "GBC_SOURCE_BENCH") ||
        command_has_prefix(line, "GBC_PIPELINE_BENCH") ||
        command_has_prefix(line, "GBC_PIPELINE_BENCH_PERSIST") ||
        command_has_prefix(line, "GBC_REARM_BENCH") ||
        command_has_prefix(line, "GBC_FRAME_REARM_BENCH") ||
        command_has_prefix(line, "GBC_CAPCARD_STREAM_BIN") ||
        command_has_prefix(line, "DVP_") ||
        command_has_prefix(line, "ISP_DVP_") ||
        command_has_prefix(line, "LCDCAM_") ||
        command_has_prefix(line, "SET_TRIGGER") ||
        command_has_prefix(line, "DUMP_BUFFER")) {
        diagnostics_record_unsupported_command();
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"safe_recovery_blocks_capture\","
               "\"phase\":\"%s\",\"allowed\":[\"PING\",\"GET_VERSION\",\"CORE_STATUS\","
               "\"EXPORT_STATS\",\"GET_PINMAP\",\"SAFE_IDLE\",\"ELECTRICAL_ISOLATE\","
               "\"READ_GPIO\",\"COUNT_GPIO_EDGES\",\"MEASURE_DCLK\",\"GBC_SOURCE_STATUS\"]}\n",
               line,
               GBC_P4_PROBE_PHASE);
        return;
    }
#endif

    if (command_has_prefix(line, "GBC_SOURCE_FRAME_BIN")) {
        handle_gbc_source_frame_bin(line);
        return;
    }

    if (command_has_prefix(line, "GBC_SOURCE_STREAM_BIN")) {
        handle_gbc_source_stream_bin(line);
        return;
    }

    if (command_has_prefix(line, "GBC_SOURCE_BENCH")) {
        handle_gbc_source_bench(line);
        return;
    }

    if (command_has_prefix(line, "GBC_PIPELINE_BENCH") ||
        command_has_prefix(line, "GBC_PIPELINE_BENCH_PERSIST")) {
        handle_gbc_pipeline_bench(line);
        return;
    }

    if (command_has_prefix(line, "GBC_REARM_BENCH")) {
        handle_gbc_rearm_bench(line);
        return;
    }

    if (command_has_prefix(line, "GBC_FRAME_REARM_BENCH")) {
        handle_gbc_rearm_bench(line);
        return;
    }

    if (command_has_prefix(line, "GBC_CAPCARD_STREAM_BIN")) {
        handle_gbc_capcard_stream_bin(line);
        return;
    }

    if (command_has_prefix(line, "CAPTURE_TIMING_EDGES")) {
        handle_capture_timing_edges(line);
        return;
    }

    if (command_has_prefix(line, "CAPTURE_RED_DCLK")) {
        handle_capture_red_dclk(line);
        return;
    }

    if (command_has_prefix(line, "CAPTURE_LINE_CLOCKS")) {
        handle_capture_line_clocks(line);
        return;
    }

    if (command_has_prefix(line, "CAPTURE_RG_LINE_BURSTS")) {
        handle_capture_rg_line_bursts(line);
        return;
    }
    if (command_has_prefix(line, "CAPTURE_RGB666_LINE_BURSTS")) {
        handle_capture_rgb666_line_bursts(line);
        return;
    }

    if (strcmp(line, "DVP_PROBE_ALLOC") == 0) {
        handle_dvp_probe_alloc();
        return;
    }

    if (command_has_prefix(line, "LCDCAM_RAW_STREAM_BIN")) {
        handle_lcdcam_raw_stream_bin(line);
        return;
    }

    if (strcmp(line, "LCDCAM_RAW_CAPTURE_SRC_BIN") == 0) {
        handle_lcdcam_raw_capture_src_bin();
        return;
    }

    if (command_has_prefix(line, "LCDCAM_RAW_CAPTURE_BIN")) {
        handle_lcdcam_raw_capture_bin(line);
        return;
    }

    if (command_has_prefix(line, "LCDCAM_RAW_CAPTURE")) {
        handle_lcdcam_raw_capture(line);
        return;
    }

    if (command_has_prefix(line, "DVP_CAPTURE_RAW_LEN")) {
        handle_dvp_capture_raw(line, true);
        return;
    }

    if (command_has_prefix(line, "DVP_CAPTURE_RAW")) {
        handle_dvp_capture_raw(line, false);
        return;
    }

    if (command_has_prefix(line, "ISP_DVP_CAPTURE_RAW")) {
        handle_isp_dvp_capture_raw(line);
        return;
    }

    if (command_has_prefix(line, "ISP_DVP_CAPTURE_RGB565")) {
        handle_isp_dvp_capture_rgb565(line);
        return;
    }

    if (command_has_prefix(line, "MEASURE_CLOCKS") ||
        command_has_prefix(line, "CAPTURE_TIMING") ||
        command_has_prefix(line, "CAPTURE_RAW") ||
        command_has_prefix(line, "CAPTURE_FRAME") ||
        command_has_prefix(line, "SET_TRIGGER") ||
        command_has_prefix(line, "DUMP_BUFFER")) {
        print_no_capture_pins_configured(line);
        return;
    }

    diagnostics_record_unsupported_command();
    printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"unknown_command\"}\n", line);
}

static void usb_protocol_task(void *arg)
{
    (void)arg;

    char line[256];
    ESP_LOGI(TAG, "USB serial command task started on core %d", (int)xPortGetCoreID());
    printf("{\"ok\":true,\"event\":\"ready\",\"name\":\"%s\",\"version\":\"%s\",\"phase\":\"%s\","
           "\"task_core\":%d,\"pinned_core\":%d}\n",
           GBC_P4_PROBE_NAME,
           GBC_P4_PROBE_VERSION,
           GBC_P4_PROBE_PHASE,
           (int)xPortGetCoreID(),
           USB_PROTOCOL_TASK_CORE);

    while (true) {
        if (fgets(line, sizeof(line), stdin) == NULL) {
            vTaskDelay(pdMS_TO_TICKS(50));
            clearerr(stdin);
            continue;
        }

        trim_line(line);
        if (line[0] == '\0') {
            continue;
        }

        handle_command(line);
        fflush(stdout);
    }
}

void usb_protocol_start(void)
{
    BaseType_t ok = xTaskCreatePinnedToCore(usb_protocol_task,
                                            "usb_protocol",
                                            USB_PROTOCOL_TASK_STACK_WORDS,
                                            NULL,
                                            USB_PROTOCOL_TASK_PRIORITY,
                                            NULL,
                                            USB_PROTOCOL_TASK_CORE);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "failed to create USB protocol task on core %d", USB_PROTOCOL_TASK_CORE);
    }
}
