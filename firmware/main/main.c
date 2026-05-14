#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_version.h"
#include "diagnostics.h"
#include "gpio_sampler.h"
#include "lcdcam_raw.h"
#include "pinmap_gbc.h"
#include "production_mirror.h"
#include "usb_protocol.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "%s %s starting", GBC_P4_PROBE_NAME, GBC_P4_PROBE_VERSION);
    ESP_LOGW(TAG, "Phase 1 electrical safety mode: no GPIO outputs are used for LCD capture");
    ESP_LOGW(TAG, "Do not connect GBC LCD rails or unverified lines to ESP32-P4 GPIO");

    diagnostics_init(GBC_CAPTURE_PIN_COUNT);

#ifdef GBC_P4_PRODUCTION_MIRROR
    production_mirror_start();
#else
    ESP_ERROR_CHECK(lcdcam_raw_enter_electrical_isolate());
    ESP_ERROR_CHECK(gpio_sampler_init_phase1_inputs_only());
    usb_protocol_start();
#endif

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
