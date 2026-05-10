#include "lcdcam_raw.h"

#include <string.h>
#include <sys/param.h>

#include "driver/gpio.h"
#include "esp_cache.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_private/esp_cache_private.h"
#include "esp_private/gdma.h"
#include "esp_private/esp_cam_dvp.h"
#include "esp_private/gpio.h"
#include "esp_rom_gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "hal/cam_ll.h"
#include "hal/dma_types.h"
#include "soc/cam_periph.h"
#include "soc/gpio_pins.h"
#include "soc/io_mux_reg.h"

#define LCDCAM_RAW_MAX_H_RES 320U
#define LCDCAM_RAW_MAX_V_RES 240U
#define LCDCAM_RAW_DMA_DESC_MAX_SIZE DMA_DESCRIPTOR_BUFFER_MAX_SIZE_4B_ALIGNED

typedef dma_descriptor_align8_t lcdcam_raw_dma_desc_t;

typedef struct {
    SemaphoreHandle_t done_sem;
    lcdcam_raw_dma_desc_t *desc;
    size_t desc_count;
    volatile bool eof_seen;
    volatile bool done_seen;
    volatile intptr_t eof_desc_addr;
    volatile size_t completed_descriptors;
} lcdcam_raw_context_t;

typedef struct {
    gdma_channel_handle_t dma_chan;
    SemaphoreHandle_t done_sem;
    lcd_cam_dev_t *hw;
    lcdcam_raw_dma_desc_t **descs;
    uint8_t **buffers;
    size_t desc_size;
    size_t buffer_len;
    uint32_t requested_chunks;
    volatile uint32_t completed_chunks;
    volatile uint32_t failed_rearms;
    volatile uint32_t current_slot;
    volatile int64_t last_chunk_started_us;
    int64_t *chunk_us;
    esp_err_t last_esp_err;
} lcdcam_raw_rearm_context_t;

static esp_err_t configure_input_gpio(int pin, int signal, bool invert)
{
    if (pin < 0) {
        return ESP_OK;
    }

    gpio_func_sel(pin, PIN_FUNC_GPIO);
    ESP_RETURN_ON_ERROR(gpio_set_direction(pin, GPIO_MODE_INPUT), "lcdcam_raw", "gpio direction");
    ESP_RETURN_ON_ERROR(gpio_set_pull_mode(pin, GPIO_FLOATING), "lcdcam_raw", "gpio pull");
    esp_rom_gpio_connect_in_signal(pin, signal, invert);
    return ESP_OK;
}

static esp_err_t configure_passive_gpio(int pin)
{
    gpio_func_sel(pin, PIN_FUNC_GPIO);
    gpio_intr_disable((gpio_num_t)pin);
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&config);
}

static esp_err_t configure_isolated_gpio(int pin)
{
    gpio_func_sel(pin, PIN_FUNC_GPIO);
    gpio_intr_disable((gpio_num_t)pin);
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        return err;
    }
    ESP_RETURN_ON_ERROR(gpio_set_pull_mode((gpio_num_t)pin, GPIO_FLOATING),
                        "lcdcam_raw",
                        "gpio isolate pull");
    return gpio_set_direction((gpio_num_t)pin, GPIO_MODE_DISABLE);
}

static void detach_lcdcam_input_signals(void)
{
    esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ZERO_INPUT, cam_periph_signals.buses[0].vsync_sig, false);
    esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ZERO_INPUT, cam_periph_signals.buses[0].de_sig, false);
    esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ZERO_INPUT, cam_periph_signals.buses[0].hsync_sig, false);
    esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ZERO_INPUT, cam_periph_signals.buses[0].pclk_sig, false);

    for (int i = 0; i < 16; ++i) {
        esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ZERO_INPUT, cam_periph_signals.buses[0].data_sigs[i], false);
    }
}

esp_err_t lcdcam_raw_enter_safe_idle(void)
{
    cam_ll_stop(CAM_LL_GET_HW(0));
    detach_lcdcam_input_signals();

    const int passive_gpios[] = {
        7, 8, 9, 10, 11, 12,
        13, 14, 15, 16, 17, 18,
        19, 20, 21, 22,
        3, 33, 36,
        45, 46, 47, 48, 50,
    };

    esp_err_t first_err = ESP_OK;
    for (size_t i = 0; i < sizeof(passive_gpios) / sizeof(passive_gpios[0]); ++i) {
        esp_err_t err = configure_passive_gpio(passive_gpios[i]);
        if (first_err == ESP_OK && err != ESP_OK) {
            first_err = err;
        }
    }
    return first_err;
}

esp_err_t lcdcam_raw_enter_electrical_isolate(void)
{
    cam_ll_stop(CAM_LL_GET_HW(0));
    detach_lcdcam_input_signals();

    const int isolated_gpios[] = {
        7, 8, 9, 10, 11, 12,
        13, 14, 15, 16, 17, 18,
        19, 20, 21, 22,
        3, 33, 36,
        45, 46, 47, 48, 50,
    };

    esp_err_t first_err = ESP_OK;
    for (size_t i = 0; i < sizeof(isolated_gpios) / sizeof(isolated_gpios[0]); ++i) {
        esp_err_t err = configure_isolated_gpio(isolated_gpios[i]);
        if (first_err == ESP_OK && err != ESP_OK) {
            first_err = err;
        }
    }
    return first_err;
}

static void fill_descriptors(lcdcam_raw_dma_desc_t *desc, uint8_t *buffer, size_t size)
{
    size_t index = 0;
    while (size > 0) {
        uint32_t node_size = MIN(size, LCDCAM_RAW_DMA_DESC_MAX_SIZE);
        desc[index].dw0.size = node_size;
        desc[index].dw0.length = 0;
        desc[index].dw0.err_eof = 0;
        desc[index].dw0.suc_eof = 0;
        desc[index].dw0.owner = DMA_DESCRIPTOR_BUFFER_OWNER_DMA;
        desc[index].buffer = buffer;
        desc[index].next = &desc[index + 1];

        buffer += node_size;
        size -= node_size;
        index++;
    }

    if (index > 0) {
        desc[index - 1].next = NULL;
    }
}

static size_t sum_descriptor_lengths(lcdcam_raw_dma_desc_t *desc, size_t desc_count)
{
    size_t total = 0;
    for (size_t i = 0; i < desc_count; ++i) {
        total += desc[i].dw0.length;
    }
    return total;
}

static size_t count_completed_descriptors(lcdcam_raw_dma_desc_t *desc, size_t desc_count)
{
    size_t completed = 0;
    for (size_t i = 0; i < desc_count; ++i) {
        if (desc[i].dw0.owner == DMA_DESCRIPTOR_BUFFER_OWNER_CPU || desc[i].dw0.length > 0) {
            completed++;
        }
    }
    return completed;
}

static void copy_descriptor_report(lcdcam_raw_result_t *result, lcdcam_raw_dma_desc_t *desc, size_t desc_count)
{
    size_t count = MIN(desc_count, (size_t)LCDCAM_RAW_MAX_REPORT_DESCRIPTORS);
    for (size_t i = 0; i < count; ++i) {
        result->descriptor_lengths[i] = desc[i].dw0.length;
        result->descriptor_owners[i] = desc[i].dw0.owner;
        result->descriptor_suc_eof[i] = desc[i].dw0.suc_eof;
        result->descriptor_err_eof[i] = desc[i].dw0.err_eof;
    }
}

static bool wait_for_level(int gpio, int target_level, uint32_t timeout_ms)
{
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (esp_timer_get_time() < deadline) {
        if (gpio_get_level(gpio) == target_level) {
            return true;
        }
    }
    return false;
}

static bool wait_for_edge(int gpio, int from_level, int to_level, uint32_t timeout_ms)
{
    if (!wait_for_level(gpio, from_level, timeout_ms)) {
        return false;
    }
    return wait_for_level(gpio, to_level, timeout_ms);
}

static bool wait_for_start_trigger(lcdcam_raw_start_mode_t start_mode, uint32_t timeout_ms)
{
    switch (start_mode) {
    case LCDCAM_RAW_START_AFTER_SPS_RISING:
        return wait_for_edge(33, 0, 1, timeout_ms);
    case LCDCAM_RAW_START_AFTER_SPS_THEN_SPL_FALLING:
        if (!wait_for_edge(33, 0, 1, timeout_ms)) {
            return false;
        }
        return wait_for_edge(19, 1, 0, timeout_ms);
    case LCDCAM_RAW_START_IMMEDIATE:
    default:
        return true;
    }
}

static bool IRAM_ATTR on_recv_eof(gdma_channel_handle_t dma_chan, gdma_event_data_t *event_data, void *user_data)
{
    (void)dma_chan;
    BaseType_t high_task_woken = pdFALSE;
    lcdcam_raw_context_t *context = (lcdcam_raw_context_t *)user_data;
    context->eof_seen = true;
    context->done_seen = true;
    context->eof_desc_addr = event_data == NULL ? 0 : event_data->rx_eof_desc_addr;
    xSemaphoreGiveFromISR(context->done_sem, &high_task_woken);
    return high_task_woken == pdTRUE;
}

static bool IRAM_ATTR on_recv_done(gdma_channel_handle_t dma_chan, gdma_event_data_t *event_data, void *user_data)
{
    (void)dma_chan;
    (void)event_data;
    lcdcam_raw_context_t *context = (lcdcam_raw_context_t *)user_data;
    context->completed_descriptors++;
    return false;
}

static bool IRAM_ATTR on_rearm_recv_eof(gdma_channel_handle_t dma_chan,
                                        gdma_event_data_t *event_data,
                                        void *user_data)
{
    (void)dma_chan;
    (void)event_data;
    BaseType_t high_task_woken = pdFALSE;
    lcdcam_raw_rearm_context_t *context = (lcdcam_raw_rearm_context_t *)user_data;
    uint32_t completed = context->completed_chunks;
    if (completed < context->requested_chunks) {
        int64_t now_us = esp_timer_get_time();
        context->chunk_us[completed] = now_us - context->last_chunk_started_us;
        context->completed_chunks = completed + 1U;
    }

    if (context->completed_chunks >= context->requested_chunks) {
        cam_ll_stop(context->hw);
        (void)gdma_stop(context->dma_chan);
        xSemaphoreGiveFromISR(context->done_sem, &high_task_woken);
        return high_task_woken == pdTRUE;
    }

    uint32_t next_slot = (context->current_slot + 1U) & 1U;
    context->current_slot = next_slot;

    cam_ll_stop(context->hw);
    (void)gdma_stop(context->dma_chan);
    esp_err_t err = gdma_reset(context->dma_chan);
    if (err == ESP_OK) {
        memset(context->buffers[next_slot], 0, context->buffer_len);
        memset(context->descs[next_slot], 0, context->desc_size);
        fill_descriptors(context->descs[next_slot], context->buffers[next_slot], context->buffer_len);
        err = esp_cache_msync(context->descs[next_slot],
                              context->desc_size,
                              ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_INVALIDATE);
    }
    if (err == ESP_OK) {
        err = gdma_start(context->dma_chan, (intptr_t)context->descs[next_slot]);
    }
    if (err == ESP_OK) {
        context->last_chunk_started_us = esp_timer_get_time();
        cam_ll_reset(context->hw);
        cam_ll_fifo_reset(context->hw);
        cam_ll_start(context->hw);
    } else {
        context->last_esp_err = err;
        context->failed_rearms++;
        xSemaphoreGiveFromISR(context->done_sem, &high_task_woken);
    }
    return high_task_woken == pdTRUE;
}

static void summarize_buffer(lcdcam_raw_result_t *result)
{
    result->checksum = 0;
    result->min_value = 0xff;
    result->max_value = 0x00;
    result->raw8_transitions = 0;

    for (size_t i = 0; i < result->buffer_len; ++i) {
        uint8_t value = result->buffer[i];
        result->checksum = (result->checksum + value) & 0xffffffffU;
        if (value < result->min_value) {
            result->min_value = value;
        }
        if (value > result->max_value) {
            result->max_value = value;
        }
        if (i > 0 && value != result->buffer[i - 1]) {
            result->raw8_transitions++;
        }
    }
}

static size_t lcdcam_raw_bytes_per_sample(lcdcam_raw_data_mode_t data_mode)
{
    return (data_mode == LCDCAM_RAW_DATA_RGB664 || data_mode == LCDCAM_RAW_DATA_RGB565) ? 2U : 1U;
}

static uint32_t lcdcam_raw_input_data_width(lcdcam_raw_data_mode_t data_mode)
{
    return (data_mode == LCDCAM_RAW_DATA_RGB664 || data_mode == LCDCAM_RAW_DATA_RGB565) ? 16U : 8U;
}

static esp_err_t route_lcdcam_inputs(lcdcam_raw_de_source_t de_source,
                                     bool vsync_invert,
                                     bool de_invert,
                                     bool pclk_invert,
                                     bool vh_de_mode,
                                     lcdcam_raw_data_mode_t data_mode)
{
    const int de_gpio = de_source == LCDCAM_RAW_DE_LP ? 21 : 19;
    const int hsync_gpio = de_source == LCDCAM_RAW_DE_LP ? 19 : 21;

    ESP_RETURN_ON_ERROR(configure_input_gpio(33, cam_periph_signals.buses[0].vsync_sig, vsync_invert),
                        "lcdcam_raw", "vsync route");
    if (de_source == LCDCAM_RAW_DE_HIGH) {
        esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ONE_INPUT, cam_periph_signals.buses[0].de_sig, de_invert);
    } else {
        ESP_RETURN_ON_ERROR(configure_input_gpio(de_gpio, cam_periph_signals.buses[0].de_sig, de_invert),
                            "lcdcam_raw", "de route");
    }
    if (vh_de_mode) {
        ESP_RETURN_ON_ERROR(configure_input_gpio(hsync_gpio, cam_periph_signals.buses[0].hsync_sig, false),
                            "lcdcam_raw", "hsync route");
    }
    ESP_RETURN_ON_ERROR(configure_input_gpio(22, cam_periph_signals.buses[0].pclk_sig, pclk_invert),
                        "lcdcam_raw", "pclk route");

    const int rg44_data_gpio[8] = {
        16, /* bit0: R2 */
        15, /* bit1: R3 */
        14, /* bit2: R4 */
        13, /* bit3: R5 */
        10, /* bit4: G2 */
        9,  /* bit5: G3 */
        8,  /* bit6: G4 */
        7,  /* bit7: G5 */
    };
    const int rgb332_data_gpio[8] = {
        48, /* bit0: B4 */
        50, /* bit1: B5 */
        9,  /* bit2: G3 */
        8,  /* bit3: G4 */
        7,  /* bit4: G5 */
        15, /* bit5: R3 */
        14, /* bit6: R4 */
        13, /* bit7: R5 */
    };
    const int rgb664_data_gpio[16] = {
        18, /* bit0: R0 */
        17, /* bit1: R1 */
        16, /* bit2: R2 */
        15, /* bit3: R3 */
        14, /* bit4: R4 */
        13, /* bit5: R5 */
        12, /* bit6: G0 */
        11, /* bit7: G1 */
        10, /* bit8: G2 */
        9,  /* bit9: G3 */
        8,  /* bit10: G4 */
        7,  /* bit11: G5 */
        46, /* bit12: B2 */
        47, /* bit13: B3 */
        48, /* bit14: B4 */
        50, /* bit15: B5 */
    };
    const int rgb565_data_gpio[16] = {
        45, /* bit0: B1 */
        46, /* bit1: B2 */
        47, /* bit2: B3 */
        48, /* bit3: B4 */
        50, /* bit4: B5 */
        12, /* bit5: G0 */
        11, /* bit6: G1 */
        10, /* bit7: G2 */
        9,  /* bit8: G3 */
        8,  /* bit9: G4 */
        7,  /* bit10: G5 */
        17, /* bit11: R1 */
        16, /* bit12: R2 */
        15, /* bit13: R3 */
        14, /* bit14: R4 */
        13, /* bit15: R5 */
    };
    const int *data_gpio = rg44_data_gpio;
    int data_width = 8;
    if (data_mode == LCDCAM_RAW_DATA_RGB332) {
        data_gpio = rgb332_data_gpio;
    } else if (data_mode == LCDCAM_RAW_DATA_RGB664) {
        data_gpio = rgb664_data_gpio;
        data_width = 16;
    } else if (data_mode == LCDCAM_RAW_DATA_RGB565) {
        data_gpio = rgb565_data_gpio;
        data_width = 16;
    }
    for (int i = 0; i < data_width; ++i) {
        ESP_RETURN_ON_ERROR(configure_input_gpio(data_gpio[i], cam_periph_signals.buses[0].data_sigs[i], false),
                            "lcdcam_raw", "data route");
    }
    return ESP_OK;
}

esp_err_t lcdcam_raw_capture(lcdcam_raw_de_source_t de_source,
                             uint32_t h_res,
                             uint32_t v_res,
                             uint32_t timeout_ms,
                             bool vsync_invert,
                             bool de_invert,
                             bool pclk_invert,
                             bool byte_count_eof,
                             lcdcam_raw_start_mode_t start_mode,
                             bool vh_de_mode,
                             lcdcam_raw_data_mode_t data_mode,
                             lcdcam_raw_result_t *result)
{
    const char *stage = "validate_args";
    if (result == NULL ||
        h_res == 0 ||
        h_res > LCDCAM_RAW_MAX_H_RES ||
        v_res == 0 ||
        v_res > LCDCAM_RAW_MAX_V_RES ||
        timeout_ms == 0 ||
        timeout_ms > 5000) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(result, 0, sizeof(*result));
    result->buffer_len = h_res * v_res * lcdcam_raw_bytes_per_sample(data_mode);
    result->timeout_ms = timeout_ms;
    result->de_source = de_source;
    result->vsync_invert = vsync_invert;
    result->de_invert = de_invert;
    result->pclk_invert = pclk_invert;
    result->byte_count_eof = byte_count_eof;
    result->vh_de_mode = vh_de_mode;
    result->hsync_gpio = de_source == LCDCAM_RAW_DE_LP ? 19 : 21;
    result->data_mode = data_mode;
    result->start_mode = start_mode;
    result->failure_stage = "none";
    result->failure_err = ESP_OK;

    gdma_channel_handle_t dma_chan = NULL;
    uint8_t *buffer = NULL;
    lcdcam_raw_dma_desc_t *desc = NULL;
    SemaphoreHandle_t done_sem = NULL;
    bool dvp_initialized = false;

    (void)lcdcam_raw_enter_safe_idle();

    stage = "esp_cache_get_alignment";
    size_t alignment_size = 0;
    esp_err_t err = esp_cache_get_alignment(MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA, &alignment_size);
    if (err != ESP_OK) {
        goto fail;
    }

    stage = "heap_caps_aligned_calloc_buffer";
    buffer = heap_caps_aligned_calloc(alignment_size,
                                      1,
                                      result->buffer_len,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }

    stage = "heap_caps_aligned_calloc_desc";
    size_t desc_count = (result->buffer_len + LCDCAM_RAW_DMA_DESC_MAX_SIZE - 1U) / LCDCAM_RAW_DMA_DESC_MAX_SIZE;
    size_t desc_size = ((desc_count * sizeof(lcdcam_raw_dma_desc_t)) + alignment_size - 1U) & ~(alignment_size - 1U);
    desc = heap_caps_aligned_calloc(alignment_size,
                                    1,
                                    desc_size,
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (desc == NULL) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }
    result->descriptor_count = desc_count;

    stage = "xSemaphoreCreateBinary";
    done_sem = xSemaphoreCreateBinary();
    if (done_sem == NULL) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }

    stage = "gdma_new_axi_channel";
    gdma_channel_alloc_config_t alloc_config = {
        .direction = GDMA_CHANNEL_DIRECTION_RX,
    };
    err = gdma_new_axi_channel(&alloc_config, &dma_chan);
    if (err != ESP_OK) {
        goto fail;
    }

    stage = "gdma_connect";
    err = gdma_connect(dma_chan, GDMA_MAKE_TRIGGER(GDMA_TRIG_PERIPH_CAM, 0));
    if (err != ESP_OK) {
        goto fail;
    }

    stage = "gdma_apply_strategy";
    gdma_strategy_config_t strategy_config = {
        .owner_check = true,
        .auto_update_desc = false,
    };
    err = gdma_apply_strategy(dma_chan, &strategy_config);
    if (err != ESP_OK) {
        goto fail;
    }

    stage = "gdma_config_transfer";
    gdma_transfer_config_t transfer_config = {
        .max_data_burst_size = 128,
        .access_ext_mem = true,
    };
    err = gdma_config_transfer(dma_chan, &transfer_config);
    if (err != ESP_OK) {
        goto fail;
    }

    lcdcam_raw_context_t context = {
        .done_sem = done_sem,
        .desc = desc,
        .desc_count = desc_count,
    };
    gdma_rx_event_callbacks_t cbs = {
        .on_recv_eof = on_recv_eof,
        .on_recv_done = on_recv_done,
    };
    stage = "gdma_register_rx_event_callbacks";
    err = gdma_register_rx_event_callbacks(dma_chan, &cbs, &context);
    if (err != ESP_OK) {
        goto fail;
    }

    esp_cam_ctlr_dvp_pin_config_t pin_config = {
        .data_width = CAM_CTLR_DATA_WIDTH_8,
        .data_io = {16, 15, 14, 13, 10, 9, 8, 7, -1, -1, -1, -1, -1, -1, -1, -1},
        .pclk_io = 22,
        .vsync_io = 33,
        .de_io = de_source == LCDCAM_RAW_DE_LP ? 21 : 19,
        .xclk_io = GPIO_NUM_NC,
    };
    stage = "esp_cam_ctlr_dvp_init";
    err = esp_cam_ctlr_dvp_init(0, CAM_CLK_SRC_DEFAULT, &pin_config);
    if (err != ESP_OK) {
        goto fail;
    }
    dvp_initialized = true;

    stage = "route_lcdcam_inputs";
    err = route_lcdcam_inputs(de_source, vsync_invert, de_invert, pclk_invert, vh_de_mode, data_mode);
    if (err != ESP_OK) {
        goto fail;
    }

    lcd_cam_dev_t *hw = CAM_LL_GET_HW(0);
    cam_ll_enable_stop_signal(hw, false);
    cam_ll_swap_dma_data_byte_order(hw, false);
    cam_ll_reverse_dma_data_bit_order(hw, false);
    cam_ll_enable_rgb_yuv_convert(hw, false);
    cam_ll_set_input_data_width(hw, lcdcam_raw_input_data_width(data_mode));
    cam_ll_set_vh_de_mode(hw, vh_de_mode);
    cam_ll_enable_vsync_filter(hw, false);
    cam_ll_enable_vsync_generate_eof(hw, !byte_count_eof);
    if (byte_count_eof) {
        cam_ll_set_recv_data_bytelen(hw, result->buffer_len - 1U);
    }

    fill_descriptors(desc, buffer, result->buffer_len);
    stage = "esp_cache_msync_desc";
    err = esp_cache_msync(desc, desc_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_INVALIDATE);
    if (err != ESP_OK) {
        goto fail;
    }

    stage = "gdma_reset";
    err = gdma_reset(dma_chan);
    if (err != ESP_OK) {
        goto fail;
    }

    stage = "gdma_start";
    err = gdma_start(dma_chan, (intptr_t)desc);
    if (err != ESP_OK) {
        goto fail;
    }

    stage = "wait_for_start_trigger";
    result->start_trigger_seen = wait_for_start_trigger(start_mode, timeout_ms);
    if (!result->start_trigger_seen) {
        err = ESP_ERR_TIMEOUT;
        goto capture_done;
    }

    stage = "cam_start";
    cam_ll_reset(hw);
    cam_ll_fifo_reset(hw);
    cam_ll_start(hw);

    if (xSemaphoreTake(done_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        err = ESP_ERR_TIMEOUT;
    } else {
        err = ESP_OK;
    }

capture_done:
    cam_ll_stop(hw);
    (void)gdma_stop(dma_chan);

    (void)esp_cache_msync(desc, desc_size, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    result->received_size = sum_descriptor_lengths(desc, desc_count);
    result->completed_descriptors = MAX(context.completed_descriptors, count_completed_descriptors(desc, desc_count));
    copy_descriptor_report(result, desc, desc_count);
    result->eof_seen = context.eof_seen;
    result->done_seen = context.done_seen;
    result->eof_desc_addr = context.eof_desc_addr;

    (void)esp_cache_msync(buffer, result->buffer_len, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    result->buffer = buffer;
    summarize_buffer(result);

    if (dvp_initialized) {
        (void)esp_cam_ctlr_dvp_deinit(0);
    }
    (void)lcdcam_raw_enter_safe_idle();
    if (dma_chan != NULL) {
        (void)gdma_disconnect(dma_chan);
        (void)gdma_del_channel(dma_chan);
    }
    if (done_sem != NULL) {
        vSemaphoreDelete(done_sem);
    }
    heap_caps_free(desc);

    if (err != ESP_OK) {
        result->failure_stage = "frame_done_timeout";
        result->failure_err = err;
        return err;
    }

    result->failure_stage = "none";
    result->failure_err = ESP_OK;
    return ESP_OK;

fail:
    if (dma_chan != NULL) {
        (void)gdma_stop(dma_chan);
    }
    cam_ll_stop(CAM_LL_GET_HW(0));
    if (dvp_initialized) {
        (void)esp_cam_ctlr_dvp_deinit(0);
    }
    (void)lcdcam_raw_enter_safe_idle();
    if (dma_chan != NULL) {
        (void)gdma_disconnect(dma_chan);
        (void)gdma_del_channel(dma_chan);
    }
    if (done_sem != NULL) {
        vSemaphoreDelete(done_sem);
    }
    heap_caps_free(desc);
    heap_caps_free(buffer);
    result->buffer = NULL;
    result->failure_stage = stage;
    result->failure_err = err;
    return err;
}

void lcdcam_raw_result_free(lcdcam_raw_result_t *result)
{
    if (result == NULL) {
        return;
    }
    heap_caps_free(result->buffer);
    memset(result, 0, sizeof(*result));
}

esp_err_t lcdcam_raw_capture_loop(lcdcam_raw_de_source_t de_source,
                                  uint32_t h_res,
                                  uint32_t v_res,
                                  uint32_t timeout_ms,
                                  bool vsync_invert,
                                  bool de_invert,
                                  bool pclk_invert,
                                  bool byte_count_eof,
                                  lcdcam_raw_start_mode_t start_mode,
                                  bool vh_de_mode,
                                  lcdcam_raw_data_mode_t data_mode,
                                  uint32_t frame_count,
                                  lcdcam_raw_frame_callback_t frame_callback,
                                  void *callback_user_data,
                                  lcdcam_raw_loop_stats_t *stats)
{
    const char *stage = "validate_args";
    if (stats == NULL ||
        h_res == 0 ||
        h_res > LCDCAM_RAW_MAX_H_RES ||
        v_res == 0 ||
        v_res > LCDCAM_RAW_MAX_V_RES ||
        timeout_ms == 0 ||
        timeout_ms > 5000 ||
        frame_count == 0 ||
        frame_count > 512) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(stats, 0, sizeof(*stats));
    stats->requested_frames = frame_count;
    stats->last_esp_err = ESP_OK;
    stats->failure_stage = "none";

    const size_t buffer_len = h_res * v_res * lcdcam_raw_bytes_per_sample(data_mode);
    gdma_channel_handle_t dma_chan = NULL;
    uint8_t *buffer = NULL;
    lcdcam_raw_dma_desc_t *desc = NULL;
    SemaphoreHandle_t done_sem = NULL;
    bool dvp_initialized = false;
    size_t desc_count = 0;
    size_t desc_size = 0;
    size_t alignment_size = 0;
    int64_t total_capture_us = 0;
    int64_t started_us = esp_timer_get_time();

    (void)lcdcam_raw_enter_safe_idle();

    esp_err_t err = esp_cache_get_alignment(MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA, &alignment_size);
    if (err != ESP_OK) {
        goto fail;
    }

    stage = "heap_caps_aligned_calloc_buffer";
    buffer = heap_caps_aligned_calloc(alignment_size,
                                      1,
                                      buffer_len,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }

    stage = "heap_caps_aligned_calloc_desc";
    desc_count = (buffer_len + LCDCAM_RAW_DMA_DESC_MAX_SIZE - 1U) / LCDCAM_RAW_DMA_DESC_MAX_SIZE;
    desc_size = ((desc_count * sizeof(lcdcam_raw_dma_desc_t)) + alignment_size - 1U) & ~(alignment_size - 1U);
    desc = heap_caps_aligned_calloc(alignment_size,
                                    1,
                                    desc_size,
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (desc == NULL) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }

    stage = "xSemaphoreCreateBinary";
    done_sem = xSemaphoreCreateBinary();
    if (done_sem == NULL) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }

    stage = "gdma_new_axi_channel";
    gdma_channel_alloc_config_t alloc_config = {
        .direction = GDMA_CHANNEL_DIRECTION_RX,
    };
    err = gdma_new_axi_channel(&alloc_config, &dma_chan);
    if (err != ESP_OK) {
        goto fail;
    }

    stage = "gdma_connect";
    err = gdma_connect(dma_chan, GDMA_MAKE_TRIGGER(GDMA_TRIG_PERIPH_CAM, 0));
    if (err != ESP_OK) {
        goto fail;
    }

    stage = "gdma_apply_strategy";
    gdma_strategy_config_t strategy_config = {
        .owner_check = true,
        .auto_update_desc = false,
    };
    err = gdma_apply_strategy(dma_chan, &strategy_config);
    if (err != ESP_OK) {
        goto fail;
    }

    stage = "gdma_config_transfer";
    gdma_transfer_config_t transfer_config = {
        .max_data_burst_size = 128,
        .access_ext_mem = true,
    };
    err = gdma_config_transfer(dma_chan, &transfer_config);
    if (err != ESP_OK) {
        goto fail;
    }

    lcdcam_raw_context_t context = {
        .done_sem = done_sem,
        .desc = desc,
        .desc_count = desc_count,
    };
    gdma_rx_event_callbacks_t cbs = {
        .on_recv_eof = on_recv_eof,
        .on_recv_done = on_recv_done,
    };
    stage = "gdma_register_rx_event_callbacks";
    err = gdma_register_rx_event_callbacks(dma_chan, &cbs, &context);
    if (err != ESP_OK) {
        goto fail;
    }

    esp_cam_ctlr_dvp_pin_config_t pin_config = {
        .data_width = CAM_CTLR_DATA_WIDTH_8,
        .data_io = {16, 15, 14, 13, 10, 9, 8, 7, -1, -1, -1, -1, -1, -1, -1, -1},
        .pclk_io = 22,
        .vsync_io = 33,
        .de_io = de_source == LCDCAM_RAW_DE_LP ? 21 : 19,
        .xclk_io = GPIO_NUM_NC,
    };
    stage = "esp_cam_ctlr_dvp_init";
    err = esp_cam_ctlr_dvp_init(0, CAM_CLK_SRC_DEFAULT, &pin_config);
    if (err != ESP_OK) {
        goto fail;
    }
    dvp_initialized = true;

    stage = "route_lcdcam_inputs";
    err = route_lcdcam_inputs(de_source, vsync_invert, de_invert, pclk_invert, vh_de_mode, data_mode);
    if (err != ESP_OK) {
        goto fail;
    }

    lcd_cam_dev_t *hw = CAM_LL_GET_HW(0);
    cam_ll_enable_stop_signal(hw, false);
    cam_ll_swap_dma_data_byte_order(hw, false);
    cam_ll_reverse_dma_data_bit_order(hw, false);
    cam_ll_enable_rgb_yuv_convert(hw, false);
    cam_ll_set_input_data_width(hw, lcdcam_raw_input_data_width(data_mode));
    cam_ll_set_vh_de_mode(hw, vh_de_mode);
    cam_ll_enable_vsync_filter(hw, false);
    cam_ll_enable_vsync_generate_eof(hw, !byte_count_eof);
    if (byte_count_eof) {
        cam_ll_set_recv_data_bytelen(hw, buffer_len - 1U);
    }

    for (uint32_t frame = 0; frame < frame_count; ++frame) {
        while (xSemaphoreTake(done_sem, 0) == pdTRUE) {
        }
        memset(buffer, 0, buffer_len);
        memset(desc, 0, desc_size);
        memset(&context, 0, sizeof(context));
        context.done_sem = done_sem;
        context.desc = desc;
        context.desc_count = desc_count;
        fill_descriptors(desc, buffer, buffer_len);

        int64_t frame_start_us = esp_timer_get_time();
        err = esp_cache_msync(desc, desc_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_INVALIDATE);
        if (err != ESP_OK) {
            stage = "esp_cache_msync_desc";
            goto fail;
        }
        err = gdma_reset(dma_chan);
        if (err != ESP_OK) {
            stage = "gdma_reset";
            goto fail;
        }
        err = gdma_start(dma_chan, (intptr_t)desc);
        if (err != ESP_OK) {
            stage = "gdma_start";
            goto fail;
        }

        bool start_trigger_seen = wait_for_start_trigger(start_mode, timeout_ms);
        if (!start_trigger_seen) {
            err = ESP_ERR_TIMEOUT;
        } else {
            cam_ll_reset(hw);
            cam_ll_fifo_reset(hw);
            cam_ll_start(hw);
            err = xSemaphoreTake(done_sem, pdMS_TO_TICKS(timeout_ms)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
        }

        cam_ll_stop(hw);
        (void)gdma_stop(dma_chan);
        int64_t capture_us = esp_timer_get_time() - frame_start_us;
        total_capture_us += capture_us;
        if (capture_us > stats->max_capture_us) {
            stats->max_capture_us = capture_us;
        }
        stats->last_esp_err = err;

        lcdcam_raw_result_t frame_result = {
            .buffer = buffer,
            .buffer_len = buffer_len,
            .descriptor_count = desc_count,
            .timeout_ms = timeout_ms,
            .de_source = de_source,
            .vsync_invert = vsync_invert,
            .de_invert = de_invert,
            .pclk_invert = pclk_invert,
            .byte_count_eof = byte_count_eof,
            .vh_de_mode = vh_de_mode,
            .hsync_gpio = de_source == LCDCAM_RAW_DE_LP ? 19 : 21,
            .data_mode = data_mode,
            .start_mode = start_mode,
            .start_trigger_seen = start_trigger_seen,
            .failure_stage = err == ESP_OK ? "none" : "frame_done_timeout",
            .failure_err = err,
        };

        (void)esp_cache_msync(desc, desc_size, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
        frame_result.received_size = sum_descriptor_lengths(desc, desc_count);
        frame_result.completed_descriptors = MAX(context.completed_descriptors, count_completed_descriptors(desc, desc_count));
        copy_descriptor_report(&frame_result, desc, desc_count);
        frame_result.eof_seen = context.eof_seen;
        frame_result.done_seen = context.done_seen;
        frame_result.eof_desc_addr = context.eof_desc_addr;
        (void)esp_cache_msync(buffer, buffer_len, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
        summarize_buffer(&frame_result);

        if (err == ESP_OK) {
            stats->captured_frames++;
            if (frame_callback != NULL) {
                frame_callback(&frame_result, capture_us, callback_user_data);
            }
        } else {
            stats->failed_frames++;
        }
    }

    stats->elapsed_us = esp_timer_get_time() - started_us;
    stats->avg_capture_us = frame_count > 0 ? total_capture_us / (int64_t)frame_count : 0;
    if (dvp_initialized) {
        (void)esp_cam_ctlr_dvp_deinit(0);
    }
    (void)lcdcam_raw_enter_safe_idle();
    if (dma_chan != NULL) {
        (void)gdma_disconnect(dma_chan);
        (void)gdma_del_channel(dma_chan);
    }
    if (done_sem != NULL) {
        vSemaphoreDelete(done_sem);
    }
    heap_caps_free(desc);
    heap_caps_free(buffer);
    return stats->failed_frames == 0 ? ESP_OK : stats->last_esp_err;

fail:
    stats->failure_stage = stage;
    stats->last_esp_err = err;
    stats->elapsed_us = esp_timer_get_time() - started_us;
    if (dma_chan != NULL) {
        (void)gdma_stop(dma_chan);
    }
    cam_ll_stop(CAM_LL_GET_HW(0));
    if (dvp_initialized) {
        (void)esp_cam_ctlr_dvp_deinit(0);
    }
    (void)lcdcam_raw_enter_safe_idle();
    if (dma_chan != NULL) {
        (void)gdma_disconnect(dma_chan);
        (void)gdma_del_channel(dma_chan);
    }
    if (done_sem != NULL) {
        vSemaphoreDelete(done_sem);
    }
    heap_caps_free(desc);
    heap_caps_free(buffer);
    return err;
}

esp_err_t lcdcam_raw_rearm_bench(lcdcam_raw_de_source_t de_source,
                                 uint32_t h_res,
                                 uint32_t v_res,
                                 uint32_t timeout_ms,
                                 bool vsync_invert,
                                 bool de_invert,
                                 bool pclk_invert,
                                 bool byte_count_eof,
                                 lcdcam_raw_start_mode_t start_mode,
                                 bool vh_de_mode,
                                 lcdcam_raw_data_mode_t data_mode,
                                 uint32_t chunk_count,
                                 lcdcam_raw_rearm_stats_t *stats)
{
    const char *stage = "validate_args";
    if (stats == NULL ||
        h_res == 0 ||
        h_res > LCDCAM_RAW_MAX_H_RES ||
        v_res == 0 ||
        v_res > LCDCAM_RAW_MAX_V_RES ||
        timeout_ms == 0 ||
        timeout_ms > 5000 ||
        chunk_count == 0 ||
        chunk_count > 512) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(stats, 0, sizeof(*stats));
    stats->requested_chunks = chunk_count;
    stats->timeout_ms = timeout_ms;
    stats->h_res = h_res;
    stats->v_res = v_res;
    stats->bytes_per_sample = lcdcam_raw_bytes_per_sample(data_mode);
    stats->chunk_bytes = h_res * v_res * stats->bytes_per_sample;
    stats->data_mode = data_mode;
    stats->last_esp_err = ESP_OK;
    stats->failure_stage = "none";

    gdma_channel_handle_t dma_chan = NULL;
    uint8_t *buffers[2] = {0};
    lcdcam_raw_dma_desc_t *descs[2] = {0};
    int64_t *chunk_us = NULL;
    SemaphoreHandle_t done_sem = NULL;
    bool dvp_initialized = false;
    size_t alignment_size = 0;
    size_t desc_count = 0;
    size_t desc_size = 0;
    int64_t started_us = esp_timer_get_time();

    (void)lcdcam_raw_enter_safe_idle();

    esp_err_t err = esp_cache_get_alignment(MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA, &alignment_size);
    if (err != ESP_OK) {
        goto fail;
    }

    stage = "allocate_buffers";
    for (size_t i = 0; i < 2; ++i) {
        buffers[i] = heap_caps_aligned_calloc(alignment_size,
                                              1,
                                              stats->chunk_bytes,
                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
        if (buffers[i] == NULL) {
            err = ESP_ERR_NO_MEM;
            goto fail;
        }
    }

    stage = "allocate_descriptors";
    desc_count = (stats->chunk_bytes + LCDCAM_RAW_DMA_DESC_MAX_SIZE - 1U) / LCDCAM_RAW_DMA_DESC_MAX_SIZE;
    desc_size = ((desc_count * sizeof(lcdcam_raw_dma_desc_t)) + alignment_size - 1U) & ~(alignment_size - 1U);
    for (size_t i = 0; i < 2; ++i) {
        descs[i] = heap_caps_aligned_calloc(alignment_size,
                                            1,
                                            desc_size,
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
        if (descs[i] == NULL) {
            err = ESP_ERR_NO_MEM;
            goto fail;
        }
    }

    stage = "allocate_chunk_timing";
    chunk_us = heap_caps_calloc(chunk_count, sizeof(int64_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (chunk_us == NULL) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }

    stage = "xSemaphoreCreateBinary";
    done_sem = xSemaphoreCreateBinary();
    if (done_sem == NULL) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }

    stage = "gdma_new_axi_channel";
    gdma_channel_alloc_config_t alloc_config = {
        .direction = GDMA_CHANNEL_DIRECTION_RX,
    };
    err = gdma_new_axi_channel(&alloc_config, &dma_chan);
    if (err != ESP_OK) {
        goto fail;
    }

    stage = "gdma_connect";
    err = gdma_connect(dma_chan, GDMA_MAKE_TRIGGER(GDMA_TRIG_PERIPH_CAM, 0));
    if (err != ESP_OK) {
        goto fail;
    }

    stage = "gdma_apply_strategy";
    gdma_strategy_config_t strategy_config = {
        .owner_check = true,
        .auto_update_desc = false,
    };
    err = gdma_apply_strategy(dma_chan, &strategy_config);
    if (err != ESP_OK) {
        goto fail;
    }

    stage = "gdma_config_transfer";
    gdma_transfer_config_t transfer_config = {
        .max_data_burst_size = 128,
        .access_ext_mem = true,
    };
    err = gdma_config_transfer(dma_chan, &transfer_config);
    if (err != ESP_OK) {
        goto fail;
    }

    lcdcam_raw_rearm_context_t context = {
        .dma_chan = dma_chan,
        .done_sem = done_sem,
        .hw = CAM_LL_GET_HW(0),
        .descs = descs,
        .buffers = buffers,
        .desc_size = desc_size,
        .buffer_len = stats->chunk_bytes,
        .requested_chunks = chunk_count,
        .chunk_us = chunk_us,
        .last_esp_err = ESP_OK,
    };
    gdma_rx_event_callbacks_t cbs = {
        .on_recv_eof = on_rearm_recv_eof,
    };
    stage = "gdma_register_rx_event_callbacks";
    err = gdma_register_rx_event_callbacks(dma_chan, &cbs, &context);
    if (err != ESP_OK) {
        goto fail;
    }

    esp_cam_ctlr_dvp_pin_config_t pin_config = {
        .data_width = CAM_CTLR_DATA_WIDTH_8,
        .data_io = {16, 15, 14, 13, 10, 9, 8, 7, -1, -1, -1, -1, -1, -1, -1, -1},
        .pclk_io = 22,
        .vsync_io = 33,
        .de_io = de_source == LCDCAM_RAW_DE_LP ? 21 : 19,
        .xclk_io = GPIO_NUM_NC,
    };
    stage = "esp_cam_ctlr_dvp_init";
    err = esp_cam_ctlr_dvp_init(0, CAM_CLK_SRC_DEFAULT, &pin_config);
    if (err != ESP_OK) {
        goto fail;
    }
    dvp_initialized = true;

    stage = "route_lcdcam_inputs";
    err = route_lcdcam_inputs(de_source, vsync_invert, de_invert, pclk_invert, vh_de_mode, data_mode);
    if (err != ESP_OK) {
        goto fail;
    }

    lcd_cam_dev_t *hw = CAM_LL_GET_HW(0);
    cam_ll_enable_stop_signal(hw, false);
    cam_ll_swap_dma_data_byte_order(hw, false);
    cam_ll_reverse_dma_data_bit_order(hw, false);
    cam_ll_enable_rgb_yuv_convert(hw, false);
    cam_ll_set_input_data_width(hw, lcdcam_raw_input_data_width(data_mode));
    cam_ll_set_vh_de_mode(hw, vh_de_mode);
    cam_ll_enable_vsync_filter(hw, false);
    cam_ll_enable_vsync_generate_eof(hw, !byte_count_eof);
    if (byte_count_eof) {
        cam_ll_set_recv_data_bytelen(hw, stats->chunk_bytes - 1U);
    }

    stage = "fill_first_descriptor";
    fill_descriptors(descs[0], buffers[0], stats->chunk_bytes);
    err = esp_cache_msync(descs[0], desc_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_INVALIDATE);
    if (err != ESP_OK) {
        goto fail;
    }

    stage = "gdma_reset";
    err = gdma_reset(dma_chan);
    if (err != ESP_OK) {
        goto fail;
    }

    stage = "gdma_start";
    err = gdma_start(dma_chan, (intptr_t)descs[0]);
    if (err != ESP_OK) {
        goto fail;
    }

    stage = "wait_for_start_trigger";
    stats->start_trigger_seen = wait_for_start_trigger(start_mode, timeout_ms);
    if (!stats->start_trigger_seen) {
        err = ESP_ERR_TIMEOUT;
        goto fail;
    }

    stage = "cam_start";
    context.last_chunk_started_us = esp_timer_get_time();
    cam_ll_reset(hw);
    cam_ll_fifo_reset(hw);
    cam_ll_start(hw);

    uint32_t wait_ms = timeout_ms * chunk_count;
    if (wait_ms < timeout_ms) {
        wait_ms = timeout_ms;
    }
    if (xSemaphoreTake(done_sem, pdMS_TO_TICKS(wait_ms)) != pdTRUE && context.completed_chunks < chunk_count) {
        err = ESP_ERR_TIMEOUT;
    } else {
        err = context.last_esp_err;
    }

    cam_ll_stop(hw);
    (void)gdma_stop(dma_chan);
    stats->elapsed_us = esp_timer_get_time() - started_us;
    stats->completed_chunks = context.completed_chunks;
    stats->failed_rearms = context.failed_rearms;
    stats->last_esp_err = err;
    int64_t total_chunk_us = 0;
    for (uint32_t i = 0; i < context.completed_chunks && i < chunk_count; ++i) {
        int64_t chunk_us = context.chunk_us[i];
        total_chunk_us += chunk_us;
        if (i == 0) {
            stats->first_chunk_us = chunk_us;
        }
        if (chunk_us > stats->max_chunk_us) {
            stats->max_chunk_us = chunk_us;
        }
    }
    stats->avg_chunk_us = context.completed_chunks > 0 ? total_chunk_us / (int64_t)context.completed_chunks : 0;
    for (size_t slot = 0; slot < 2; ++slot) {
        (void)esp_cache_msync(buffers[slot], stats->chunk_bytes, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
        size_t sample_count = MIN((size_t)64, (size_t)stats->chunk_bytes);
        for (size_t i = 0; i < sample_count; ++i) {
            stats->checksum = (stats->checksum * 33U) ^ buffers[slot][i];
        }
    }

    if (dvp_initialized) {
        (void)esp_cam_ctlr_dvp_deinit(0);
    }
    (void)lcdcam_raw_enter_safe_idle();
    if (dma_chan != NULL) {
        (void)gdma_disconnect(dma_chan);
        (void)gdma_del_channel(dma_chan);
    }
    if (done_sem != NULL) {
        vSemaphoreDelete(done_sem);
    }
    heap_caps_free(chunk_us);
    for (size_t i = 0; i < 2; ++i) {
        heap_caps_free(descs[i]);
        heap_caps_free(buffers[i]);
    }
    return (err == ESP_OK && stats->completed_chunks == chunk_count) ? ESP_OK : err;

fail:
    stats->failure_stage = stage;
    stats->last_esp_err = err;
    stats->elapsed_us = esp_timer_get_time() - started_us;
    if (dma_chan != NULL) {
        (void)gdma_stop(dma_chan);
    }
    cam_ll_stop(CAM_LL_GET_HW(0));
    if (dvp_initialized) {
        (void)esp_cam_ctlr_dvp_deinit(0);
    }
    (void)lcdcam_raw_enter_safe_idle();
    if (dma_chan != NULL) {
        (void)gdma_disconnect(dma_chan);
        (void)gdma_del_channel(dma_chan);
    }
    if (done_sem != NULL) {
        vSemaphoreDelete(done_sem);
    }
    heap_caps_free(chunk_us);
    for (size_t i = 0; i < 2; ++i) {
        heap_caps_free(descs[i]);
        heap_caps_free(buffers[i]);
    }
    return err;
}
