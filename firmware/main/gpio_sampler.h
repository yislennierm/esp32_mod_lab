#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t gpio_sampler_init_phase1_inputs_only(void);
bool gpio_sampler_is_test_gpio_allowed(int gpio_num);
esp_err_t gpio_sampler_read_test_gpio(int gpio_num, int *level);
esp_err_t gpio_sampler_count_test_gpio_edges(int gpio_num, uint32_t duration_ms, uint32_t *rising_edges, uint32_t *falling_edges);
esp_err_t gpio_sampler_measure_clock_pcnt(int gpio_num, uint32_t duration_ms, int *edge_count);
