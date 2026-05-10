#include "timing_analysis.h"

#include <stdbool.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/gpio_struct.h"

static const char *TAG = "timing_analysis";

typedef struct {
    const char *name;
    gpio_num_t gpio;
} phase1_timing_signal_t;

static const phase1_timing_signal_t PHASE1_TIMING_SIGNALS[] = {
    {.name = "SPL", .gpio = GPIO_NUM_19},
    {.name = "PS", .gpio = GPIO_NUM_20},
    {.name = "LP", .gpio = GPIO_NUM_21},
    {.name = "CLS", .gpio = GPIO_NUM_3},
    {.name = "SPS", .gpio = GPIO_NUM_33},
};

#define TIMING_EDGE_CAPTURE_MAX_EVENTS 8192U
#define RED_DCLK_CAPTURE_MAX_SAMPLES 2048U
#define LINE_CLOCK_CAPTURE_MAX_SAMPLES 256U
#define RG_LINE_BURST_MAX_WIDTH 160U
#define RG_LINE_BURST_MAX_HEIGHT 144U
#define RG_LINE_BURST_MAX_PIXELS (RG_LINE_BURST_MAX_WIDTH * RG_LINE_BURST_MAX_HEIGHT)
#define RGB666_LINE_BURST_BYTES_PER_PIXEL 3U
#define RGB666_LINE_BURST_MAX_BYTES (RG_LINE_BURST_MAX_PIXELS * RGB666_LINE_BURST_BYTES_PER_PIXEL)

static uint8_t IRAM_ATTR sample_red_bus(void)
{
    uint8_t red = 0;
    red |= (uint8_t)gpio_get_level(GPIO_NUM_18) << 0; // R0
    red |= (uint8_t)gpio_get_level(GPIO_NUM_17) << 1; // R1
    red |= (uint8_t)gpio_get_level(GPIO_NUM_16) << 2; // R2
    red |= (uint8_t)gpio_get_level(GPIO_NUM_15) << 3; // R3
    red |= (uint8_t)gpio_get_level(GPIO_NUM_14) << 4; // R4
    red |= (uint8_t)gpio_get_level(GPIO_NUM_13) << 5; // R5
    return red;
}

static inline uint8_t IRAM_ATTR sample_rg44_from_gpio_in(uint32_t gpio_in)
{
    uint8_t value = 0;
    value |= (uint8_t)((gpio_in >> 16) & 0x1) << 0; // R2
    value |= (uint8_t)((gpio_in >> 15) & 0x1) << 1; // R3
    value |= (uint8_t)((gpio_in >> 14) & 0x1) << 2; // R4
    value |= (uint8_t)((gpio_in >> 13) & 0x1) << 3; // R5
    value |= (uint8_t)((gpio_in >> 10) & 0x1) << 4; // G2
    value |= (uint8_t)((gpio_in >> 9) & 0x1) << 5;  // G3
    value |= (uint8_t)((gpio_in >> 8) & 0x1) << 6;  // G4
    value |= (uint8_t)((gpio_in >> 7) & 0x1) << 7;  // G5
    return value;
}

static inline void IRAM_ATTR sample_rgb666_from_gpio_in(uint32_t gpio_in, uint32_t gpio_in1, uint8_t *out)
{
    uint8_t red = 0;
    red |= (uint8_t)((gpio_in >> 18) & 0x1) << 0; // R0
    red |= (uint8_t)((gpio_in >> 17) & 0x1) << 1; // R1
    red |= (uint8_t)((gpio_in >> 16) & 0x1) << 2; // R2
    red |= (uint8_t)((gpio_in >> 15) & 0x1) << 3; // R3
    red |= (uint8_t)((gpio_in >> 14) & 0x1) << 4; // R4
    red |= (uint8_t)((gpio_in >> 13) & 0x1) << 5; // R5

    uint8_t green = 0;
    green |= (uint8_t)((gpio_in >> 12) & 0x1) << 0; // G0
    green |= (uint8_t)((gpio_in >> 11) & 0x1) << 1; // G1
    green |= (uint8_t)((gpio_in >> 10) & 0x1) << 2; // G2
    green |= (uint8_t)((gpio_in >> 9) & 0x1) << 3;  // G3
    green |= (uint8_t)((gpio_in >> 8) & 0x1) << 4;  // G4
    green |= (uint8_t)((gpio_in >> 7) & 0x1) << 5;  // G5

    uint8_t blue = 0;
    blue |= (uint8_t)((gpio_in1 >> (36 - 32)) & 0x1) << 0; // B0
    blue |= (uint8_t)((gpio_in1 >> (45 - 32)) & 0x1) << 1; // B1
    blue |= (uint8_t)((gpio_in1 >> (46 - 32)) & 0x1) << 2; // B2
    blue |= (uint8_t)((gpio_in1 >> (47 - 32)) & 0x1) << 3; // B3
    blue |= (uint8_t)((gpio_in1 >> (48 - 32)) & 0x1) << 4; // B4
    blue |= (uint8_t)((gpio_in1 >> (50 - 32)) & 0x1) << 5; // B5

    out[0] = red;
    out[1] = green;
    out[2] = blue;
}

static timing_edge_event_t s_timing_events[TIMING_EDGE_CAPTURE_MAX_EVENTS];
static red_dclk_sample_t s_red_dclk_samples[RED_DCLK_CAPTURE_MAX_SAMPLES];
static line_clock_sample_t s_line_clock_samples[LINE_CLOCK_CAPTURE_MAX_SAMPLES];
static uint8_t s_rg_line_burst_pixels[RG_LINE_BURST_MAX_PIXELS];
static uint8_t s_rgb666_line_burst_pixels[RGB666_LINE_BURST_MAX_BYTES];
static uint16_t s_rg_line_burst_line_counts[RG_LINE_BURST_MAX_HEIGHT];
static portMUX_TYPE s_timing_capture_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_line_clock_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_timing_capture_active;
static volatile size_t s_timing_event_count;
static volatile uint32_t s_timing_overflow_count;
static volatile int64_t s_timing_start_us;
static volatile bool s_line_clock_active;
static volatile bool s_line_clock_falling_edge;
static volatile size_t s_line_clock_sample_count;
static volatile uint32_t s_line_clock_overflow_count;
static volatile int64_t s_line_clock_start_us;
static volatile int32_t s_line_clock_previous_count;
static pcnt_unit_handle_t s_line_clock_pcnt_unit;

static void IRAM_ATTR timing_edge_isr(void *arg)
{
    int gpio_num = (int)(intptr_t)arg;
    int level = gpio_get_level((gpio_num_t)gpio_num);
    int64_t now_us = esp_timer_get_time();

    portENTER_CRITICAL_ISR(&s_timing_capture_lock);
    if (s_timing_capture_active) {
        int64_t delta_us = now_us - s_timing_start_us;
        if (delta_us < 0) {
            portEXIT_CRITICAL_ISR(&s_timing_capture_lock);
            return;
        }

        size_t index = s_timing_event_count;
        if (index < TIMING_EDGE_CAPTURE_MAX_EVENTS) {
            s_timing_events[index].t_us = (uint32_t)delta_us;
            s_timing_events[index].gpio = (uint8_t)gpio_num;
            s_timing_events[index].level = (uint8_t)level;
            s_timing_events[index].red6 = sample_red_bus();
            s_timing_event_count = index + 1;
        } else {
            s_timing_overflow_count++;
        }
    }
    portEXIT_CRITICAL_ISR(&s_timing_capture_lock);
}

static void IRAM_ATTR line_clock_marker_isr(void *arg)
{
    (void)arg;
    int level = gpio_get_level((gpio_num_t)(intptr_t)arg);
    bool edge_matches = s_line_clock_falling_edge ? (level == 0) : (level == 1);
    if (!edge_matches || !s_line_clock_active || s_line_clock_pcnt_unit == NULL) {
        return;
    }

    int current_count = 0;
    if (pcnt_unit_get_count(s_line_clock_pcnt_unit, &current_count) != ESP_OK) {
        return;
    }

    int64_t now_us = esp_timer_get_time();
    portENTER_CRITICAL_ISR(&s_line_clock_lock);
    if (s_line_clock_active) {
        size_t index = s_line_clock_sample_count;
        if (index < LINE_CLOCK_CAPTURE_MAX_SAMPLES) {
            s_line_clock_samples[index].index = (uint16_t)index;
            s_line_clock_samples[index].t_us = (uint32_t)(now_us - s_line_clock_start_us);
            s_line_clock_samples[index].dclk_delta = current_count - s_line_clock_previous_count;
            s_line_clock_samples[index].dclk_total = current_count;
            s_line_clock_previous_count = current_count;
            s_line_clock_sample_count = index + 1;
        } else {
            s_line_clock_overflow_count++;
        }
    }
    portEXIT_CRITICAL_ISR(&s_line_clock_lock);
}

const char *timing_analysis_signal_name_for_gpio(int gpio_num)
{
    for (size_t i = 0; i < sizeof(PHASE1_TIMING_SIGNALS) / sizeof(PHASE1_TIMING_SIGNALS[0]); ++i) {
        if (gpio_num == PHASE1_TIMING_SIGNALS[i].gpio) {
            return PHASE1_TIMING_SIGNALS[i].name;
        }
    }

    return "UNKNOWN";
}

bool timing_analysis_is_phase1_signal_gpio(int gpio_num)
{
    return timing_analysis_signal_name_for_gpio(gpio_num)[0] != 'U';
}

static esp_err_t configure_timing_gpio(gpio_num_t gpio_num)
{
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << gpio_num,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };

    return gpio_config(&config);
}

static esp_err_t configure_red_gpio(gpio_num_t gpio_num)
{
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << gpio_num,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    return gpio_config(&config);
}

static bool wait_for_falling_edge(gpio_num_t gpio_num, int64_t deadline_us)
{
    int previous_level = gpio_get_level(gpio_num);
    while (esp_timer_get_time() < deadline_us) {
        int level = gpio_get_level(gpio_num);
        if (previous_level == 1 && level == 0) {
            return true;
        }
        previous_level = level;
    }

    return false;
}

static bool wait_for_rising_edge(gpio_num_t gpio_num, int64_t deadline_us)
{
    int previous_level = gpio_get_level(gpio_num);
    while (esp_timer_get_time() < deadline_us) {
        int level = gpio_get_level(gpio_num);
        if (previous_level == 0 && level == 1) {
            return true;
        }
        previous_level = level;
    }

    return false;
}

static bool wait_for_marker_falling_or_sps_rising(gpio_num_t marker_gpio,
                                                  int64_t deadline_us,
                                                  bool *sps_rising_seen)
{
    int previous_marker_level = gpio_get_level(marker_gpio);
    int previous_sps_level = gpio_get_level(GPIO_NUM_33);
    if (sps_rising_seen != NULL) {
        *sps_rising_seen = false;
    }

    while (esp_timer_get_time() < deadline_us) {
        int marker_level = gpio_get_level(marker_gpio);
        int sps_level = gpio_get_level(GPIO_NUM_33);
        if (previous_sps_level == 0 && sps_level == 1) {
            if (sps_rising_seen != NULL) {
                *sps_rising_seen = true;
            }
            return false;
        }
        if (previous_marker_level == 1 && marker_level == 0) {
            return true;
        }
        previous_marker_level = marker_level;
        previous_sps_level = sps_level;
    }

    return false;
}

esp_err_t timing_analysis_capture_edges(uint32_t duration_ms, timing_edge_capture_result_t *result)
{
    if (result == NULL || duration_ms == 0 || duration_ms > 250) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    for (size_t i = 0; i < sizeof(PHASE1_TIMING_SIGNALS) / sizeof(PHASE1_TIMING_SIGNALS[0]); ++i) {
        gpio_num_t gpio_num = PHASE1_TIMING_SIGNALS[i].gpio;
        gpio_intr_disable(gpio_num);
        gpio_isr_handler_remove(gpio_num);

        err = configure_timing_gpio(gpio_num);
        if (err != ESP_OK) {
            return err;
        }

        err = gpio_isr_handler_add(gpio_num, timing_edge_isr, (void *)(intptr_t)gpio_num);
        if (err != ESP_OK) {
            return err;
        }
    }

    const gpio_num_t red_gpios[] = {
        GPIO_NUM_13,
        GPIO_NUM_14,
        GPIO_NUM_15,
        GPIO_NUM_16,
        GPIO_NUM_17,
        GPIO_NUM_18,
    };
    for (size_t i = 0; i < sizeof(red_gpios) / sizeof(red_gpios[0]); ++i) {
        err = configure_red_gpio(red_gpios[i]);
        if (err != ESP_OK) {
            return err;
        }
    }

    portENTER_CRITICAL(&s_timing_capture_lock);
    s_timing_event_count = 0;
    s_timing_overflow_count = 0;
    s_timing_start_us = esp_timer_get_time();
    s_timing_capture_active = true;
    portEXIT_CRITICAL(&s_timing_capture_lock);

    for (size_t i = 0; i < sizeof(PHASE1_TIMING_SIGNALS) / sizeof(PHASE1_TIMING_SIGNALS[0]); ++i) {
        gpio_intr_enable(PHASE1_TIMING_SIGNALS[i].gpio);
    }

    vTaskDelay(pdMS_TO_TICKS(duration_ms));

    for (size_t i = 0; i < sizeof(PHASE1_TIMING_SIGNALS) / sizeof(PHASE1_TIMING_SIGNALS[0]); ++i) {
        gpio_intr_disable(PHASE1_TIMING_SIGNALS[i].gpio);
    }

    portENTER_CRITICAL(&s_timing_capture_lock);
    s_timing_capture_active = false;
    size_t captured_count = s_timing_event_count;
    uint32_t overflow_count = s_timing_overflow_count;
    int64_t start_time_us = s_timing_start_us;
    portEXIT_CRITICAL(&s_timing_capture_lock);

    for (size_t i = 0; i < sizeof(PHASE1_TIMING_SIGNALS) / sizeof(PHASE1_TIMING_SIGNALS[0]); ++i) {
        gpio_isr_handler_remove(PHASE1_TIMING_SIGNALS[i].gpio);
    }

    result->events = s_timing_events;
    result->event_count = captured_count;
    result->overflow_count = overflow_count;
    result->duration_ms = duration_ms;
    result->start_time_us = start_time_us;
    return ESP_OK;
}

esp_err_t timing_analysis_capture_red_on_dclk(uint32_t requested_sample_count, uint32_t timeout_ms, red_dclk_capture_result_t *result)
{
    if (result == NULL || requested_sample_count == 0 || requested_sample_count > RED_DCLK_CAPTURE_MAX_SAMPLES ||
        timeout_ms == 0 || timeout_ms > 1000) {
        return ESP_ERR_INVALID_ARG;
    }

    const gpio_num_t input_gpios[] = {
        GPIO_NUM_13,
        GPIO_NUM_14,
        GPIO_NUM_15,
        GPIO_NUM_16,
        GPIO_NUM_17,
        GPIO_NUM_18,
        GPIO_NUM_19, // SPL trigger
        GPIO_NUM_22, // DCLK sample clock
    };

    for (size_t i = 0; i < sizeof(input_gpios) / sizeof(input_gpios[0]); ++i) {
        esp_err_t err = configure_red_gpio(input_gpios[i]);
        if (err != ESP_OK) {
            return err;
        }
    }

    int64_t command_start_us = esp_timer_get_time();
    int64_t deadline_us = command_start_us + ((int64_t)timeout_ms * 1000);
    bool trigger_seen = wait_for_falling_edge(GPIO_NUM_19, deadline_us);

    size_t sample_count = 0;
    int64_t trigger_time_us = esp_timer_get_time();
    bool timed_out = false;

    if (trigger_seen) {
        int previous_dclk = gpio_get_level(GPIO_NUM_22);
        while (sample_count < requested_sample_count) {
            int64_t now_us = esp_timer_get_time();
            if (now_us >= deadline_us) {
                timed_out = true;
                break;
            }

            int dclk = gpio_get_level(GPIO_NUM_22);
            if (previous_dclk == 0 && dclk == 1) {
                s_red_dclk_samples[sample_count].t_us = (uint32_t)(now_us - trigger_time_us);
                s_red_dclk_samples[sample_count].index = (uint16_t)sample_count;
                s_red_dclk_samples[sample_count].red6 = sample_red_bus();
                sample_count++;
            }
            previous_dclk = dclk;
        }
    } else {
        timed_out = true;
    }

    result->samples = s_red_dclk_samples;
    result->sample_count = sample_count;
    result->requested_sample_count = requested_sample_count;
    result->timeout_ms = timeout_ms;
    result->trigger_time_us = trigger_time_us;
    result->trigger_seen = trigger_seen;
    result->timeout = timed_out;
    return ESP_OK;
}

static esp_err_t create_dclk_pcnt(pcnt_unit_handle_t *unit_out, pcnt_channel_handle_t *channel_out)
{
    const int pcnt_low_limit = -1;
    const int pcnt_high_limit = 30000;

    pcnt_unit_config_t unit_config = {
        .low_limit = pcnt_low_limit,
        .high_limit = pcnt_high_limit,
        .flags = {
            .accum_count = true,
        },
    };

    pcnt_unit_handle_t unit = NULL;
    esp_err_t err = pcnt_new_unit(&unit_config, &unit);
    if (err != ESP_OK) {
        return err;
    }

    pcnt_channel_handle_t channel = NULL;
    pcnt_chan_config_t channel_config = {
        .edge_gpio_num = GPIO_NUM_22,
        .level_gpio_num = -1,
    };
    err = pcnt_new_channel(unit, &channel_config, &channel);
    if (err != ESP_OK) {
        pcnt_del_unit(unit);
        return err;
    }

    err = gpio_set_pull_mode(GPIO_NUM_22, GPIO_FLOATING);
    if (err != ESP_OK) {
        pcnt_del_channel(channel);
        pcnt_del_unit(unit);
        return err;
    }

    err = pcnt_channel_set_edge_action(channel,
                                       PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                       PCNT_CHANNEL_EDGE_ACTION_HOLD);
    if (err != ESP_OK) {
        pcnt_del_channel(channel);
        pcnt_del_unit(unit);
        return err;
    }

    err = pcnt_channel_set_level_action(channel,
                                        PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                        PCNT_CHANNEL_LEVEL_ACTION_KEEP);
    if (err != ESP_OK) {
        pcnt_del_channel(channel);
        pcnt_del_unit(unit);
        return err;
    }

    err = pcnt_unit_add_watch_point(unit, pcnt_high_limit);
    if (err != ESP_OK) {
        pcnt_del_channel(channel);
        pcnt_del_unit(unit);
        return err;
    }

    *unit_out = unit;
    *channel_out = channel;
    return ESP_OK;
}

esp_err_t timing_analysis_capture_line_clocks(int marker_gpio,
                                              bool marker_falling_edge,
                                              uint32_t requested_line_count,
                                              uint32_t timeout_ms,
                                              line_clock_capture_result_t *result)
{
    if (result == NULL ||
        (marker_gpio != GPIO_NUM_19 && marker_gpio != GPIO_NUM_21) ||
        requested_line_count == 0 ||
        requested_line_count > LINE_CLOCK_CAPTURE_MAX_SAMPLES ||
        timeout_ms == 0 ||
        timeout_ms > 5000) {
        return ESP_ERR_INVALID_ARG;
    }

    const gpio_num_t input_gpios[] = {
        GPIO_NUM_19, // SPL marker candidate
        GPIO_NUM_21, // LP marker candidate
        GPIO_NUM_22, // DCLK counted by PCNT
        GPIO_NUM_33, // SPS frame sync
    };
    for (size_t i = 0; i < sizeof(input_gpios) / sizeof(input_gpios[0]); ++i) {
        esp_err_t err = configure_red_gpio(input_gpios[i]);
        if (err != ESP_OK) {
            return err;
        }
    }

    pcnt_unit_handle_t unit = NULL;
    pcnt_channel_handle_t channel = NULL;
    esp_err_t err = create_dclk_pcnt(&unit, &channel);
    if (err != ESP_OK) {
        return err;
    }

    err = pcnt_unit_enable(unit);
    if (err != ESP_OK) {
        pcnt_del_channel(channel);
        pcnt_del_unit(unit);
        return err;
    }

    err = pcnt_unit_clear_count(unit);
    if (err != ESP_OK) {
        pcnt_unit_disable(unit);
        pcnt_del_channel(channel);
        pcnt_del_unit(unit);
        return err;
    }

    err = pcnt_unit_start(unit);
    if (err != ESP_OK) {
        pcnt_unit_disable(unit);
        pcnt_del_channel(channel);
        pcnt_del_unit(unit);
        return err;
    }

    int64_t command_start_us = esp_timer_get_time();
    int64_t deadline_us = command_start_us + ((int64_t)timeout_ms * 1000);
    bool frame_sync_seen = wait_for_rising_edge(GPIO_NUM_33, deadline_us);

    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        pcnt_unit_stop(unit);
        pcnt_unit_disable(unit);
        pcnt_del_channel(channel);
        pcnt_del_unit(unit);
        return err;
    }

    gpio_intr_disable((gpio_num_t)marker_gpio);
    gpio_isr_handler_remove((gpio_num_t)marker_gpio);

    gpio_config_t marker_config = {
        .pin_bit_mask = 1ULL << marker_gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    err = gpio_config(&marker_config);
    if (err != ESP_OK) {
        pcnt_unit_stop(unit);
        pcnt_unit_disable(unit);
        pcnt_del_channel(channel);
        pcnt_del_unit(unit);
        return err;
    }

    int initial_count = 0;
    pcnt_unit_get_count(unit, &initial_count);
    portENTER_CRITICAL(&s_line_clock_lock);
    s_line_clock_active = true;
    s_line_clock_falling_edge = marker_falling_edge;
    s_line_clock_sample_count = 0;
    s_line_clock_overflow_count = 0;
    s_line_clock_start_us = esp_timer_get_time();
    s_line_clock_previous_count = initial_count;
    s_line_clock_pcnt_unit = unit;
    portEXIT_CRITICAL(&s_line_clock_lock);

    err = gpio_isr_handler_add((gpio_num_t)marker_gpio, line_clock_marker_isr, (void *)(intptr_t)marker_gpio);
    if (err != ESP_OK) {
        portENTER_CRITICAL(&s_line_clock_lock);
        s_line_clock_active = false;
        s_line_clock_pcnt_unit = NULL;
        portEXIT_CRITICAL(&s_line_clock_lock);
        pcnt_unit_stop(unit);
        pcnt_unit_disable(unit);
        pcnt_del_channel(channel);
        pcnt_del_unit(unit);
        return err;
    }
    gpio_intr_enable((gpio_num_t)marker_gpio);

    bool timed_out = false;
    while (true) {
        int64_t now_us = esp_timer_get_time();
        portENTER_CRITICAL(&s_line_clock_lock);
        size_t sample_count_snapshot = s_line_clock_sample_count;
        portEXIT_CRITICAL(&s_line_clock_lock);
        if (sample_count_snapshot >= requested_line_count) {
            break;
        }
        if (now_us >= deadline_us) {
            timed_out = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    gpio_intr_disable((gpio_num_t)marker_gpio);
    gpio_isr_handler_remove((gpio_num_t)marker_gpio);

    portENTER_CRITICAL(&s_line_clock_lock);
    s_line_clock_active = false;
    s_line_clock_pcnt_unit = NULL;
    size_t sample_count = s_line_clock_sample_count;
    portEXIT_CRITICAL(&s_line_clock_lock);

    int32_t min_delta = INT32_MAX;
    int32_t max_delta = INT32_MIN;
    int64_t sum_delta = 0;
    for (size_t i = 0; i < sample_count; ++i) {
        int32_t delta = s_line_clock_samples[i].dclk_delta;
        if (delta < min_delta) {
            min_delta = delta;
        }
        if (delta > max_delta) {
            max_delta = delta;
        }
        sum_delta += delta;
    }

    esp_err_t stop_err = pcnt_unit_stop(unit);
    esp_err_t disable_err = pcnt_unit_disable(unit);
    esp_err_t del_channel_err = pcnt_del_channel(channel);
    esp_err_t del_unit_err = pcnt_del_unit(unit);

    if (err != ESP_OK) {
        return err;
    }
    if (stop_err != ESP_OK) {
        return stop_err;
    }
    if (disable_err != ESP_OK) {
        return disable_err;
    }
    if (del_channel_err != ESP_OK) {
        return del_channel_err;
    }
    if (del_unit_err != ESP_OK) {
        return del_unit_err;
    }

    result->samples = s_line_clock_samples;
    result->sample_count = sample_count;
    result->requested_line_count = requested_line_count;
    result->timeout_ms = timeout_ms;
    result->marker_gpio = marker_gpio;
    result->marker_falling_edge = marker_falling_edge;
    result->frame_sync_seen = frame_sync_seen;
    result->timeout = timed_out;
    result->min_delta = sample_count > 0 ? min_delta : 0;
    result->max_delta = sample_count > 0 ? max_delta : 0;
    result->mean_delta = sample_count > 0 ? (double)sum_delta / (double)sample_count : 0.0;
    return ESP_OK;
}

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
                                                 rg_line_burst_capture_result_t *result)
{
    if (result == NULL ||
        width == 0 ||
        width > RG_LINE_BURST_MAX_WIDTH ||
        height == 0 ||
        height > RG_LINE_BURST_MAX_HEIGHT ||
        timeout_ms == 0 ||
        timeout_ms > 5000 ||
        (marker_gpio != GPIO_NUM_19 && marker_gpio != GPIO_NUM_21) ||
        skip_markers > 32 ||
        dclk_delay_edges > 64 ||
        marker_stride == 0 ||
        marker_stride > 16 ||
        marker_phase >= marker_stride) {
        return ESP_ERR_INVALID_ARG;
    }

    const gpio_num_t input_gpios[] = {
        GPIO_NUM_7,
        GPIO_NUM_8,
        GPIO_NUM_9,
        GPIO_NUM_10,
        GPIO_NUM_13,
        GPIO_NUM_14,
        GPIO_NUM_15,
        GPIO_NUM_16,
        GPIO_NUM_19, // SPL line start
        GPIO_NUM_21, // LP line marker candidate
        GPIO_NUM_22, // DCLK sample clock
        GPIO_NUM_33, // SPS frame sync
    };
    for (size_t i = 0; i < sizeof(input_gpios) / sizeof(input_gpios[0]); ++i) {
        esp_err_t err = configure_red_gpio(input_gpios[i]);
        if (err != ESP_OK) {
            return err;
        }
    }

    memset(s_rg_line_burst_pixels, 0, sizeof(s_rg_line_burst_pixels));
    memset(s_rg_line_burst_line_counts, 0, sizeof(s_rg_line_burst_line_counts));

    int64_t command_start_us = esp_timer_get_time();
    int64_t deadline_us = command_start_us + ((int64_t)timeout_ms * 1000);
    bool frame_sync_seen = wait_for_rising_edge(GPIO_NUM_33, deadline_us);
    bool timed_out = !frame_sync_seen;
    uint32_t captured_lines = 0;

    for (uint32_t skipped = 0; skipped < skip_markers && !timed_out; ++skipped) {
        bool skipped_marker_seen = wait_for_falling_edge((gpio_num_t)marker_gpio, deadline_us);
        if (!skipped_marker_seen) {
            timed_out = true;
            break;
        }
    }

    uint32_t marker_index = 0;
    uint32_t observed_markers = 0;
    bool next_frame_seen = false;
    while (captured_lines < height && !timed_out) {
        bool line_start_seen = stop_on_next_frame
                                   ? wait_for_marker_falling_or_sps_rising((gpio_num_t)marker_gpio,
                                                                           deadline_us,
                                                                           &next_frame_seen)
                                   : wait_for_falling_edge((gpio_num_t)marker_gpio, deadline_us);
        if (!line_start_seen) {
            timed_out = !next_frame_seen;
            break;
        }
        observed_markers++;
        bool capture_this_marker = (marker_index % marker_stride) == marker_phase;
        marker_index++;
        if (!capture_this_marker) {
            continue;
        }

        uint32_t samples = 0;
        uint32_t gpio_in = GPIO.in.val;
        int previous_dclk = (gpio_in >> 22) & 0x1;
        uint32_t skipped_dclk_edges = 0;
        uint32_t loop_budget = 12000;
        while ((skipped_dclk_edges < dclk_delay_edges || samples < width) && loop_budget > 0) {
            gpio_in = GPIO.in.val;
            int dclk = (gpio_in >> 22) & 0x1;
            bool sample_edge_seen = sample_falling_edge ? (previous_dclk == 1 && dclk == 0)
                                                        : (previous_dclk == 0 && dclk == 1);
            if (sample_edge_seen) {
                if (skipped_dclk_edges < dclk_delay_edges) {
                    skipped_dclk_edges++;
                } else {
                    s_rg_line_burst_pixels[(captured_lines * width) + samples] = sample_rg44_from_gpio_in(gpio_in);
                    samples++;
                }
            }
            previous_dclk = dclk;
            loop_budget--;
        }

        s_rg_line_burst_line_counts[captured_lines] = (uint16_t)samples;
        captured_lines++;
    }

    uint32_t checksum = 0;
    uint32_t transition_count = 0;
    uint32_t min_value = 255;
    uint32_t max_value = 0;
    uint32_t pixel_count = width * height;
    for (uint32_t i = 0; i < pixel_count; ++i) {
        uint8_t value = s_rg_line_burst_pixels[i];
        checksum += value;
        if (value < min_value) {
            min_value = value;
        }
        if (value > max_value) {
            max_value = value;
        }
        if (i > 0 && value != s_rg_line_burst_pixels[i - 1]) {
            transition_count++;
        }
    }

    result->pixels = s_rg_line_burst_pixels;
    result->line_sample_counts = s_rg_line_burst_line_counts;
    result->width = width;
    result->height = height;
    result->captured_lines = captured_lines;
    result->timeout_ms = timeout_ms;
    result->frame_sync_seen = frame_sync_seen;
    result->timeout = timed_out;
    result->checksum = checksum;
    result->transition_count = transition_count;
    result->min_value = min_value;
    result->max_value = max_value;
    result->sample_falling_edge = sample_falling_edge;
    result->marker_gpio = marker_gpio;
    result->skipped_markers = skip_markers;
    result->dclk_delay_edges = dclk_delay_edges;
    result->marker_stride = marker_stride;
    result->marker_phase = marker_phase;
    result->stop_on_next_frame = stop_on_next_frame;
    result->next_frame_seen = next_frame_seen;
    result->observed_markers = observed_markers;
    return ESP_OK;
}

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
                                                     rgb666_line_burst_capture_result_t *result)
{
    if (result == NULL ||
        width == 0 ||
        width > RG_LINE_BURST_MAX_WIDTH ||
        height == 0 ||
        height > RG_LINE_BURST_MAX_HEIGHT ||
        timeout_ms == 0 ||
        timeout_ms > 5000 ||
        (marker_gpio != GPIO_NUM_19 && marker_gpio != GPIO_NUM_21) ||
        skip_markers > 32 ||
        dclk_delay_edges > 64 ||
        marker_stride == 0 ||
        marker_stride > 16 ||
        marker_phase >= marker_stride) {
        return ESP_ERR_INVALID_ARG;
    }

    const gpio_num_t input_gpios[] = {
        GPIO_NUM_7, GPIO_NUM_8, GPIO_NUM_9, GPIO_NUM_10, GPIO_NUM_11, GPIO_NUM_12,
        GPIO_NUM_13, GPIO_NUM_14, GPIO_NUM_15, GPIO_NUM_16, GPIO_NUM_17, GPIO_NUM_18,
        GPIO_NUM_19, GPIO_NUM_21, GPIO_NUM_22, GPIO_NUM_33,
        GPIO_NUM_36, GPIO_NUM_45, GPIO_NUM_46, GPIO_NUM_47, GPIO_NUM_48, GPIO_NUM_50,
    };
    for (size_t i = 0; i < sizeof(input_gpios) / sizeof(input_gpios[0]); ++i) {
        esp_err_t err = configure_red_gpio(input_gpios[i]);
        if (err != ESP_OK) {
            return err;
        }
    }

    memset(s_rgb666_line_burst_pixels, 0, sizeof(s_rgb666_line_burst_pixels));
    memset(s_rg_line_burst_line_counts, 0, sizeof(s_rg_line_burst_line_counts));

    int64_t command_start_us = esp_timer_get_time();
    int64_t deadline_us = command_start_us + ((int64_t)timeout_ms * 1000);
    bool frame_sync_seen = wait_for_rising_edge(GPIO_NUM_33, deadline_us);
    bool timed_out = !frame_sync_seen;
    uint32_t captured_lines = 0;

    for (uint32_t skipped = 0; skipped < skip_markers && !timed_out; ++skipped) {
        bool skipped_marker_seen = wait_for_falling_edge((gpio_num_t)marker_gpio, deadline_us);
        if (!skipped_marker_seen) {
            timed_out = true;
            break;
        }
    }

    uint32_t marker_index = 0;
    uint32_t observed_markers = 0;
    bool next_frame_seen = false;
    while (captured_lines < height && !timed_out) {
        bool line_start_seen = stop_on_next_frame
                                   ? wait_for_marker_falling_or_sps_rising((gpio_num_t)marker_gpio,
                                                                           deadline_us,
                                                                           &next_frame_seen)
                                   : wait_for_falling_edge((gpio_num_t)marker_gpio, deadline_us);
        if (!line_start_seen) {
            timed_out = !next_frame_seen;
            break;
        }
        observed_markers++;
        bool capture_this_marker = (marker_index % marker_stride) == marker_phase;
        marker_index++;
        if (!capture_this_marker) {
            continue;
        }

        uint32_t samples = 0;
        uint32_t gpio_in = GPIO.in.val;
        int previous_dclk = (gpio_in >> 22) & 0x1;
        uint32_t skipped_dclk_edges = 0;
        uint32_t loop_budget = 16000;
        while ((skipped_dclk_edges < dclk_delay_edges || samples < width) && loop_budget > 0) {
            gpio_in = GPIO.in.val;
            int dclk = (gpio_in >> 22) & 0x1;
            bool sample_edge_seen = sample_falling_edge ? (previous_dclk == 1 && dclk == 0)
                                                        : (previous_dclk == 0 && dclk == 1);
            if (sample_edge_seen) {
                if (skipped_dclk_edges < dclk_delay_edges) {
                    skipped_dclk_edges++;
                } else {
                    uint32_t gpio_in1 = GPIO.in1.val;
                    uint32_t offset = ((captured_lines * width) + samples) * RGB666_LINE_BURST_BYTES_PER_PIXEL;
                    sample_rgb666_from_gpio_in(gpio_in, gpio_in1, &s_rgb666_line_burst_pixels[offset]);
                    samples++;
                }
            }
            previous_dclk = dclk;
            loop_budget--;
        }

        s_rg_line_burst_line_counts[captured_lines] = (uint16_t)samples;
        captured_lines++;
    }

    uint32_t checksum = 0;
    uint32_t transition_count = 0;
    uint32_t min_value = 255;
    uint32_t max_value = 0;
    uint32_t byte_count = width * height * RGB666_LINE_BURST_BYTES_PER_PIXEL;
    for (uint32_t i = 0; i < byte_count; ++i) {
        uint8_t value = s_rgb666_line_burst_pixels[i];
        checksum += value;
        if (value < min_value) {
            min_value = value;
        }
        if (value > max_value) {
            max_value = value;
        }
        if (i > 0 && value != s_rgb666_line_burst_pixels[i - 1]) {
            transition_count++;
        }
    }

    result->pixels_rgb666 = s_rgb666_line_burst_pixels;
    result->line_sample_counts = s_rg_line_burst_line_counts;
    result->width = width;
    result->height = height;
    result->captured_lines = captured_lines;
    result->timeout_ms = timeout_ms;
    result->frame_sync_seen = frame_sync_seen;
    result->timeout = timed_out;
    result->checksum = checksum;
    result->transition_count = transition_count;
    result->min_value = min_value;
    result->max_value = max_value;
    result->sample_falling_edge = sample_falling_edge;
    result->marker_gpio = marker_gpio;
    result->skipped_markers = skip_markers;
    result->dclk_delay_edges = dclk_delay_edges;
    result->marker_stride = marker_stride;
    result->marker_phase = marker_phase;
    result->stop_on_next_frame = stop_on_next_frame;
    result->next_frame_seen = next_frame_seen;
    result->observed_markers = observed_markers;
    return ESP_OK;
}

void timing_analysis_placeholder(void)
{
    ESP_LOGD(TAG, "timing analysis is not enabled during Phase 1");
}
