#pragma once

#include <stddef.h>

#include "driver/gpio.h"

typedef struct {
    const char *name;
    gpio_num_t gpio;
} gbc_capture_pin_t;

/*
 * Phase 1 safety rule:
 * No Game Boy Color LCD bus pins are assigned until voltage levels and signal
 * roles are measured and documented. Dangerous analog LCD rails must never be
 * added to this table.
 */
static const gbc_capture_pin_t GBC_CAPTURE_PINS[] = {
    {"UNASSIGNED", GPIO_NUM_NC},
};

#define GBC_CAPTURE_PIN_COUNT 0U
