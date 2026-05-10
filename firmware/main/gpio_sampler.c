#include "gpio_sampler.h"

#include <stdbool.h>

#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pinmap_gbc.h"

static const char *TAG = "gpio_sampler";

static const gpio_num_t PHASE1_TEST_GPIO_ALLOWLIST[] = {
    GPIO_NUM_7,
    GPIO_NUM_8,
    GPIO_NUM_9,
    GPIO_NUM_10,
    GPIO_NUM_11,
    GPIO_NUM_12,
    GPIO_NUM_13,
    GPIO_NUM_14,
    GPIO_NUM_15,
    GPIO_NUM_16,
    GPIO_NUM_17,
    GPIO_NUM_18,
    GPIO_NUM_19,
    GPIO_NUM_20,
    GPIO_NUM_21,
    GPIO_NUM_22,
    GPIO_NUM_3,
    GPIO_NUM_33,
    GPIO_NUM_36,
    GPIO_NUM_45,
    GPIO_NUM_46,
    GPIO_NUM_47,
    GPIO_NUM_48,
    GPIO_NUM_50,
};

static portMUX_TYPE s_edge_count_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t s_rising_edges;
static volatile uint32_t s_falling_edges;
static volatile int s_edge_count_gpio = -1;

static void IRAM_ATTR gpio_edge_count_isr(void *arg)
{
    int gpio_num = (int)(intptr_t)arg;
    if (gpio_num != s_edge_count_gpio) {
        return;
    }

    int level = gpio_get_level((gpio_num_t)gpio_num);
    portENTER_CRITICAL_ISR(&s_edge_count_lock);
    if (level) {
        s_rising_edges++;
    } else {
        s_falling_edges++;
    }
    portEXIT_CRITICAL_ISR(&s_edge_count_lock);
}

esp_err_t gpio_sampler_init_phase1_inputs_only(void)
{
    if (GBC_CAPTURE_PIN_COUNT == 0) {
        ESP_LOGW(TAG, "no capture GPIOs assigned; Phase 1 requires documented electrical measurements first");
        return ESP_OK;
    }

#if GBC_CAPTURE_PIN_COUNT > 0
    for (size_t i = 0; i < GBC_CAPTURE_PIN_COUNT; ++i) {
        gpio_config_t config = {
            .pin_bit_mask = 1ULL << GBC_CAPTURE_PINS[i].gpio,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };

        esp_err_t err = gpio_config(&config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "failed to configure %s on GPIO %d as input: %s",
                     GBC_CAPTURE_PINS[i].name,
                     GBC_CAPTURE_PINS[i].gpio,
                     esp_err_to_name(err));
            return err;
        }

        ESP_LOGI(TAG, "configured %s on GPIO %d as input-only",
                 GBC_CAPTURE_PINS[i].name,
                 GBC_CAPTURE_PINS[i].gpio);
    }
#endif

    return ESP_OK;
}

bool gpio_sampler_is_test_gpio_allowed(int gpio_num)
{
    for (size_t i = 0; i < sizeof(PHASE1_TEST_GPIO_ALLOWLIST) / sizeof(PHASE1_TEST_GPIO_ALLOWLIST[0]); ++i) {
        if (gpio_num == PHASE1_TEST_GPIO_ALLOWLIST[i]) {
            return true;
        }
    }

    return false;
}

esp_err_t gpio_sampler_read_test_gpio(int gpio_num, int *level)
{
    if (level == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!gpio_sampler_is_test_gpio_allowed(gpio_num)) {
        return ESP_ERR_NOT_ALLOWED;
    }

    gpio_config_t config = {
        .pin_bit_mask = 1ULL << gpio_num,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        return err;
    }

    *level = gpio_get_level((gpio_num_t)gpio_num);
    return ESP_OK;
}

esp_err_t gpio_sampler_count_test_gpio_edges(int gpio_num, uint32_t duration_ms, uint32_t *rising_edges, uint32_t *falling_edges)
{
    if (rising_edges == NULL || falling_edges == NULL || duration_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!gpio_sampler_is_test_gpio_allowed(gpio_num)) {
        return ESP_ERR_NOT_ALLOWED;
    }

    gpio_config_t config = {
        .pin_bit_mask = 1ULL << gpio_num,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };

    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    gpio_isr_handler_remove((gpio_num_t)gpio_num);

    portENTER_CRITICAL(&s_edge_count_lock);
    s_rising_edges = 0;
    s_falling_edges = 0;
    s_edge_count_gpio = gpio_num;
    portEXIT_CRITICAL(&s_edge_count_lock);

    err = gpio_isr_handler_add((gpio_num_t)gpio_num, gpio_edge_count_isr, (void *)(intptr_t)gpio_num);
    if (err != ESP_OK) {
        s_edge_count_gpio = -1;
        return err;
    }

    gpio_intr_enable((gpio_num_t)gpio_num);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    gpio_intr_disable((gpio_num_t)gpio_num);
    gpio_isr_handler_remove((gpio_num_t)gpio_num);

    portENTER_CRITICAL(&s_edge_count_lock);
    *rising_edges = s_rising_edges;
    *falling_edges = s_falling_edges;
    s_edge_count_gpio = -1;
    portEXIT_CRITICAL(&s_edge_count_lock);

    return ESP_OK;
}

esp_err_t gpio_sampler_measure_clock_pcnt(int gpio_num, uint32_t duration_ms, int *edge_count)
{
    if (edge_count == NULL || duration_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!gpio_sampler_is_test_gpio_allowed(gpio_num)) {
        return ESP_ERR_NOT_ALLOWED;
    }

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
        .edge_gpio_num = gpio_num,
        .level_gpio_num = -1,
    };
    err = pcnt_new_channel(unit, &channel_config, &channel);
    if (err != ESP_OK) {
        pcnt_del_unit(unit);
        return err;
    }

    err = gpio_set_pull_mode((gpio_num_t)gpio_num, GPIO_FLOATING);
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

    vTaskDelay(pdMS_TO_TICKS(duration_ms));

    esp_err_t stop_err = pcnt_unit_stop(unit);
    int count = 0;
    esp_err_t count_err = pcnt_unit_get_count(unit, &count);
    esp_err_t disable_err = pcnt_unit_disable(unit);
    esp_err_t del_channel_err = pcnt_del_channel(channel);
    esp_err_t del_unit_err = pcnt_del_unit(unit);

    if (stop_err != ESP_OK) {
        return stop_err;
    }
    if (count_err != ESP_OK) {
        return count_err;
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

    *edge_count = count;
    return ESP_OK;
}
