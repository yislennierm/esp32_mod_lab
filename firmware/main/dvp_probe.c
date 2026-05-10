#include "dvp_probe.h"

#include <string.h>

#include "esp_cam_ctlr.h"
#include "esp_cam_ctlr_dvp.h"
#include "esp_cam_ctlr_isp_dvp.h"
#include "esp_heap_caps.h"
#include "esp_private/esp_cache_private.h"
#include "esp_rom_gpio.h"
#include "driver/isp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "hal/cam_ll.h"
#include "hal/cam_ctlr_types.h"
#include "hal/cam_types.h"
#include "lcdcam_raw.h"
#include "pinmap_gbc.h"
#include "soc/cam_periph.h"

#define DVP_PROBE_H_RES 160U
#define DVP_PROBE_V_RES 144U
#define DVP_PROBE_FRAME_LEN (DVP_PROBE_H_RES * DVP_PROBE_V_RES)
#define DVP_PROBE_MAX_H_RES 320U
#define DVP_PROBE_MAX_V_RES 240U

typedef struct {
    esp_cam_ctlr_trans_t trans;
    SemaphoreHandle_t done_sem;
    size_t received_size;
} dvp_capture_context_t;

static bool IRAM_ATTR dvp_probe_get_new_trans(esp_cam_ctlr_handle_t handle, esp_cam_ctlr_trans_t *trans, void *user_data)
{
    (void)handle;
    dvp_capture_context_t *context = (dvp_capture_context_t *)user_data;
    trans->buffer = context->trans.buffer;
    trans->buflen = context->trans.buflen;
    return true;
}

static bool IRAM_ATTR dvp_probe_trans_finished(esp_cam_ctlr_handle_t handle, esp_cam_ctlr_trans_t *trans, void *user_data)
{
    (void)handle;
    BaseType_t high_task_woken = pdFALSE;
    dvp_capture_context_t *context = (dvp_capture_context_t *)user_data;
    context->received_size = trans->received_size;
    xSemaphoreGiveFromISR(context->done_sem, &high_task_woken);
    return high_task_woken == pdTRUE;
}

static void fill_raw8_pin_config(dvp_probe_de_source_t de_source, esp_cam_ctlr_dvp_pin_config_t *pin_config)
{
    memset(pin_config, 0, sizeof(*pin_config));
    pin_config->data_width = CAM_CTLR_DATA_WIDTH_8;
    for (size_t i = 0; i < CAM_DVP_DATA_SIG_NUM; ++i) {
        pin_config->data_io[i] = GPIO_NUM_NC;
    }

    /*
     * Temporary RAW8 red/green diagnostic packing:
     * bits 0-3 carry R2-R5 and bits 4-7 carry G2-G5. The generic DVP driver
     * is currently configured for RAW8, so full RGB666 cannot be captured in
     * one frame yet. Upper color bits are used first because they carry the
     * most visible image information.
     */
    pin_config->data_io[0] = GPIO_NUM_16; /* R2 */
    pin_config->data_io[1] = GPIO_NUM_15; /* R3 */
    pin_config->data_io[2] = GPIO_NUM_14; /* R4 */
    pin_config->data_io[3] = GPIO_NUM_13; /* R5 */
    pin_config->data_io[4] = GPIO_NUM_10; /* G2 */
    pin_config->data_io[5] = GPIO_NUM_9;  /* G3 */
    pin_config->data_io[6] = GPIO_NUM_8;  /* G4 */
    pin_config->data_io[7] = GPIO_NUM_7;  /* G5 */
    pin_config->pclk_io = GPIO_NUM_22;    /* DCLK */
    pin_config->vsync_io = GPIO_NUM_33;   /* SPS */
    pin_config->de_io = de_source == DVP_PROBE_DE_LP ? GPIO_NUM_21 : GPIO_NUM_19;
    pin_config->xclk_io = GPIO_NUM_NC;
}

static int sync_source_gpio(dvp_probe_sync_source_t source)
{
    switch (source) {
    case DVP_PROBE_SYNC_SPL:
        return GPIO_NUM_19;
    case DVP_PROBE_SYNC_LP:
        return GPIO_NUM_21;
    case DVP_PROBE_SYNC_NONE:
    default:
        return -1;
    }
}

static void override_input_polarity(dvp_probe_de_source_t de_source, bool vsync_invert, bool de_invert, bool pclk_invert)
{
    gpio_num_t de_gpio = de_source == DVP_PROBE_DE_LP ? GPIO_NUM_21 : GPIO_NUM_19;
    esp_rom_gpio_connect_in_signal(GPIO_NUM_33, cam_periph_signals.buses[0].vsync_sig, vsync_invert);
    esp_rom_gpio_connect_in_signal(de_gpio, cam_periph_signals.buses[0].de_sig, de_invert);
    esp_rom_gpio_connect_in_signal(GPIO_NUM_22, cam_periph_signals.buses[0].pclk_sig, pclk_invert);
}

static void summarize_buffer(dvp_probe_capture_result_t *result)
{
    result->checksum = 0;
    result->min_value = 0xff;
    result->max_value = 0x00;
    result->lower6_transitions = 0;
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
        if (i > 0 && ((value ^ result->buffer[i - 1]) & 0x3fU) != 0) {
            result->lower6_transitions++;
        }
        if (i > 0 && value != result->buffer[i - 1]) {
            result->raw8_transitions++;
        }
    }
}

esp_err_t dvp_probe_allocate_raw8(dvp_probe_alloc_result_t *result)
{
    if (result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(result, 0, sizeof(*result));
    result->controller_count = CAP_DVP_PERIPH_NUM;
    result->max_data_width = CAM_DVP_DATA_SIG_NUM;
    result->configured_width = CAM_CTLR_DATA_WIDTH_8;
    result->h_res = DVP_PROBE_H_RES;
    result->v_res = DVP_PROBE_V_RES;
    result->backup_buffer_disabled = true;

    esp_cam_ctlr_dvp_config_t config = {
        .ctlr_id = 0,
        .clk_src = CAM_CLK_SRC_DEFAULT,
        .h_res = result->h_res,
        .v_res = result->v_res,
        .input_data_color_type = CAM_CTLR_COLOR_RAW8,
        .dma_burst_size = 128,
        .byte_swap_en = false,
        .bk_buffer_dis = true,
        .pin_dont_init = true,
        .pic_format_jpeg = false,
    };

    esp_cam_ctlr_handle_t handle = NULL;
    esp_err_t err = esp_cam_new_dvp_ctlr(&config, &handle);
    if (err != ESP_OK) {
        return err;
    }

    size_t frame_buffer_len = 0;
    err = esp_cam_ctlr_get_frame_buffer_len(handle, &frame_buffer_len);
    esp_err_t del_err = esp_cam_ctlr_del(handle);
    if (err != ESP_OK) {
        return err;
    }
    if (del_err != ESP_OK) {
        return del_err;
    }

    result->frame_buffer_len = frame_buffer_len;
    return ESP_OK;
}

static esp_err_t dvp_probe_capture_raw8_common(dvp_probe_de_source_t de_source,
                                               uint32_t h_res,
                                               uint32_t v_res,
                                               uint32_t timeout_ms,
                                               bool vsync_invert,
                                               bool de_invert,
                                               bool pclk_invert,
                                               bool byte_count_eof,
                                               dvp_probe_capture_result_t *result)
{
    if (result == NULL ||
        h_res == 0 ||
        h_res > DVP_PROBE_MAX_H_RES ||
        v_res == 0 ||
        v_res > DVP_PROBE_MAX_V_RES ||
        timeout_ms == 0 ||
        timeout_ms > 5000) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(result, 0, sizeof(*result));
    result->de_source = de_source;
    result->timeout_ms = timeout_ms;
    result->buffer_len = h_res * v_res;
    result->vsync_invert = vsync_invert;
    result->de_invert = de_invert;
    result->pclk_invert = pclk_invert;
    result->byte_count_eof = byte_count_eof;

    size_t alignment_size = 0;
    esp_err_t err = esp_cache_get_alignment(MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA, &alignment_size);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t *buffer = heap_caps_aligned_calloc(alignment_size,
                                               1,
                                               result->buffer_len,
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        return ESP_ERR_NO_MEM;
    }

    SemaphoreHandle_t done_sem = xSemaphoreCreateBinary();
    if (done_sem == NULL) {
        heap_caps_free(buffer);
        return ESP_ERR_NO_MEM;
    }

    esp_cam_ctlr_dvp_pin_config_t pin_config = {0};
    fill_raw8_pin_config(de_source, &pin_config);

    esp_cam_ctlr_dvp_config_t config = {
        .ctlr_id = 0,
        .clk_src = CAM_CLK_SRC_DEFAULT,
        .h_res = h_res,
        .v_res = v_res,
        .input_data_color_type = CAM_CTLR_COLOR_RAW8,
        .dma_burst_size = 128,
        .byte_swap_en = false,
        .bk_buffer_dis = true,
        .pin_dont_init = false,
        .pic_format_jpeg = false,
        .pin = &pin_config,
    };

    esp_cam_ctlr_handle_t handle = NULL;
    dvp_capture_context_t context = {
        .trans = {
            .buffer = buffer,
            .buflen = result->buffer_len,
        },
        .done_sem = done_sem,
    };

    err = esp_cam_new_dvp_ctlr(&config, &handle);
    if (err != ESP_OK) {
        goto fail;
    }
    override_input_polarity(de_source, vsync_invert, de_invert, pclk_invert);

    esp_cam_ctlr_evt_cbs_t cbs = {
        .on_get_new_trans = dvp_probe_get_new_trans,
        .on_trans_finished = dvp_probe_trans_finished,
    };
    err = esp_cam_ctlr_register_event_callbacks(handle, &cbs, &context);
    if (err != ESP_OK) {
        goto fail;
    }

    err = esp_cam_ctlr_enable(handle);
    if (err != ESP_OK) {
        goto fail;
    }

    err = esp_cam_ctlr_start(handle);
    if (err != ESP_OK) {
        (void)esp_cam_ctlr_disable(handle);
        goto fail;
    }

    if (byte_count_eof) {
        lcd_cam_dev_t *hw = CAM_LL_GET_HW(0);
        cam_ll_enable_vsync_generate_eof(hw, false);
        cam_ll_set_recv_data_bytelen(hw, result->buffer_len - 1U);
        cam_ll_start(hw);
    }

    if (xSemaphoreTake(done_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        err = ESP_ERR_TIMEOUT;
    }

    (void)esp_cam_ctlr_stop(handle);
    (void)esp_cam_ctlr_disable(handle);

    if (err != ESP_OK) {
        goto fail;
    }

    result->buffer = buffer;
    result->received_size = context.received_size;
    summarize_buffer(result);

    (void)esp_cam_ctlr_del(handle);
    vSemaphoreDelete(done_sem);
    (void)lcdcam_raw_enter_electrical_isolate();
    return ESP_OK;

fail:
    if (handle != NULL) {
        (void)esp_cam_ctlr_del(handle);
    }
    vSemaphoreDelete(done_sem);
    heap_caps_free(buffer);
    memset(result, 0, sizeof(*result));
    (void)lcdcam_raw_enter_electrical_isolate();
    return err;
}

esp_err_t dvp_probe_capture_raw8(dvp_probe_de_source_t de_source,
                                 uint32_t h_res,
                                 uint32_t v_res,
                                 uint32_t timeout_ms,
                                 bool vsync_invert,
                                 bool de_invert,
                                 bool pclk_invert,
                                 dvp_probe_capture_result_t *result)
{
    return dvp_probe_capture_raw8_common(de_source,
                                         h_res,
                                         v_res,
                                         timeout_ms,
                                         vsync_invert,
                                         de_invert,
                                         pclk_invert,
                                         false,
                                         result);
}

esp_err_t dvp_probe_capture_raw8_byte_count(dvp_probe_de_source_t de_source,
                                            uint32_t h_res,
                                            uint32_t v_res,
                                            uint32_t timeout_ms,
                                            bool vsync_invert,
                                            bool de_invert,
                                            bool pclk_invert,
                                            dvp_probe_capture_result_t *result)
{
    return dvp_probe_capture_raw8_common(de_source,
                                         h_res,
                                         v_res,
                                         timeout_ms,
                                         vsync_invert,
                                         de_invert,
                                         pclk_invert,
                                         true,
                                         result);
}

void dvp_probe_capture_result_free(dvp_probe_capture_result_t *result)
{
    if (result == NULL) {
        return;
    }
    heap_caps_free(result->buffer);
    memset(result, 0, sizeof(*result));
}

static esp_err_t dvp_probe_capture_isp(dvp_probe_sync_source_t hsync_source,
                                       dvp_probe_sync_source_t de_source,
                                       uint32_t h_res,
                                       uint32_t v_res,
                                       uint32_t timeout_ms,
                                       bool hsync_invert,
                                       bool vsync_invert,
                                       bool de_invert,
                                       bool pclk_invert,
                                       bool output_rgb565,
                                       dvp_probe_capture_result_t *result)
{
    const char *stage = "validate_args";
    if (result == NULL ||
        h_res == 0 ||
        h_res > DVP_PROBE_MAX_H_RES ||
        v_res == 0 ||
        v_res > DVP_PROBE_MAX_V_RES ||
        timeout_ms == 0 ||
        timeout_ms > 5000) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(result, 0, sizeof(*result));
    result->timeout_ms = timeout_ms;
    result->buffer_len = h_res * v_res * (output_rgb565 ? 2U : 1U);
    result->vsync_invert = vsync_invert;
    result->de_invert = de_invert;
    result->pclk_invert = pclk_invert;
    result->failure_stage = "none";
    result->failure_err = ESP_OK;

    stage = "esp_cache_get_alignment";
    size_t alignment_size = 0;
    esp_err_t err = esp_cache_get_alignment(MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA, &alignment_size);
    if (err != ESP_OK) {
        result->failure_stage = stage;
        result->failure_err = err;
        return err;
    }

    stage = "heap_caps_aligned_calloc";
    uint8_t *buffer = heap_caps_aligned_calloc(alignment_size,
                                               1,
                                               result->buffer_len,
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        result->failure_stage = stage;
        result->failure_err = ESP_ERR_NO_MEM;
        return ESP_ERR_NO_MEM;
    }

    stage = "xSemaphoreCreateBinary";
    SemaphoreHandle_t done_sem = xSemaphoreCreateBinary();
    if (done_sem == NULL) {
        heap_caps_free(buffer);
        result->failure_stage = stage;
        result->failure_err = ESP_ERR_NO_MEM;
        return ESP_ERR_NO_MEM;
    }

    isp_proc_handle_t isp_proc = NULL;
    esp_cam_ctlr_handle_t handle = NULL;
    dvp_capture_context_t context = {
        .trans = {
            .buffer = buffer,
            .buflen = result->buffer_len,
        },
        .done_sem = done_sem,
    };
    esp_isp_processor_cfg_t isp_config = {
        .clk_hz = 80 * 1000 * 1000,
        .input_data_source = ISP_INPUT_DATA_SOURCE_DVP,
        .input_data_color_type = ISP_COLOR_RAW8,
        .output_data_color_type = output_rgb565 ? ISP_COLOR_RGB565 : ISP_COLOR_RAW8,
        .has_line_start_packet = false,
        .has_line_end_packet = false,
        .h_res = h_res,
        .v_res = v_res,
        .flags.bypass_isp = false,
    };

    stage = "esp_isp_new_processor";
    err = esp_isp_new_processor(&isp_config, &isp_proc);
    if (err != ESP_OK) {
        goto fail;
    }
    stage = "esp_isp_enable";
    err = esp_isp_enable(isp_proc);
    if (err != ESP_OK) {
        goto fail;
    }

    esp_cam_ctlr_isp_dvp_cfg_t dvp_config = {
        .data_width = CAM_CTLR_DATA_WIDTH_8,
        .data_io = {
            GPIO_NUM_16, /* R2 */
            GPIO_NUM_15, /* R3 */
            GPIO_NUM_14, /* R4 */
            GPIO_NUM_13, /* R5 */
            GPIO_NUM_10, /* G2 */
            GPIO_NUM_9,  /* G3 */
            GPIO_NUM_8,  /* G4 */
            GPIO_NUM_7,  /* G5 */
            -1, -1, -1, -1, -1, -1, -1, -1,
        },
        .pclk_io = GPIO_NUM_22,
        .hsync_io = sync_source_gpio(hsync_source),
        .vsync_io = GPIO_NUM_33,
        .de_io = sync_source_gpio(de_source),
        .io_flags = {
            .pclk_invert = pclk_invert,
            .hsync_invert = hsync_invert,
            .vsync_invert = vsync_invert,
            .de_invert = de_invert,
        },
        .queue_items = 2,
        .bk_buffer_dis = true,
    };

    stage = "esp_cam_new_isp_dvp_ctlr";
    err = esp_cam_new_isp_dvp_ctlr(isp_proc, &dvp_config, &handle);
    if (err != ESP_OK) {
        goto fail;
    }
    esp_cam_ctlr_evt_cbs_t cbs = {
        .on_get_new_trans = dvp_probe_get_new_trans,
        .on_trans_finished = dvp_probe_trans_finished,
    };
    stage = "esp_cam_ctlr_register_event_callbacks";
    err = esp_cam_ctlr_register_event_callbacks(handle, &cbs, &context);
    if (err != ESP_OK) {
        goto fail;
    }
    stage = "esp_cam_ctlr_enable";
    err = esp_cam_ctlr_enable(handle);
    if (err != ESP_OK) {
        goto fail;
    }
    stage = "esp_cam_ctlr_start";
    err = esp_cam_ctlr_start(handle);
    if (err != ESP_OK) {
        goto fail;
    }

    stage = "frame_done_timeout";
    if (xSemaphoreTake(done_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        stage = "frame_done_timeout";
        err = ESP_ERR_TIMEOUT;
    } else {
        err = ESP_OK;
    }
    (void)esp_cam_ctlr_stop(handle);
    (void)esp_cam_ctlr_disable(handle);
    if (err != ESP_OK) {
        goto fail;
    }

    result->buffer = buffer;
    result->received_size = context.received_size;
    result->failure_stage = "none";
    result->failure_err = ESP_OK;
    summarize_buffer(result);

    (void)esp_cam_ctlr_del(handle);
    (void)esp_isp_disable(isp_proc);
    (void)esp_isp_del_processor(isp_proc);
    vSemaphoreDelete(done_sem);
    (void)lcdcam_raw_enter_electrical_isolate();
    return ESP_OK;

fail:
    if (handle != NULL) {
        (void)esp_cam_ctlr_stop(handle);
        (void)esp_cam_ctlr_disable(handle);
        (void)esp_cam_ctlr_del(handle);
    }
    if (isp_proc != NULL) {
        (void)esp_isp_disable(isp_proc);
        (void)esp_isp_del_processor(isp_proc);
    }
    vSemaphoreDelete(done_sem);
    heap_caps_free(buffer);
    result->buffer = NULL;
    result->received_size = context.received_size;
    result->failure_stage = stage;
    result->failure_err = err;
    (void)lcdcam_raw_enter_electrical_isolate();
    return err;
}

esp_err_t dvp_probe_capture_isp_raw8(dvp_probe_sync_source_t hsync_source,
                                     dvp_probe_sync_source_t de_source,
                                     uint32_t h_res,
                                     uint32_t v_res,
                                     uint32_t timeout_ms,
                                     bool hsync_invert,
                                     bool vsync_invert,
                                     bool de_invert,
                                     bool pclk_invert,
                                     dvp_probe_capture_result_t *result)
{
    return dvp_probe_capture_isp(hsync_source,
                                 de_source,
                                 h_res,
                                 v_res,
                                 timeout_ms,
                                 hsync_invert,
                                 vsync_invert,
                                 de_invert,
                                 pclk_invert,
                                 false,
                                 result);
}

esp_err_t dvp_probe_capture_isp_rgb565(dvp_probe_sync_source_t hsync_source,
                                       dvp_probe_sync_source_t de_source,
                                       uint32_t h_res,
                                       uint32_t v_res,
                                       uint32_t timeout_ms,
                                       bool hsync_invert,
                                       bool vsync_invert,
                                       bool de_invert,
                                       bool pclk_invert,
                                       dvp_probe_capture_result_t *result)
{
    return dvp_probe_capture_isp(hsync_source,
                                 de_source,
                                 h_res,
                                 v_res,
                                 timeout_ms,
                                 hsync_invert,
                                 vsync_invert,
                                 de_invert,
                                 pclk_invert,
                                 true,
                                 result);
}
