#pragma once

#include "esp_err.h"

void destination_gpio_lab_release_all(void);
void destination_gpio_lab_print_status(void);
void destination_gpio_lab_handle_validate(const char *line);
void destination_gpio_lab_handle_claim(const char *line);
void destination_gpio_lab_handle_set(const char *line);
void destination_gpio_lab_handle_pulse(const char *line);
void destination_gpio_lab_handle_release(const char *line);

