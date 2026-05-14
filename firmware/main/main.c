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
#ifdef GBC_P4_SAFE_RECOVERY
    ESP_LOGW(TAG, "Safe recovery mode: no capture or destination runtime is started");
#elif defined(GBC_P4_PRODUCTION_MIRROR)
    ESP_LOGI(TAG, "Production mode: browser workbench and command server are disabled");
#elif defined(GBC_P4_TELEMETRY)
    ESP_LOGI(TAG, "Telemetry mode: command server active for selected runtime observation");
    ESP_LOGW(TAG, "Capture and destination activity should remain opt-in and profile-controlled");
#elif defined(GBC_P4_LAB)
    ESP_LOGI(TAG, "Lab mode: command server active for research, probing, capture, and validation");
    ESP_LOGW(TAG, "Startup remains input-safe until lab commands claim peripherals or outputs");
#else
    ESP_LOGW(TAG, "Legacy probe mode: no GPIO outputs are used for LCD capture at startup");
#endif
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
