#include "destination_gpio_lab.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pinmap_current.h"

#define DEST_GPIO_MAX_CLAIMS 12
#define DEST_GPIO_SIGNAL_LEN 31
#define DEST_GPIO_MAX_PULSE_MS 5000

typedef struct {
    const char *signal;
    gpio_num_t gpio;
} source_gpio_owner_t;

typedef struct {
    bool active;
    char signal[DEST_GPIO_SIGNAL_LEN + 1];
    gpio_num_t gpio;
    int level;
} destination_gpio_claim_t;

/*
 * Mirrors the current GBC LCD source lab profile. This intentionally blocks
 * destination output claims from touching source capture pins until source and
 * destination firmware pin tables are generated from profiles.
 */
static const source_gpio_owner_t ACTIVE_SOURCE_GPIOS[] = {
    {"CLS", GBC_LCD_GPIO_CLS},
    {"G5", GBC_LCD_GPIO_G5},
    {"G4", GBC_LCD_GPIO_G4},
    {"G3", GBC_LCD_GPIO_G3},
    {"G2", GBC_LCD_GPIO_G2},
    {"G1", GBC_LCD_GPIO_G1},
    {"G0", GBC_LCD_GPIO_G0},
    {"R5", GBC_LCD_GPIO_R5},
    {"R4", GBC_LCD_GPIO_R4},
    {"R3", GBC_LCD_GPIO_R3},
    {"R2", GBC_LCD_GPIO_R2},
    {"R1", GBC_LCD_GPIO_R1},
    {"R0", GBC_LCD_GPIO_R0},
    {"SPL", GBC_LCD_GPIO_SPL},
    {"PS", GBC_LCD_GPIO_PS},
    {"LP", GBC_LCD_GPIO_LP},
    {"DCLK", GBC_LCD_GPIO_DCLK},
    {"SPS", GBC_LCD_GPIO_SPS},
    {"B0", GBC_LCD_GPIO_B0},
    {"B1", GBC_LCD_GPIO_B1},
    {"B2", GBC_LCD_GPIO_B2},
    {"B3", GBC_LCD_GPIO_B3},
    {"B4", GBC_LCD_GPIO_B4},
    {"B5", GBC_LCD_GPIO_B5},
};

static destination_gpio_claim_t s_claims[DEST_GPIO_MAX_CLAIMS];

static bool signal_is_valid(const char *signal)
{
    if (signal == NULL || signal[0] == '\0') {
        return false;
    }
    for (size_t i = 0; signal[i] != '\0'; ++i) {
        const unsigned char ch = (unsigned char)signal[i];
        if (!(isalnum(ch) || ch == '_' || ch == '-' || ch == '/')) {
            return false;
        }
    }
    return true;
}

static const char *source_owner_for_gpio(gpio_num_t gpio)
{
    for (size_t i = 0; i < sizeof(ACTIVE_SOURCE_GPIOS) / sizeof(ACTIVE_SOURCE_GPIOS[0]); ++i) {
        if (ACTIVE_SOURCE_GPIOS[i].gpio == gpio) {
            return ACTIVE_SOURCE_GPIOS[i].signal;
        }
    }
    return NULL;
}

static int claim_index_for_signal(const char *signal)
{
    for (size_t i = 0; i < DEST_GPIO_MAX_CLAIMS; ++i) {
        if (s_claims[i].active && strcmp(s_claims[i].signal, signal) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int claim_index_for_gpio(gpio_num_t gpio)
{
    for (size_t i = 0; i < DEST_GPIO_MAX_CLAIMS; ++i) {
        if (s_claims[i].active && s_claims[i].gpio == gpio) {
            return (int)i;
        }
    }
    return -1;
}

static int free_claim_index(void)
{
    for (size_t i = 0; i < DEST_GPIO_MAX_CLAIMS; ++i) {
        if (!s_claims[i].active) {
            return (int)i;
        }
    }
    return -1;
}

static int inactive_level_for_signal(const char *signal)
{
    if (strcmp(signal, "CS") == 0 || strcmp(signal, "RESET") == 0) {
        return 1;
    }
    return 0;
}

static esp_err_t release_gpio(gpio_num_t gpio)
{
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        return err;
    }
    err = gpio_set_pull_mode(gpio, GPIO_FLOATING);
    if (err != ESP_OK) {
        return err;
    }
    return gpio_set_direction(gpio, GPIO_MODE_DISABLE);
}

static esp_err_t configure_output(gpio_num_t gpio, int level)
{
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        return err;
    }
    return gpio_set_level(gpio, level ? 1 : 0);
}

static const char *validate_gpio_claim(const char *signal, int gpio_int, const char **owner)
{
    if (!signal_is_valid(signal)) {
        return "invalid_signal";
    }
    if (gpio_int < 0 || gpio_int >= GPIO_NUM_MAX) {
        return "invalid_gpio_range";
    }
    gpio_num_t gpio = (gpio_num_t)gpio_int;
    if (!GPIO_IS_VALID_GPIO(gpio)) {
        return "invalid_gpio";
    }
    if (!GPIO_IS_VALID_OUTPUT_GPIO(gpio)) {
        return "not_output_capable";
    }
    const char *source_owner = source_owner_for_gpio(gpio);
    if (source_owner != NULL) {
        if (owner != NULL) {
            *owner = source_owner;
        }
        return "gpio_owned_by_source";
    }
    int gpio_claim = claim_index_for_gpio(gpio);
    int signal_claim = claim_index_for_signal(signal);
    if (gpio_claim >= 0 && gpio_claim != signal_claim) {
        if (owner != NULL) {
            *owner = s_claims[gpio_claim].signal;
        }
        return "gpio_already_claimed";
    }
    return NULL;
}

void destination_gpio_lab_release_all(void)
{
    for (size_t i = 0; i < DEST_GPIO_MAX_CLAIMS; ++i) {
        if (s_claims[i].active) {
            (void)release_gpio(s_claims[i].gpio);
            s_claims[i].active = false;
        }
    }
}

void destination_gpio_lab_print_status(void)
{
    printf("{\"ok\":true,\"command\":\"DEST_GPIO_STATUS\",\"claims\":[");
    bool first = true;
    for (size_t i = 0; i < DEST_GPIO_MAX_CLAIMS; ++i) {
        if (!s_claims[i].active) {
            continue;
        }
        if (!first) {
            printf(",");
        }
        first = false;
        printf("{\"signal\":\"%s\",\"gpio\":%d,\"level\":%d}",
               s_claims[i].signal,
               (int)s_claims[i].gpio,
               s_claims[i].level);
    }
    printf("],\"max_claims\":%d}\n", DEST_GPIO_MAX_CLAIMS);
}

void destination_gpio_lab_handle_validate(const char *line)
{
    char signal[DEST_GPIO_SIGNAL_LEN + 1] = {0};
    int gpio_int = -1;
    if (sscanf(line, "DEST_GPIO_VALIDATE %31s %d", signal, &gpio_int) != 2) {
        printf("{\"ok\":false,\"command\":\"DEST_GPIO_VALIDATE\",\"error\":\"usage\",\"usage\":\"DEST_GPIO_VALIDATE <signal> <gpio>\"}\n");
        return;
    }

    const char *owner = NULL;
    const char *error = validate_gpio_claim(signal, gpio_int, &owner);
    printf("{\"ok\":%s,\"command\":\"DEST_GPIO_VALIDATE\",\"signal\":\"%s\",\"gpio\":%d,\"error\":\"%s\"",
           error == NULL ? "true" : "false",
           signal,
           gpio_int,
           error == NULL ? "none" : error);
    if (owner != NULL) {
        printf(",\"owner\":\"%s\"", owner);
    }
    printf("}\n");
}

void destination_gpio_lab_handle_claim(const char *line)
{
    char signal[DEST_GPIO_SIGNAL_LEN + 1] = {0};
    int gpio_int = -1;
    if (sscanf(line, "DEST_GPIO_CLAIM %31s %d", signal, &gpio_int) != 2) {
        printf("{\"ok\":false,\"command\":\"DEST_GPIO_CLAIM\",\"error\":\"usage\",\"usage\":\"DEST_GPIO_CLAIM <signal> <gpio>\"}\n");
        return;
    }

    const char *owner = NULL;
    const char *error = validate_gpio_claim(signal, gpio_int, &owner);
    if (error != NULL) {
        printf("{\"ok\":false,\"command\":\"DEST_GPIO_CLAIM\",\"signal\":\"%s\",\"gpio\":%d,\"error\":\"%s\"",
               signal,
               gpio_int,
               error);
        if (owner != NULL) {
            printf(",\"owner\":\"%s\"", owner);
        }
        printf("}\n");
        return;
    }

    int index = claim_index_for_signal(signal);
    if (index < 0) {
        index = free_claim_index();
    } else {
        (void)release_gpio(s_claims[index].gpio);
    }
    if (index < 0) {
        printf("{\"ok\":false,\"command\":\"DEST_GPIO_CLAIM\",\"signal\":\"%s\",\"gpio\":%d,\"error\":\"claim_table_full\"}\n",
               signal,
               gpio_int);
        return;
    }

    const int level = inactive_level_for_signal(signal);
    esp_err_t err = configure_output((gpio_num_t)gpio_int, level);
    if (err != ESP_OK) {
        printf("{\"ok\":false,\"command\":\"DEST_GPIO_CLAIM\",\"signal\":\"%s\",\"gpio\":%d,\"error\":\"%s\",\"err\":%d}\n",
               signal,
               gpio_int,
               esp_err_to_name(err),
               err);
        return;
    }

    s_claims[index].active = true;
    s_claims[index].gpio = (gpio_num_t)gpio_int;
    s_claims[index].level = level;
    strlcpy(s_claims[index].signal, signal, sizeof(s_claims[index].signal));

    printf("{\"ok\":true,\"command\":\"DEST_GPIO_CLAIM\",\"signal\":\"%s\",\"gpio\":%d,\"level\":%d,\"mode\":\"output_no_pulls\"}\n",
           signal,
           gpio_int,
           level);
}

void destination_gpio_lab_handle_set(const char *line)
{
    char signal[DEST_GPIO_SIGNAL_LEN + 1] = {0};
    int level = -1;
    if (sscanf(line, "DEST_GPIO_SET %31s %d", signal, &level) != 2 || (level != 0 && level != 1)) {
        printf("{\"ok\":false,\"command\":\"DEST_GPIO_SET\",\"error\":\"usage\",\"usage\":\"DEST_GPIO_SET <signal> <0|1>\"}\n");
        return;
    }

    int index = claim_index_for_signal(signal);
    if (index < 0) {
        printf("{\"ok\":false,\"command\":\"DEST_GPIO_SET\",\"signal\":\"%s\",\"error\":\"signal_not_claimed\"}\n", signal);
        return;
    }

    esp_err_t err = gpio_set_level(s_claims[index].gpio, level);
    if (err != ESP_OK) {
        printf("{\"ok\":false,\"command\":\"DEST_GPIO_SET\",\"signal\":\"%s\",\"gpio\":%d,\"error\":\"%s\",\"err\":%d}\n",
               signal,
               (int)s_claims[index].gpio,
               esp_err_to_name(err),
               err);
        return;
    }
    s_claims[index].level = level;
    printf("{\"ok\":true,\"command\":\"DEST_GPIO_SET\",\"signal\":\"%s\",\"gpio\":%d,\"level\":%d}\n",
           signal,
           (int)s_claims[index].gpio,
           level);
}

void destination_gpio_lab_handle_pulse(const char *line)
{
    char signal[DEST_GPIO_SIGNAL_LEN + 1] = {0};
    int level = -1;
    int duration_ms = 0;
    if (sscanf(line, "DEST_GPIO_PULSE %31s %d %d", signal, &level, &duration_ms) != 3 ||
        (level != 0 && level != 1) ||
        duration_ms < 1 ||
        duration_ms > DEST_GPIO_MAX_PULSE_MS) {
        printf("{\"ok\":false,\"command\":\"DEST_GPIO_PULSE\",\"error\":\"usage\",\"usage\":\"DEST_GPIO_PULSE <signal> <0|1> <duration_ms>\",\"max_duration_ms\":%d}\n",
               DEST_GPIO_MAX_PULSE_MS);
        return;
    }

    int index = claim_index_for_signal(signal);
    if (index < 0) {
        printf("{\"ok\":false,\"command\":\"DEST_GPIO_PULSE\",\"signal\":\"%s\",\"error\":\"signal_not_claimed\"}\n", signal);
        return;
    }

    const int previous_level = s_claims[index].level;
    esp_err_t err = gpio_set_level(s_claims[index].gpio, level);
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS((uint32_t)duration_ms));
        err = gpio_set_level(s_claims[index].gpio, previous_level);
    }
    if (err != ESP_OK) {
        printf("{\"ok\":false,\"command\":\"DEST_GPIO_PULSE\",\"signal\":\"%s\",\"gpio\":%d,\"error\":\"%s\",\"err\":%d}\n",
               signal,
               (int)s_claims[index].gpio,
               esp_err_to_name(err),
               err);
        return;
    }
    printf("{\"ok\":true,\"command\":\"DEST_GPIO_PULSE\",\"signal\":\"%s\",\"gpio\":%d,\"pulse_level\":%d,\"restored_level\":%d,\"duration_ms\":%d}\n",
           signal,
           (int)s_claims[index].gpio,
           level,
           previous_level,
           duration_ms);
}

void destination_gpio_lab_handle_release(const char *line)
{
    char signal[DEST_GPIO_SIGNAL_LEN + 1] = {0};
    if (sscanf(line, "DEST_GPIO_RELEASE %31s", signal) != 1) {
        printf("{\"ok\":false,\"command\":\"DEST_GPIO_RELEASE\",\"error\":\"usage\",\"usage\":\"DEST_GPIO_RELEASE <signal>|ALL\"}\n");
        return;
    }
    if (strcmp(signal, "ALL") == 0) {
        destination_gpio_lab_release_all();
        printf("{\"ok\":true,\"command\":\"DEST_GPIO_RELEASE\",\"signal\":\"ALL\"}\n");
        return;
    }

    int index = claim_index_for_signal(signal);
    if (index < 0) {
        printf("{\"ok\":false,\"command\":\"DEST_GPIO_RELEASE\",\"signal\":\"%s\",\"error\":\"signal_not_claimed\"}\n", signal);
        return;
    }

    gpio_num_t gpio = s_claims[index].gpio;
    esp_err_t err = release_gpio(gpio);
    s_claims[index].active = false;
    printf("{\"ok\":%s,\"command\":\"DEST_GPIO_RELEASE\",\"signal\":\"%s\",\"gpio\":%d,\"error\":\"%s\",\"err\":%d,\"mode\":\"disabled_no_pulls\"}\n",
           err == ESP_OK ? "true" : "false",
           signal,
           (int)gpio,
           err == ESP_OK ? "none" : esp_err_to_name(err),
           err);
}
