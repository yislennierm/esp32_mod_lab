#include "destination_spi_lcd.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "destination_gpio_lab.h"
#include "pinmap_current.h"

#define DEST_SPI_LCD_HOST SPI2_HOST
#define DEST_SPI_LCD_GPIO_CS SPI_LCD_GPIO_CS
#define DEST_SPI_LCD_GPIO_RESET SPI_LCD_GPIO_RESET
#define DEST_SPI_LCD_GPIO_DC SPI_LCD_GPIO_DC
#define DEST_SPI_LCD_GPIO_MOSI SPI_LCD_GPIO_MOSI
#define DEST_SPI_LCD_GPIO_SCLK SPI_LCD_GPIO_SCLK
#define DEST_SPI_LCD_H_RES 320
#define DEST_SPI_LCD_V_RES 480
#ifndef DEST_SPI_LCD_PCLK_HZ
#define DEST_SPI_LCD_PCLK_HZ 20000000
#endif
#define DEST_SPI_LCD_SPI_MODE 0
#define DEST_SPI_LCD_CMD_BITS 8
#define DEST_SPI_LCD_PARAM_BITS 8
#define DEST_SPI_LCD_TRANSFER_LINES 20
#define DEST_SPI_LCD_CONTROLLER "st7796s_ili9486_mipi_dcs"
#ifndef DEST_SPI_LCD_RAW_SPI
#define DEST_SPI_LCD_RAW_SPI 0
#endif
#define DEST_SPI_LCD_MADCTL_SOURCE_STRAIGHT 0x08
#define DEST_SPI_LCD_MADCTL_PANEL_CORRECTED 0xE8
#define DEST_SPI_LCD_DEFAULT_MADCTL DEST_SPI_LCD_MADCTL_PANEL_CORRECTED
#define DEST_SPI_LCD_MADCTL_MV 0x20
#define FPS_OVERLAY_SCALE 2
#define FPS_OVERLAY_CHAR_W 5
#define FPS_OVERLAY_CHAR_H 7
#define FPS_OVERLAY_SPACING 1

typedef enum {
    DEST_SPI_LCD_STATE_SAFE_OFF = 0,
    DEST_SPI_LCD_STATE_INITIALIZED,
    DEST_SPI_LCD_STATE_ERROR,
} destination_spi_lcd_state_t;

static esp_lcd_panel_io_handle_t s_io_handle;
static spi_device_handle_t s_spi_handle;
static SemaphoreHandle_t s_color_done_sem;
static bool s_bus_initialized;
static bool s_reset_gpio_configured;
static destination_spi_lcd_state_t s_state = DEST_SPI_LCD_STATE_SAFE_OFF;
static esp_err_t s_last_err = ESP_OK;
static char s_last_action[32] = "none";
static uint8_t s_madctl = DEST_SPI_LCD_DEFAULT_MADCTL;
static int s_effective_pclk_hz = DEST_SPI_LCD_PCLK_HZ;

static esp_err_t fill_rect666(int x_start, int y_start, int width, int height, uint8_t red, uint8_t green, uint8_t blue);

static bool IRAM_ATTR on_color_trans_done(esp_lcd_panel_io_handle_t panel_io,
                                          esp_lcd_panel_io_event_data_t *edata,
                                          void *user_ctx)
{
    (void)panel_io;
    (void)edata;
    SemaphoreHandle_t sem = (SemaphoreHandle_t)user_ctx;
    if (sem == NULL) {
        return false;
    }
    BaseType_t high_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(sem, &high_task_woken);
    return high_task_woken == pdTRUE;
}

static esp_err_t tx_color_sync(int lcd_cmd, const void *payload, size_t payload_len)
{
    if (s_io_handle == NULL || s_color_done_sem == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    (void)xSemaphoreTake(s_color_done_sem, 0);
    esp_err_t err = esp_lcd_panel_io_tx_color(s_io_handle, lcd_cmd, payload, payload_len);
    if (err != ESP_OK) {
        return err;
    }
    return xSemaphoreTake(s_color_done_sem, pdMS_TO_TICKS(500)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

static int logical_h_res(void)
{
    return (s_madctl & DEST_SPI_LCD_MADCTL_MV) ? DEST_SPI_LCD_V_RES : DEST_SPI_LCD_H_RES;
}

static int logical_v_res(void)
{
    return (s_madctl & DEST_SPI_LCD_MADCTL_MV) ? DEST_SPI_LCD_H_RES : DEST_SPI_LCD_V_RES;
}

static const char *state_name(void)
{
    switch (s_state) {
    case DEST_SPI_LCD_STATE_SAFE_OFF:
        return "safe_off";
    case DEST_SPI_LCD_STATE_INITIALIZED:
        return "initialized";
    case DEST_SPI_LCD_STATE_ERROR:
        return "error";
    default:
        return "unknown";
    }
}

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)((r >> 3) << 11) | (uint16_t)((g >> 2) << 5) | (uint16_t)(b >> 3);
}

static void rgb565_le_to_rgb888(uint8_t low, uint8_t high, uint8_t *red, uint8_t *green, uint8_t *blue)
{
    const uint16_t word = (uint16_t)low | ((uint16_t)high << 8);
    const uint8_t blue5 = word & 0x1f;
    const uint8_t green6 = (word >> 5) & 0x3f;
    const uint8_t red5 = (word >> 11) & 0x1f;
    *red = (uint8_t)((red5 * 255U + 15U) / 31U);
    *green = (uint8_t)((green6 * 255U + 31U) / 63U);
    *blue = (uint8_t)((blue5 * 255U + 15U) / 31U);
}

static void rgb565_le_to_be(uint8_t low, uint8_t high, uint8_t *out_high, uint8_t *out_low)
{
    *out_high = high;
    *out_low = low;
}

static void remember(const char *action, esp_err_t err)
{
    strlcpy(s_last_action, action, sizeof(s_last_action));
    s_last_err = err;
    if (err != ESP_OK) {
        s_state = DEST_SPI_LCD_STATE_ERROR;
    }
}

static void print_result(const char *command, esp_err_t err)
{
    printf("{\"ok\":%s,\"command\":\"%s\",\"state\":\"%s\",\"error\":\"%s\",\"err\":%d,"
           "\"controller\":\"%s\",\"width\":%d,\"height\":%d,"
           "\"spi\":{\"host\":\"SPI2\",\"pclk_hz\":%d,\"mode\":%d},"
           "\"pins\":{\"cs\":%d,\"reset\":%d,\"dc\":%d,\"mosi\":%d,\"sclk\":%d}}\n",
           err == ESP_OK ? "true" : "false",
           command,
           state_name(),
           err == ESP_OK ? "none" : esp_err_to_name(err),
           err,
           DEST_SPI_LCD_CONTROLLER,
           DEST_SPI_LCD_H_RES,
           DEST_SPI_LCD_V_RES,
           s_effective_pclk_hz,
           DEST_SPI_LCD_SPI_MODE,
           DEST_SPI_LCD_GPIO_CS,
           DEST_SPI_LCD_GPIO_RESET,
           DEST_SPI_LCD_GPIO_DC,
           DEST_SPI_LCD_GPIO_MOSI,
           DEST_SPI_LCD_GPIO_SCLK);
}

static void release_reset_gpio(void)
{
    if (!s_reset_gpio_configured) {
        return;
    }
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << DEST_SPI_LCD_GPIO_RESET,
        .mode = GPIO_MODE_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    (void)gpio_config(&io_conf);
    s_reset_gpio_configured = false;
}

static void cleanup_silent(void)
{
    if (s_io_handle != NULL) {
        (void)esp_lcd_panel_io_tx_param(s_io_handle, 0x28, NULL, 0); // DISP OFF
        (void)esp_lcd_panel_io_del(s_io_handle);
        s_io_handle = NULL;
    }
    if (s_spi_handle != NULL) {
        (void)spi_bus_remove_device(s_spi_handle);
        s_spi_handle = NULL;
    }
    if (s_bus_initialized) {
        (void)spi_bus_free(DEST_SPI_LCD_HOST);
        s_bus_initialized = false;
    }
    if (s_color_done_sem != NULL) {
        vSemaphoreDelete(s_color_done_sem);
        s_color_done_sem = NULL;
    }
    release_reset_gpio();
    destination_gpio_lab_release_all();
}

static esp_err_t send_cmd(uint8_t cmd, const void *params, size_t param_len)
{
    if (s_spi_handle != NULL) {
        spi_transaction_t trans = {
            .length = 8,
            .tx_buffer = &cmd,
        };
        (void)gpio_set_level(DEST_SPI_LCD_GPIO_DC, 0);
        esp_err_t err = spi_device_polling_transmit(s_spi_handle, &trans);
        if (err != ESP_OK || param_len == 0) {
            return err;
        }

        trans.length = param_len * 8U;
        trans.tx_buffer = params;
        (void)gpio_set_level(DEST_SPI_LCD_GPIO_DC, 1);
        return spi_device_polling_transmit(s_spi_handle, &trans);
    }
    if (s_io_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_lcd_panel_io_tx_param(s_io_handle, cmd, params, param_len);
}

static esp_err_t reset_panel(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << DEST_SPI_LCD_GPIO_RESET,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        return err;
    }
    s_reset_gpio_configured = true;

    (void)gpio_set_level(DEST_SPI_LCD_GPIO_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    (void)gpio_set_level(DEST_SPI_LCD_GPIO_RESET, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    (void)gpio_set_level(DEST_SPI_LCD_GPIO_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
}

static esp_err_t panel_init_sequence(void)
{
    esp_err_t err = reset_panel();
    if (err != ESP_OK) {
        return err;
    }

    err = send_cmd(0x01, NULL, 0); // SWRESET
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(120));

    err = send_cmd(0x11, NULL, 0); // SLPOUT
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(120));

    const uint8_t rgb666_colmod = 0x66;
    err = send_cmd(0x3A, &rgb666_colmod, sizeof(rgb666_colmod)); // COLMOD: 18-bit/pixel
    if (err != ESP_OK) {
        return err;
    }

    s_madctl = DEST_SPI_LCD_DEFAULT_MADCTL;
    err = send_cmd(0x36, &s_madctl, sizeof(s_madctl)); // MADCTL: destination orientation and BGR order
    if (err != ESP_OK) {
        return err;
    }

    err = send_cmd(0x20, NULL, 0); // INVOFF
    if (err != ESP_OK) {
        return err;
    }

    err = send_cmd(0x29, NULL, 0); // DISPON
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
    return ESP_OK;
}

static esp_err_t set_addr_window(int x_start, int y_start, int x_end, int y_end)
{
    const int h_res = logical_h_res();
    const int v_res = logical_v_res();
    if (x_start < 0 || y_start < 0 || x_end <= x_start || y_end <= y_start ||
        x_end > h_res || y_end > v_res) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint16_t x0 = (uint16_t)x_start;
    const uint16_t x1 = (uint16_t)(x_end - 1);
    const uint16_t y0 = (uint16_t)y_start;
    const uint16_t y1 = (uint16_t)(y_end - 1);
    const uint8_t col[] = {
        (uint8_t)(x0 >> 8),
        (uint8_t)(x0 & 0xff),
        (uint8_t)(x1 >> 8),
        (uint8_t)(x1 & 0xff),
    };
    const uint8_t row[] = {
        (uint8_t)(y0 >> 8),
        (uint8_t)(y0 & 0xff),
        (uint8_t)(y1 >> 8),
        (uint8_t)(y1 & 0xff),
    };

    esp_err_t err = send_cmd(0x2A, col, sizeof(col)); // CASET
    if (err != ESP_OK) {
        return err;
    }
    return send_cmd(0x2B, row, sizeof(row)); // RASET
}

static esp_err_t draw_pixels(int x_start, int y_start, int x_end, int y_end, const uint16_t *pixels)
{
    if (s_state != DEST_SPI_LCD_STATE_INITIALIZED || (s_io_handle == NULL && s_spi_handle == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = set_addr_window(x_start, y_start, x_end, y_end);
    if (err != ESP_OK) {
        return err;
    }
    const size_t pixel_count = (size_t)(x_end - x_start) * (size_t)(y_end - y_start);
    if (s_spi_handle != NULL) {
        const uint8_t ramwr = 0x2C;
        spi_transaction_t trans = {
            .length = 8,
            .tx_buffer = &ramwr,
        };
        (void)gpio_set_level(DEST_SPI_LCD_GPIO_DC, 0);
        esp_err_t err = spi_device_polling_transmit(s_spi_handle, &trans);
        if (err != ESP_OK) {
            return err;
        }
        trans.length = pixel_count * sizeof(uint16_t) * 8U;
        trans.tx_buffer = pixels;
        (void)gpio_set_level(DEST_SPI_LCD_GPIO_DC, 1);
        return spi_device_polling_transmit(s_spi_handle, &trans);
    }
    return tx_color_sync(0x2C, pixels, pixel_count * sizeof(uint16_t)); // RAMWR
}

void destination_spi_lcd_handle_status(void)
{
    printf("{\"ok\":true,\"command\":\"DEST_SPI_LCD_STATUS\",\"state\":\"%s\","
           "\"last_action\":\"%s\",\"last_error\":\"%s\",\"last_err\":%d,"
           "\"controller\":\"%s\",\"width\":%d,\"height\":%d,\"logical_width\":%d,\"logical_height\":%d,"
           "\"madctl\":\"0x%02x\","
           "\"spi\":{\"host\":\"SPI2\",\"pclk_hz\":%d,\"mode\":%d,\"cmd_bits\":%d,\"param_bits\":%d,\"transfer_lines\":%d},"
           "\"pins\":{\"cs\":%d,\"reset\":%d,\"dc\":%d,\"mosi\":%d,\"sclk\":%d}}\n",
           state_name(),
           s_last_action,
           s_last_err == ESP_OK ? "none" : esp_err_to_name(s_last_err),
           s_last_err,
           DEST_SPI_LCD_CONTROLLER,
           DEST_SPI_LCD_H_RES,
           DEST_SPI_LCD_V_RES,
           logical_h_res(),
           logical_v_res(),
           s_madctl,
           s_effective_pclk_hz,
           DEST_SPI_LCD_SPI_MODE,
           DEST_SPI_LCD_CMD_BITS,
           DEST_SPI_LCD_PARAM_BITS,
           DEST_SPI_LCD_TRANSFER_LINES,
           DEST_SPI_LCD_GPIO_CS,
           DEST_SPI_LCD_GPIO_RESET,
           DEST_SPI_LCD_GPIO_DC,
           DEST_SPI_LCD_GPIO_MOSI,
           DEST_SPI_LCD_GPIO_SCLK);
}

void destination_spi_lcd_handle_safe_off(void)
{
    destination_spi_lcd_safe_off();
    print_result("DEST_SPI_LCD_SAFE_OFF", ESP_OK);
}

void destination_spi_lcd_safe_off(void)
{
    cleanup_silent();
    s_state = DEST_SPI_LCD_STATE_SAFE_OFF;
    remember("safe_off", ESP_OK);
}

esp_err_t destination_spi_lcd_init(void)
{
    destination_gpio_lab_release_all();
    if (s_state == DEST_SPI_LCD_STATE_INITIALIZED) {
        return ESP_OK;
    }

    spi_bus_config_t bus_config = {
        .sclk_io_num = DEST_SPI_LCD_GPIO_SCLK,
        .mosi_io_num = DEST_SPI_LCD_GPIO_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .data4_io_num = GPIO_NUM_NC,
        .data5_io_num = GPIO_NUM_NC,
        .data6_io_num = GPIO_NUM_NC,
        .data7_io_num = GPIO_NUM_NC,
        .max_transfer_sz = DEST_SPI_LCD_V_RES * DEST_SPI_LCD_TRANSFER_LINES * 3,
        .flags = SPICOMMON_BUSFLAG_MASTER,
    };
    esp_err_t err = spi_bus_initialize(DEST_SPI_LCD_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        remember("spi_bus_initialize", err);
        return err;
    }
    s_bus_initialized = true;

#if DEST_SPI_LCD_RAW_SPI
    gpio_config_t dc_conf = {
        .pin_bit_mask = 1ULL << DEST_SPI_LCD_GPIO_DC,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&dc_conf);
    if (err != ESP_OK) {
        cleanup_silent();
        remember("dc_gpio_config", err);
        return err;
    }

    static const int pclk_candidates[] = {DEST_SPI_LCD_PCLK_HZ};
    err = ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < sizeof(pclk_candidates) / sizeof(pclk_candidates[0]); ++i) {
        spi_device_interface_config_t dev_config = {
            .clock_speed_hz = pclk_candidates[i],
            .clock_source = SPI_CLK_SRC_SPLL,
            .mode = DEST_SPI_LCD_SPI_MODE,
            .spics_io_num = DEST_SPI_LCD_GPIO_CS,
            .queue_size = 4,
            .flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_NO_DUMMY,
        };
        err = spi_bus_add_device(DEST_SPI_LCD_HOST, &dev_config, &s_spi_handle);
        if (err == ESP_OK) {
            s_effective_pclk_hz = pclk_candidates[i];
            break;
        }
    }
    if (err != ESP_OK) {
        cleanup_silent();
        remember("spi_bus_add_device_raw", err);
        return err;
    }
#else
    s_color_done_sem = xSemaphoreCreateBinary();
    if (s_color_done_sem == NULL) {
        cleanup_silent();
        remember("color_done_sem", ESP_ERR_NO_MEM);
        return ESP_ERR_NO_MEM;
    }
    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = DEST_SPI_LCD_GPIO_CS,
        .dc_gpio_num = DEST_SPI_LCD_GPIO_DC,
        .pclk_hz = DEST_SPI_LCD_PCLK_HZ,
        .spi_mode = DEST_SPI_LCD_SPI_MODE,
        .trans_queue_depth = 1,
        .lcd_cmd_bits = DEST_SPI_LCD_CMD_BITS,
        .lcd_param_bits = DEST_SPI_LCD_PARAM_BITS,
        .on_color_trans_done = on_color_trans_done,
        .user_ctx = s_color_done_sem,
    };
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)DEST_SPI_LCD_HOST, &io_config, &s_io_handle);
    if (err != ESP_OK) {
        cleanup_silent();
        remember("esp_lcd_new_panel_io_spi", err);
        return err;
    }
    s_effective_pclk_hz = DEST_SPI_LCD_PCLK_HZ;
#endif

    err = panel_init_sequence();
    if (err != ESP_OK) {
        cleanup_silent();
        remember("mipi_dcs_init_sequence", err);
        return err;
    }

    s_state = DEST_SPI_LCD_STATE_INITIALIZED;
    remember("init", ESP_OK);
    return ESP_OK;
}

void destination_spi_lcd_handle_init(void)
{
    esp_err_t err = destination_spi_lcd_init();
    print_result("DEST_SPI_LCD_INIT", err);
}

static esp_err_t fill_screen(uint16_t color)
{
    if (s_state != DEST_SPI_LCD_STATE_INITIALIZED || (s_io_handle == NULL && s_spi_handle == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }
    const size_t pixels = DEST_SPI_LCD_H_RES * DEST_SPI_LCD_TRANSFER_LINES;
    uint16_t *linebuf = heap_caps_malloc(pixels * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (linebuf == NULL) {
        return ESP_ERR_NO_MEM;
    }
    for (size_t i = 0; i < pixels; ++i) {
        linebuf[i] = color;
    }
    esp_err_t err = ESP_OK;
    for (int y = 0; y < DEST_SPI_LCD_V_RES && err == ESP_OK; y += DEST_SPI_LCD_TRANSFER_LINES) {
        int y_end = y + DEST_SPI_LCD_TRANSFER_LINES;
        if (y_end > DEST_SPI_LCD_V_RES) {
            y_end = DEST_SPI_LCD_V_RES;
        }
        err = draw_pixels(0, y, DEST_SPI_LCD_H_RES, y_end, linebuf);
    }
    free(linebuf);
    return err;
}

esp_err_t destination_spi_lcd_clear_black(void)
{
    return fill_rect666(0, 0, logical_h_res(), logical_v_res(), 0, 0, 0);
}

void destination_spi_lcd_handle_clear(const char *line)
{
    unsigned color = 0;
    if (sscanf(line, "DEST_SPI_LCD_CLEAR %x", &color) != 1) {
        color = 0x0000;
    }
    esp_err_t err = fill_screen((uint16_t)color);
    remember("clear", err);
    print_result("DEST_SPI_LCD_CLEAR", err);
}

static esp_err_t draw_raw_bytes(int x_start, int y_start, int x_end, int y_end, const uint8_t *payload, size_t payload_len)
{
    if (s_state != DEST_SPI_LCD_STATE_INITIALIZED || (s_io_handle == NULL && s_spi_handle == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = set_addr_window(x_start, y_start, x_end, y_end);
    if (err != ESP_OK) {
        return err;
    }
    if (s_spi_handle != NULL) {
        const uint8_t ramwr = 0x2C;
        spi_transaction_t trans = {
            .length = 8,
            .tx_buffer = &ramwr,
        };
        (void)gpio_set_level(DEST_SPI_LCD_GPIO_DC, 0);
        err = spi_device_polling_transmit(s_spi_handle, &trans);
        if (err != ESP_OK) {
            return err;
        }
        trans.length = payload_len * 8U;
        trans.tx_buffer = payload;
        (void)gpio_set_level(DEST_SPI_LCD_GPIO_DC, 1);
        return spi_device_polling_transmit(s_spi_handle, &trans);
    }
    return tx_color_sync(0x2C, payload, payload_len);
}

static esp_err_t fill_rect666(int x_start, int y_start, int width, int height, uint8_t red, uint8_t green, uint8_t blue)
{
    if (width <= 0 || height <= 0) {
        return ESP_OK;
    }
    if (s_state != DEST_SPI_LCD_STATE_INITIALIZED || (s_io_handle == NULL && s_spi_handle == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t colmod = 0x66;
    esp_err_t err = send_cmd(0x3A, &colmod, sizeof(colmod));
    if (err != ESP_OK) {
        return err;
    }

    const int lines_per_transfer = DEST_SPI_LCD_TRANSFER_LINES;
    const size_t pixels = (size_t)width * (size_t)lines_per_transfer;
    uint8_t *linebuf = heap_caps_malloc(pixels * 3, MALLOC_CAP_DMA);
    if (linebuf == NULL) {
        return ESP_ERR_NO_MEM;
    }
    for (size_t i = 0; i < pixels; ++i) {
        linebuf[(i * 3) + 0] = red;
        linebuf[(i * 3) + 1] = green;
        linebuf[(i * 3) + 2] = blue;
    }

    for (int y = y_start; y < y_start + height && err == ESP_OK; y += lines_per_transfer) {
        int y_end = y + lines_per_transfer;
        if (y_end > y_start + height) {
            y_end = y_start + height;
        }
        err = draw_raw_bytes(x_start, y, x_start + width, y_end, linebuf, (size_t)(y_end - y) * (size_t)width * 3U);
    }
    free(linebuf);
    return err;
}

void destination_spi_lcd_handle_clear565be(const char *line)
{
    unsigned color = 0;
    if (sscanf(line, "DEST_SPI_LCD_CLEAR565BE %x", &color) != 1) {
        color = 0x0000;
    }
    if (s_state != DEST_SPI_LCD_STATE_INITIALIZED || (s_io_handle == NULL && s_spi_handle == NULL)) {
        esp_err_t err = ESP_ERR_INVALID_STATE;
        remember("clear565be", err);
        print_result("DEST_SPI_LCD_CLEAR565BE", err);
        return;
    }

    const uint8_t colmod = 0x55;
    esp_err_t err = send_cmd(0x3A, &colmod, sizeof(colmod));
    const size_t pixels = DEST_SPI_LCD_H_RES * DEST_SPI_LCD_TRANSFER_LINES;
    uint8_t *linebuf = NULL;
    if (err == ESP_OK) {
        linebuf = heap_caps_malloc(pixels * 2, MALLOC_CAP_DMA);
        if (linebuf == NULL) {
            err = ESP_ERR_NO_MEM;
        }
    }
    if (err == ESP_OK) {
        const uint8_t high = (uint8_t)((color >> 8) & 0xff);
        const uint8_t low = (uint8_t)(color & 0xff);
        for (size_t i = 0; i < pixels; ++i) {
            linebuf[(i * 2) + 0] = high;
            linebuf[(i * 2) + 1] = low;
        }
        for (int y = 0; y < DEST_SPI_LCD_V_RES && err == ESP_OK; y += DEST_SPI_LCD_TRANSFER_LINES) {
            int y_end = y + DEST_SPI_LCD_TRANSFER_LINES;
            if (y_end > DEST_SPI_LCD_V_RES) {
                y_end = DEST_SPI_LCD_V_RES;
            }
            err = draw_raw_bytes(0, y, DEST_SPI_LCD_H_RES, y_end, linebuf, (size_t)(y_end - y) * DEST_SPI_LCD_H_RES * 2);
        }
    }
    free(linebuf);
    remember("clear565be", err);
    print_result("DEST_SPI_LCD_CLEAR565BE", err);
}

void destination_spi_lcd_handle_clear666(const char *line)
{
    unsigned color = 0;
    if (sscanf(line, "DEST_SPI_LCD_CLEAR666 %x", &color) != 1) {
        color = 0x000000;
    }
    if (s_state != DEST_SPI_LCD_STATE_INITIALIZED || (s_io_handle == NULL && s_spi_handle == NULL)) {
        esp_err_t err = ESP_ERR_INVALID_STATE;
        remember("clear666", err);
        print_result("DEST_SPI_LCD_CLEAR666", err);
        return;
    }

    const uint8_t colmod = 0x66;
    esp_err_t err = send_cmd(0x3A, &colmod, sizeof(colmod));
    const size_t pixels = DEST_SPI_LCD_H_RES * DEST_SPI_LCD_TRANSFER_LINES;
    uint8_t *linebuf = NULL;
    if (err == ESP_OK) {
        linebuf = heap_caps_malloc(pixels * 3, MALLOC_CAP_DMA);
        if (linebuf == NULL) {
            err = ESP_ERR_NO_MEM;
        }
    }
    if (err == ESP_OK) {
        const uint8_t red = (uint8_t)((color >> 16) & 0xff);
        const uint8_t green = (uint8_t)((color >> 8) & 0xff);
        const uint8_t blue = (uint8_t)(color & 0xff);
        for (size_t i = 0; i < pixels; ++i) {
            linebuf[(i * 3) + 0] = red;
            linebuf[(i * 3) + 1] = green;
            linebuf[(i * 3) + 2] = blue;
        }
        for (int y = 0; y < DEST_SPI_LCD_V_RES && err == ESP_OK; y += DEST_SPI_LCD_TRANSFER_LINES) {
            int y_end = y + DEST_SPI_LCD_TRANSFER_LINES;
            if (y_end > DEST_SPI_LCD_V_RES) {
                y_end = DEST_SPI_LCD_V_RES;
            }
            err = draw_raw_bytes(0, y, DEST_SPI_LCD_H_RES, y_end, linebuf, (size_t)(y_end - y) * DEST_SPI_LCD_H_RES * 3);
        }
    }
    free(linebuf);
    remember("clear666", err);
    print_result("DEST_SPI_LCD_CLEAR666", err);
}

static bool seven_segment_digit_pixel(int x, int y, int width, int height, int digit)
{
    static const uint8_t segment_masks[10] = {
        0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f,
    };
    if (digit < 0 || digit > 9) {
        return false;
    }

    const int box_w = 96;
    const int box_h = 144;
    const int thickness = 14;
    const int margin = 14;
    const int x0 = (width - box_w) / 2;
    const int y0 = (height - box_h) / 2;
    const int lx = x - x0;
    const int ly = y - y0;
    if (lx < 0 || ly < 0 || lx >= box_w || ly >= box_h) {
        return false;
    }

    const bool top = ly >= margin && ly < margin + thickness &&
                     lx >= margin && lx < box_w - margin;
    const bool upper_right = lx >= box_w - margin - thickness && lx < box_w - margin &&
                             ly >= margin && ly < (box_h / 2);
    const bool lower_right = lx >= box_w - margin - thickness && lx < box_w - margin &&
                             ly >= (box_h / 2) && ly < box_h - margin;
    const bool bottom = ly >= box_h - margin - thickness && ly < box_h - margin &&
                        lx >= margin && lx < box_w - margin;
    const bool lower_left = lx >= margin && lx < margin + thickness &&
                            ly >= (box_h / 2) && ly < box_h - margin;
    const bool upper_left = lx >= margin && lx < margin + thickness &&
                            ly >= margin && ly < (box_h / 2);
    const bool middle = ly >= (box_h / 2) - (thickness / 2) &&
                        ly < (box_h / 2) + (thickness / 2) &&
                        lx >= margin && lx < box_w - margin;
    const bool segments[] = {top, upper_right, lower_right, bottom, lower_left, upper_left, middle};
    const uint8_t mask = segment_masks[digit];
    for (int i = 0; i < 7; ++i) {
        if ((mask & (1U << i)) && segments[i]) {
            return true;
        }
    }
    return false;
}

size_t destination_spi_lcd_rgb666_frame_size(void)
{
    return (size_t)logical_h_res() * (size_t)logical_v_res() * 3U;
}

esp_err_t destination_spi_lcd_generate_geometry_rgb666(uint8_t *frame, size_t frame_len, int label_digit)
{
    if (frame == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const int width = logical_h_res();
    const int height = logical_v_res();
    const size_t required = (size_t)width * (size_t)height * 3U;
    if (frame_len < required) {
        return ESP_ERR_INVALID_SIZE;
    }

    for (int row = 0; row < height; ++row) {
        for (int x = 0; x < width; ++x) {
            uint8_t red = 0;
            uint8_t green = 0;
            uint8_t blue = 0;
            const bool border = x < 4 || x >= (width - 4) ||
                                row < 4 || row >= (height - 4);
            const bool cross = x >= (width / 2 - 1) && x <= (width / 2 + 1);
            const bool midline = row >= (height / 2 - 1) && row <= (height / 2 + 1);
            const bool grid = (x % 40) == 0 || (row % 40) == 0;
            if (seven_segment_digit_pixel(x, row, width, height, label_digit)) {
                red = 255;
                green = 128;
                blue = 0;
            } else if (border) {
                red = 255;
                green = 255;
                blue = 255;
            } else if (x < 64 && row < 64) {
                red = 255;
            } else if (x >= width - 64 && row < 64) {
                green = 255;
            } else if (x < 64 && row >= height - 64) {
                blue = 255;
            } else if (x >= width - 64 && row >= height - 64) {
                red = 255;
                green = 255;
            } else if (cross || midline) {
                red = 255;
                green = 255;
                blue = 255;
            } else if (grid) {
                red = 48;
                green = 48;
                blue = 48;
            } else {
                red = 12;
                green = 12;
                blue = 12;
            }

            const size_t index = ((size_t)row * (size_t)width + (size_t)x) * 3U;
            frame[index + 0] = red;
            frame[index + 1] = green;
            frame[index + 2] = blue;
        }
    }
    return ESP_OK;
}

esp_err_t destination_spi_lcd_draw_rgb666_frame(const uint8_t *frame, size_t frame_len)
{
    if (frame == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_state != DEST_SPI_LCD_STATE_INITIALIZED || (s_io_handle == NULL && s_spi_handle == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }
    const int width = logical_h_res();
    const int height = logical_v_res();
    const size_t required = (size_t)width * (size_t)height * 3U;
    if (frame_len < required) {
        return ESP_ERR_INVALID_SIZE;
    }

    const uint8_t colmod = 0x66;
    esp_err_t err = send_cmd(0x3A, &colmod, sizeof(colmod));
    if (err != ESP_OK) {
        return err;
    }

    for (int y = 0; y < height && err == ESP_OK; y += DEST_SPI_LCD_TRANSFER_LINES) {
        int y_end = y + DEST_SPI_LCD_TRANSFER_LINES;
        if (y_end > height) {
            y_end = height;
        }
        const uint8_t *payload = frame + ((size_t)y * (size_t)width * 3U);
        const size_t payload_len = (size_t)(y_end - y) * (size_t)width * 3U;
        err = draw_raw_bytes(0, y, width, y_end, payload, payload_len);
    }
    return err;
}

void destination_spi_lcd_handle_test_pattern(const char *line)
{
    if (s_state != DEST_SPI_LCD_STATE_INITIALIZED || (s_io_handle == NULL && s_spi_handle == NULL)) {
        esp_err_t err = ESP_ERR_INVALID_STATE;
        remember("test_pattern", err);
        print_result("DEST_SPI_LCD_TEST_PATTERN", err);
        return;
    }
    char pattern[32] = "orientation";
    (void)sscanf(line, "DEST_SPI_LCD_TEST_PATTERN %31s", pattern);

    const uint8_t colmod = 0x66;
    esp_err_t err = send_cmd(0x3A, &colmod, sizeof(colmod));
    if (err != ESP_OK) {
        remember("test_pattern_colmod", err);
        print_result("DEST_SPI_LCD_TEST_PATTERN", err);
        return;
    }

    const int width = logical_h_res();
    const int height = logical_v_res();
    const size_t pixels = (size_t)width * DEST_SPI_LCD_TRANSFER_LINES;
    uint8_t *linebuf = heap_caps_malloc(pixels * 3, MALLOC_CAP_DMA);
    if (linebuf == NULL) {
        remember("test_pattern_alloc", ESP_ERR_NO_MEM);
        print_result("DEST_SPI_LCD_TEST_PATTERN", ESP_ERR_NO_MEM);
        return;
    }

    const uint8_t bars[][3] = {
        {255, 255, 255},
        {255, 255, 0},
        {0, 255, 255},
        {0, 255, 0},
        {255, 0, 255},
        {255, 0, 0},
        {0, 0, 255},
        {0, 0, 0},
    };
    int label_digit = -1;
    if (strncmp(pattern, "geometry_", 9) == 0 && pattern[9] >= '0' && pattern[9] <= '9') {
        label_digit = pattern[9] - '0';
    }
    for (int y = 0; y < height && err == ESP_OK; y += DEST_SPI_LCD_TRANSFER_LINES) {
        int y_end = y + DEST_SPI_LCD_TRANSFER_LINES;
        if (y_end > height) {
            y_end = height;
        }
        for (int row = y; row < y_end; ++row) {
            for (int x = 0; x < width; ++x) {
                uint8_t red = 0;
                uint8_t green = 0;
                uint8_t blue = 0;
                if (strcmp(pattern, "color_bars") == 0) {
                    const int bar = (x * 8) / width;
                    red = bars[bar][0];
                    green = bars[bar][1];
                    blue = bars[bar][2];
                } else {
                    const bool border = x < 4 || x >= (width - 4) ||
                                        row < 4 || row >= (height - 4);
                    const bool cross = x >= (width / 2 - 1) && x <= (width / 2 + 1);
                    const bool midline = row >= (height / 2 - 1) && row <= (height / 2 + 1);
                    const bool grid = (x % 40) == 0 || (row % 40) == 0;
                    if (seven_segment_digit_pixel(x, row, width, height, label_digit)) {
                        red = 255;
                        green = 128;
                        blue = 0;
                    } else if (border) {
                        red = 255;
                        green = 255;
                        blue = 255;
                    } else if (x < 64 && row < 64) {
                        red = 255;
                    } else if (x >= width - 64 && row < 64) {
                        green = 255;
                    } else if (x < 64 && row >= height - 64) {
                        blue = 255;
                    } else if (x >= width - 64 && row >= height - 64) {
                        red = 255;
                        green = 255;
                    } else if (cross || midline) {
                        red = 255;
                        green = 255;
                        blue = 255;
                    } else if (grid) {
                        red = 48;
                        green = 48;
                        blue = 48;
                    } else {
                        red = 12;
                        green = 12;
                        blue = 12;
                    }
                }
                const size_t index = ((size_t)(row - y) * (size_t)width + (size_t)x) * 3;
                linebuf[index + 0] = red;
                linebuf[index + 1] = green;
                linebuf[index + 2] = blue;
            }
        }
        err = draw_raw_bytes(0, y, width, y_end, linebuf, (size_t)(y_end - y) * (size_t)width * 3);
    }
    free(linebuf);
    remember("test_pattern", err);
    printf("{\"ok\":%s,\"command\":\"DEST_SPI_LCD_TEST_PATTERN\",\"pattern\":\"%s\","
           "\"state\":\"%s\",\"error\":\"%s\",\"err\":%d,"
           "\"controller\":\"%s\",\"width\":%d,\"height\":%d,"
           "\"spi\":{\"host\":\"SPI2\",\"pclk_hz\":%d,\"mode\":%d},"
           "\"pins\":{\"cs\":%d,\"reset\":%d,\"dc\":%d,\"mosi\":%d,\"sclk\":%d}}\n",
           err == ESP_OK ? "true" : "false",
           pattern,
           state_name(),
           err == ESP_OK ? "none" : esp_err_to_name(err),
           err,
           DEST_SPI_LCD_CONTROLLER,
           DEST_SPI_LCD_H_RES,
           DEST_SPI_LCD_V_RES,
           s_effective_pclk_hz,
           DEST_SPI_LCD_SPI_MODE,
           DEST_SPI_LCD_GPIO_CS,
           DEST_SPI_LCD_GPIO_RESET,
           DEST_SPI_LCD_GPIO_DC,
           DEST_SPI_LCD_GPIO_MOSI,
           DEST_SPI_LCD_GPIO_SCLK);
}

void destination_spi_lcd_handle_test_pattern565(const char *line)
{
    (void)line;
    if (s_state != DEST_SPI_LCD_STATE_INITIALIZED || (s_io_handle == NULL && s_spi_handle == NULL)) {
        esp_err_t err = ESP_ERR_INVALID_STATE;
        remember("test_pattern565", err);
        print_result("DEST_SPI_LCD_TEST_PATTERN565", err);
        return;
    }

    const uint8_t colmod = 0x55;
    esp_err_t err = send_cmd(0x3A, &colmod, sizeof(colmod));
    if (err != ESP_OK) {
        remember("test_pattern565_colmod", err);
        print_result("DEST_SPI_LCD_TEST_PATTERN565", err);
        return;
    }

    const size_t pixels = DEST_SPI_LCD_H_RES * DEST_SPI_LCD_TRANSFER_LINES;
    uint8_t *linebuf = heap_caps_malloc(pixels * 2, MALLOC_CAP_DMA);
    if (linebuf == NULL) {
        remember("test_pattern565_alloc", ESP_ERR_NO_MEM);
        print_result("DEST_SPI_LCD_TEST_PATTERN565", ESP_ERR_NO_MEM);
        return;
    }

    const uint16_t bars[] = {
        rgb565(255, 255, 255),
        rgb565(255, 255, 0),
        rgb565(0, 255, 255),
        rgb565(0, 255, 0),
        rgb565(255, 0, 255),
        rgb565(255, 0, 0),
        rgb565(0, 0, 255),
        rgb565(0, 0, 0),
    };
    for (int y = 0; y < DEST_SPI_LCD_V_RES && err == ESP_OK; y += DEST_SPI_LCD_TRANSFER_LINES) {
        int y_end = y + DEST_SPI_LCD_TRANSFER_LINES;
        if (y_end > DEST_SPI_LCD_V_RES) {
            y_end = DEST_SPI_LCD_V_RES;
        }
        for (int row = y; row < y_end; ++row) {
            for (int x = 0; x < DEST_SPI_LCD_H_RES; ++x) {
                const uint16_t color = bars[(x * 8) / DEST_SPI_LCD_H_RES];
                const size_t index = ((size_t)(row - y) * DEST_SPI_LCD_H_RES + (size_t)x) * 2;
                linebuf[index + 0] = (uint8_t)(color >> 8);
                linebuf[index + 1] = (uint8_t)(color & 0xff);
            }
        }
        err = draw_raw_bytes(0, y, DEST_SPI_LCD_H_RES, y_end, linebuf, (size_t)(y_end - y) * DEST_SPI_LCD_H_RES * 2);
    }
    free(linebuf);
    remember("test_pattern565", err);
    printf("{\"ok\":%s,\"command\":\"DEST_SPI_LCD_TEST_PATTERN565\","
           "\"state\":\"%s\",\"error\":\"%s\",\"err\":%d,"
           "\"controller\":\"%s\",\"width\":%d,\"height\":%d,"
           "\"spi\":{\"host\":\"SPI2\",\"pclk_hz\":%d,\"mode\":%d},"
           "\"pins\":{\"cs\":%d,\"reset\":%d,\"dc\":%d,\"mosi\":%d,\"sclk\":%d}}\n",
           err == ESP_OK ? "true" : "false",
           state_name(),
           err == ESP_OK ? "none" : esp_err_to_name(err),
           err,
           DEST_SPI_LCD_CONTROLLER,
           DEST_SPI_LCD_H_RES,
           DEST_SPI_LCD_V_RES,
           s_effective_pclk_hz,
           DEST_SPI_LCD_SPI_MODE,
           DEST_SPI_LCD_GPIO_CS,
           DEST_SPI_LCD_GPIO_RESET,
           DEST_SPI_LCD_GPIO_DC,
           DEST_SPI_LCD_GPIO_MOSI,
           DEST_SPI_LCD_GPIO_SCLK);
}

void destination_spi_lcd_handle_set_madctl(const char *line)
{
    unsigned value = 0x08;
    if (sscanf(line, "DEST_SPI_LCD_SET_MADCTL %x", &value) != 1 || value > 0xffU) {
        printf("{\"ok\":false,\"command\":\"%s\",\"error\":\"invalid_madctl\","
               "\"usage\":\"DEST_SPI_LCD_SET_MADCTL hex_00_to_ff\"}\n",
               line);
        return;
    }
    if (s_state != DEST_SPI_LCD_STATE_INITIALIZED || (s_io_handle == NULL && s_spi_handle == NULL)) {
        esp_err_t err = ESP_ERR_INVALID_STATE;
        remember("set_madctl", err);
        print_result("DEST_SPI_LCD_SET_MADCTL", err);
        return;
    }

    s_madctl = (uint8_t)value;
    esp_err_t err = send_cmd(0x36, &s_madctl, sizeof(s_madctl));
    remember("set_madctl", err);
    printf("{\"ok\":%s,\"command\":\"DEST_SPI_LCD_SET_MADCTL\","
           "\"state\":\"%s\",\"error\":\"%s\",\"err\":%d,\"madctl\":\"0x%02x\","
           "\"bits\":{\"my\":%s,\"mx\":%s,\"mv\":%s,\"ml\":%s,\"bgr\":%s,\"mh\":%s}}\n",
           err == ESP_OK ? "true" : "false",
           state_name(),
           err == ESP_OK ? "none" : esp_err_to_name(err),
           err,
           s_madctl,
           (s_madctl & 0x80) ? "true" : "false",
           (s_madctl & 0x40) ? "true" : "false",
           (s_madctl & 0x20) ? "true" : "false",
           (s_madctl & 0x10) ? "true" : "false",
           (s_madctl & 0x08) ? "true" : "false",
           (s_madctl & 0x04) ? "true" : "false");
}

void destination_spi_lcd_handle_signal_burst(const char *line)
{
    int duration_ms = 5000;
    if (sscanf(line, "DEST_SPI_LCD_SIGNAL_BURST %d", &duration_ms) != 1) {
        duration_ms = 5000;
    }
    if (duration_ms < 100) {
        duration_ms = 100;
    }
    if (duration_ms > 15000) {
        duration_ms = 15000;
    }

    if (s_state != DEST_SPI_LCD_STATE_INITIALIZED || (s_io_handle == NULL && s_spi_handle == NULL)) {
        esp_err_t err = ESP_ERR_INVALID_STATE;
        remember("signal_burst", err);
        print_result("DEST_SPI_LCD_SIGNAL_BURST", err);
        return;
    }

    const size_t pixels = DEST_SPI_LCD_H_RES * DEST_SPI_LCD_TRANSFER_LINES;
    uint16_t *linebuf = heap_caps_malloc(pixels * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (linebuf == NULL) {
        remember("signal_burst_alloc", ESP_ERR_NO_MEM);
        print_result("DEST_SPI_LCD_SIGNAL_BURST", ESP_ERR_NO_MEM);
        return;
    }

    esp_err_t err = ESP_OK;
    int passes = 0;
    const int64_t deadline_us = esp_timer_get_time() + ((int64_t)duration_ms * 1000);
    while (esp_timer_get_time() < deadline_us && err == ESP_OK) {
        const uint16_t color_a = (passes & 1) ? rgb565(255, 255, 255) : rgb565(255, 0, 0);
        const uint16_t color_b = (passes & 1) ? rgb565(0, 0, 0) : rgb565(0, 255, 255);
        for (size_t i = 0; i < pixels; ++i) {
            linebuf[i] = (i & 1) ? color_a : color_b;
        }
        for (int y = 0; y < DEST_SPI_LCD_V_RES && err == ESP_OK; y += DEST_SPI_LCD_TRANSFER_LINES) {
            int y_end = y + DEST_SPI_LCD_TRANSFER_LINES;
            if (y_end > DEST_SPI_LCD_V_RES) {
                y_end = DEST_SPI_LCD_V_RES;
            }
            err = draw_pixels(0, y, DEST_SPI_LCD_H_RES, y_end, linebuf);
        }
        ++passes;
    }
    free(linebuf);
    remember("signal_burst", err);
    printf("{\"ok\":%s,\"command\":\"DEST_SPI_LCD_SIGNAL_BURST\",\"state\":\"%s\","
           "\"error\":\"%s\",\"err\":%d,\"duration_ms\":%d,\"passes\":%d,"
           "\"pins\":{\"cs\":%d,\"reset\":%d,\"dc\":%d,\"mosi\":%d,\"sclk\":%d}}\n",
           err == ESP_OK ? "true" : "false",
           state_name(),
           err == ESP_OK ? "none" : esp_err_to_name(err),
           err,
           duration_ms,
           passes,
           DEST_SPI_LCD_GPIO_CS,
           DEST_SPI_LCD_GPIO_RESET,
           DEST_SPI_LCD_GPIO_DC,
           DEST_SPI_LCD_GPIO_MOSI,
           DEST_SPI_LCD_GPIO_SCLK);
}

static esp_err_t draw_gbc_rgb565_scaled(const uint8_t *source_rgb565,
                                        size_t source_len,
                                        uint32_t source_stride,
                                        uint32_t visible_width,
                                        uint32_t visible_height,
                                        uint32_t scale,
                                        bool clear_before_draw,
                                        int32_t linear_shift_pixels)
{
    if (source_rgb565 == NULL ||
        source_stride < visible_width ||
        visible_width == 0 ||
        visible_height == 0 ||
        scale == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t required_len = ((size_t)(visible_height - 1U) * (size_t)source_stride + (size_t)visible_width) * 2U;
    const int32_t source_pixels = (int32_t)(source_len / 2U);
    const int32_t last_source_pixel = (int32_t)((visible_height - 1U) * source_stride + (visible_width - 1U)) +
                                      linear_shift_pixels;
    if ((linear_shift_pixels == 0 && source_len < required_len) ||
        (linear_shift_pixels != 0 && (source_len < 2U || last_source_pixel >= source_pixels))) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (s_state != DEST_SPI_LCD_STATE_INITIALIZED || (s_io_handle == NULL && s_spi_handle == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }
    const int h_res = logical_h_res();
    const int v_res = logical_v_res();
    if ((visible_width * scale) > (uint32_t)h_res || (visible_height * scale) > (uint32_t)v_res) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t colmod = 0x66;
    esp_err_t err = send_cmd(0x3A, &colmod, sizeof(colmod));
    if (err != ESP_OK) {
        return err;
    }

    const int dest_width = (int)(visible_width * scale);
    const int dest_height = (int)(visible_height * scale);
    const int x_start = (h_res - dest_width) / 2;
    const int y_start = (v_res - dest_height) / 2;

    if (clear_before_draw) {
        err = fill_rect666(0, 0, h_res, v_res, 0, 0, 0);
        if (err != ESP_OK) {
            return err;
        }
    }

    if (scale == 1U) {
        const uint32_t chunk_lines = DEST_SPI_LCD_TRANSFER_LINES;
        const size_t chunk_bytes = (size_t)dest_width * (size_t)chunk_lines * 3U;
        uint8_t *chunkbuf = heap_caps_malloc(chunk_bytes, MALLOC_CAP_DMA);
        if (chunkbuf == NULL) {
            return ESP_ERR_NO_MEM;
        }
        for (uint32_t y0 = 0; y0 < visible_height && err == ESP_OK; y0 += chunk_lines) {
            const uint32_t lines = (visible_height - y0) < chunk_lines ? (visible_height - y0) : chunk_lines;
            for (uint32_t src_y = 0; src_y < lines; ++src_y) {
                uint8_t *out = chunkbuf + ((size_t)src_y * (size_t)dest_width * 3U);
                int32_t row_source_pixel = (int32_t)((y0 + src_y) * source_stride) + linear_shift_pixels;
                uint32_t src_x = 0;
                if (row_source_pixel < 0) {
                    uint32_t clamp_count = (uint32_t)(-row_source_pixel);
                    if (clamp_count > visible_width) {
                        clamp_count = visible_width;
                    }
                    for (; src_x < clamp_count; ++src_x) {
                        rgb565_le_to_rgb888(source_rgb565[0],
                                            source_rgb565[1],
                                            &out[(size_t)src_x * 3U + 0],
                                            &out[(size_t)src_x * 3U + 1],
                                            &out[(size_t)src_x * 3U + 2]);
                    }
                    row_source_pixel = 0;
                }
                size_t src_index = (size_t)row_source_pixel * 2U;
                for (; src_x < visible_width; ++src_x, src_index += 2U) {
                    rgb565_le_to_rgb888(source_rgb565[src_index],
                                        source_rgb565[src_index + 1U],
                                        &out[(size_t)src_x * 3U + 0],
                                        &out[(size_t)src_x * 3U + 1],
                                        &out[(size_t)src_x * 3U + 2]);
                }
            }
            err = draw_raw_bytes(x_start,
                                 y_start + (int)y0,
                                 x_start + dest_width,
                                 y_start + (int)(y0 + lines),
                                 chunkbuf,
                                 (size_t)dest_width * (size_t)lines * 3U);
        }
        free(chunkbuf);
        return err;
    }

    const uint32_t source_lines_per_chunk = DEST_SPI_LCD_TRANSFER_LINES / scale;
    const uint32_t chunk_source_lines = source_lines_per_chunk > 0 ? source_lines_per_chunk : 1U;
    const size_t chunk_bytes = (size_t)dest_width * (size_t)chunk_source_lines * (size_t)scale * 3U;
    uint8_t *chunkbuf[2] = {
        heap_caps_malloc(chunk_bytes, MALLOC_CAP_DMA),
        heap_caps_malloc(chunk_bytes, MALLOC_CAP_DMA),
    };
    if (chunkbuf[0] == NULL || chunkbuf[1] == NULL) {
        free(chunkbuf[0]);
        free(chunkbuf[1]);
        return ESP_ERR_NO_MEM;
    }

    uint32_t chunk_index = 0;
    for (uint32_t y0 = 0; y0 < visible_height && err == ESP_OK; y0 += chunk_source_lines) {
        uint8_t *outbuf = chunkbuf[chunk_index & 1U];
        ++chunk_index;
        const uint32_t lines = (visible_height - y0) < chunk_source_lines ? (visible_height - y0) : chunk_source_lines;
        const uint32_t dest_lines = lines * scale;
        for (uint32_t src_y = 0; src_y < lines; ++src_y) {
            for (uint32_t src_x = 0; src_x < visible_width; ++src_x) {
                const int32_t src_pixel = (int32_t)((y0 + src_y) * source_stride + src_x) +
                                          linear_shift_pixels;
                uint8_t low = 0;
                uint8_t high = 0;
                if (src_pixel < 0) {
                    low = source_rgb565[0];
                    high = source_rgb565[1];
                } else if (src_pixel < source_pixels) {
                    const size_t src_index = (size_t)src_pixel * 2U;
                    low = source_rgb565[src_index];
                    high = source_rgb565[src_index + 1U];
                }
                uint8_t red = 0;
                uint8_t green = 0;
                uint8_t blue = 0;
                rgb565_le_to_rgb888(low, high, &red, &green, &blue);
                const size_t out_x = (size_t)src_x * (size_t)scale;
                for (size_t repeat_y = 0; repeat_y < (size_t)scale; ++repeat_y) {
                    uint8_t *out = outbuf + ((((size_t)src_y * (size_t)scale + repeat_y) * (size_t)dest_width) + out_x) * 3U;
                    for (size_t repeat_x = 0; repeat_x < (size_t)scale; ++repeat_x) {
                        out[(repeat_x * 3U) + 0] = red;
                        out[(repeat_x * 3U) + 1] = green;
                        out[(repeat_x * 3U) + 2] = blue;
                    }
                }
            }
        }
        err = draw_raw_bytes(x_start,
                             y_start + (int)(y0 * scale),
                             x_start + dest_width,
                             y_start + (int)(y0 * scale) + (int)dest_lines,
                             outbuf,
                             (size_t)dest_width * (size_t)dest_lines * 3U);
    }

    free(chunkbuf[0]);
    free(chunkbuf[1]);
    return err;
}

esp_err_t destination_spi_lcd_draw_gbc_rgb565_1x(const uint8_t *source_rgb565,
                                                 size_t source_len,
                                                 uint32_t source_stride,
                                                 uint32_t visible_width,
                                                 uint32_t visible_height)
{
    return draw_gbc_rgb565_scaled(source_rgb565, source_len, source_stride, visible_width, visible_height, 1, true, 0);
}

esp_err_t destination_spi_lcd_draw_gbc_rgb565_1x_no_clear(const uint8_t *source_rgb565,
                                                          size_t source_len,
                                                          uint32_t source_stride,
                                                          uint32_t visible_width,
                                                          uint32_t visible_height)
{
    return draw_gbc_rgb565_scaled(source_rgb565, source_len, source_stride, visible_width, visible_height, 1, false, 0);
}

esp_err_t destination_spi_lcd_draw_gbc_rgb565_1x_shifted(const uint8_t *source_rgb565,
                                                         size_t source_len,
                                                         uint32_t source_stride,
                                                         uint32_t visible_width,
                                                         uint32_t visible_height,
                                                         int32_t linear_shift_pixels)
{
    return draw_gbc_rgb565_scaled(source_rgb565,
                                  source_len,
                                  source_stride,
                                  visible_width,
                                  visible_height,
                                  1,
                                  true,
                                  linear_shift_pixels);
}

esp_err_t destination_spi_lcd_draw_gbc_rgb565_1x_shifted_no_clear(const uint8_t *source_rgb565,
                                                                  size_t source_len,
                                                                  uint32_t source_stride,
                                                                  uint32_t visible_width,
                                                                  uint32_t visible_height,
                                                                  int32_t linear_shift_pixels)
{
    return draw_gbc_rgb565_scaled(source_rgb565,
                                  source_len,
                                  source_stride,
                                  visible_width,
                                  visible_height,
                                  1,
                                  false,
                                  linear_shift_pixels);
}

esp_err_t destination_spi_lcd_draw_gbc_rgb565_scaled2x(const uint8_t *source_rgb565,
                                                       size_t source_len,
                                                       uint32_t source_stride,
                                                       uint32_t visible_width,
                                                       uint32_t visible_height)
{
    return draw_gbc_rgb565_scaled(source_rgb565, source_len, source_stride, visible_width, visible_height, 2, true, 0);
}

esp_err_t destination_spi_lcd_draw_gbc_rgb565_scaled2x_no_clear(const uint8_t *source_rgb565,
                                                                size_t source_len,
                                                                uint32_t source_stride,
                                                                uint32_t visible_width,
                                                                uint32_t visible_height)
{
    return draw_gbc_rgb565_scaled(source_rgb565, source_len, source_stride, visible_width, visible_height, 2, false, 0);
}

static esp_err_t draw_gbc_rgb565_panel565_scaled(const uint8_t *source_rgb565,
                                                 size_t source_len,
                                                 uint32_t source_stride,
                                                 uint32_t visible_width,
                                                 uint32_t visible_height,
                                                 uint32_t scale,
                                                 int32_t linear_shift_pixels)
{
    if (source_rgb565 == NULL ||
        source_stride < visible_width ||
        visible_width == 0 ||
        visible_height == 0 ||
        scale == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t required_len = ((size_t)(visible_height - 1U) * (size_t)source_stride + (size_t)visible_width) * 2U;
    const int32_t source_pixels = (int32_t)(source_len / 2U);
    const int32_t last_source_pixel = (int32_t)((visible_height - 1U) * source_stride + (visible_width - 1U)) +
                                      linear_shift_pixels;
    if ((linear_shift_pixels == 0 && source_len < required_len) ||
        (linear_shift_pixels != 0 && (source_len < 2U || last_source_pixel >= source_pixels))) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (s_state != DEST_SPI_LCD_STATE_INITIALIZED || (s_io_handle == NULL && s_spi_handle == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }
    const int h_res = logical_h_res();
    const int v_res = logical_v_res();
    if ((visible_width * scale) > (uint32_t)h_res || (visible_height * scale) > (uint32_t)v_res) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t colmod = 0x55;
    esp_err_t err = send_cmd(0x3A, &colmod, sizeof(colmod));
    if (err != ESP_OK) {
        return err;
    }

    const int dest_width = (int)(visible_width * scale);
    const int dest_height = (int)(visible_height * scale);
    const int x_start = (h_res - dest_width) / 2;
    const int y_start = (v_res - dest_height) / 2;
    const uint32_t source_lines_per_chunk = DEST_SPI_LCD_TRANSFER_LINES / scale;
    const uint32_t chunk_source_lines = source_lines_per_chunk > 0 ? source_lines_per_chunk : 1U;
    const size_t chunk_bytes = (size_t)dest_width * (size_t)chunk_source_lines * (size_t)scale * 2U;
    uint8_t *chunkbuf = heap_caps_malloc(chunk_bytes, MALLOC_CAP_DMA);
    if (chunkbuf == NULL) {
        return ESP_ERR_NO_MEM;
    }

    for (uint32_t y0 = 0; y0 < visible_height && err == ESP_OK; y0 += chunk_source_lines) {
        const uint32_t lines = (visible_height - y0) < chunk_source_lines ? (visible_height - y0) : chunk_source_lines;
        const uint32_t dest_lines = lines * scale;
        for (uint32_t src_y = 0; src_y < lines; ++src_y) {
            for (uint32_t src_x = 0; src_x < visible_width; ++src_x) {
                const int32_t src_pixel = (int32_t)((y0 + src_y) * source_stride + src_x) +
                                          linear_shift_pixels;
                const size_t src_index = src_pixel < 0 ? 0U : (size_t)src_pixel * 2U;
                const size_t out_x = (size_t)src_x * (size_t)scale;
                for (size_t repeat_y = 0; repeat_y < (size_t)scale; ++repeat_y) {
                    uint8_t *out = chunkbuf + ((((size_t)src_y * (size_t)scale + repeat_y) * (size_t)dest_width) + out_x) * 2U;
                    for (size_t repeat_x = 0; repeat_x < (size_t)scale; ++repeat_x) {
                        rgb565_le_to_be(source_rgb565[src_index],
                                        source_rgb565[src_index + 1],
                                        &out[(repeat_x * 2U) + 0],
                                        &out[(repeat_x * 2U) + 1]);
                    }
                }
            }
        }
        err = draw_raw_bytes(x_start,
                             y_start + (int)(y0 * scale),
                             x_start + dest_width,
                             y_start + (int)(y0 * scale) + (int)dest_lines,
                             chunkbuf,
                             (size_t)dest_width * (size_t)dest_lines * 2U);
    }

    free(chunkbuf);
    return err;
}

esp_err_t destination_spi_lcd_draw_gbc_rgb565_panel565_1x_no_clear(const uint8_t *source_rgb565,
                                                                   size_t source_len,
                                                                   uint32_t source_stride,
                                                                   uint32_t visible_width,
                                                                   uint32_t visible_height)
{
    return draw_gbc_rgb565_panel565_scaled(source_rgb565, source_len, source_stride, visible_width, visible_height, 1, 0);
}

esp_err_t destination_spi_lcd_draw_gbc_rgb565_panel565_1x_shifted_no_clear(const uint8_t *source_rgb565,
                                                                           size_t source_len,
                                                                           uint32_t source_stride,
                                                                           uint32_t visible_width,
                                                                           uint32_t visible_height,
                                                                           int32_t linear_shift_pixels)
{
    return draw_gbc_rgb565_panel565_scaled(source_rgb565,
                                           source_len,
                                           source_stride,
                                           visible_width,
                                           visible_height,
                                           1,
                                           linear_shift_pixels);
}

esp_err_t destination_spi_lcd_draw_gbc_rgb565_panel565_scaled2x_no_clear(const uint8_t *source_rgb565,
                                                                         size_t source_len,
                                                                         uint32_t source_stride,
                                                                         uint32_t visible_width,
                                                                         uint32_t visible_height)
{
    return draw_gbc_rgb565_panel565_scaled(source_rgb565, source_len, source_stride, visible_width, visible_height, 2, 0);
}

static uint8_t fps_font_column(char c, int row)
{
    static const uint8_t digits[10][7] = {
        {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e},
        {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e},
        {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f},
        {0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e},
        {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02},
        {0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e},
        {0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e},
        {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
        {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e},
        {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c},
    };
    static const uint8_t letter_f[7] = {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10};
    static const uint8_t letter_p[7] = {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10};
    static const uint8_t letter_s[7] = {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e};
    static const uint8_t dot[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c};
    if (row < 0 || row >= FPS_OVERLAY_CHAR_H) {
        return 0;
    }
    if (c >= '0' && c <= '9') {
        return digits[c - '0'][row];
    }
    switch (c) {
    case 'F':
        return letter_f[row];
    case 'P':
        return letter_p[row];
    case 'S':
        return letter_s[row];
    case '.':
        return dot[row];
    default:
        return 0;
    }
}

esp_err_t destination_spi_lcd_draw_fps_overlay(int x_start, int y_start, int64_t fps_x1000)
{
    if (fps_x1000 < 0) {
        fps_x1000 = 0;
    }
    if (fps_x1000 > 999990) {
        fps_x1000 = 999990;
    }
    char text[16] = {0};
    const int fps_whole = (int)(fps_x1000 / 1000);
    const int fps_tenth = (int)((fps_x1000 % 1000) / 100);
    (void)snprintf(text, sizeof(text), "%03d.%01dFPS", fps_whole, fps_tenth);

    const size_t text_len = strnlen(text, sizeof(text));
    const int char_advance = (FPS_OVERLAY_CHAR_W + FPS_OVERLAY_SPACING) * FPS_OVERLAY_SCALE;
    const int width = (int)(text_len * (size_t)char_advance) + (2 * FPS_OVERLAY_SCALE);
    const int height = (FPS_OVERLAY_CHAR_H * FPS_OVERLAY_SCALE) + (2 * FPS_OVERLAY_SCALE);
    if (x_start < 0 || y_start < 0 || x_start + width > DEST_SPI_LCD_H_RES || y_start + height > DEST_SPI_LCD_V_RES) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t *buf = heap_caps_malloc((size_t)width * (size_t)height * 3U, MALLOC_CAP_DMA);
    if (buf == NULL) {
        return ESP_ERR_NO_MEM;
    }

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t out = ((size_t)y * (size_t)width + (size_t)x) * 3U;
            buf[out + 0] = 0;
            buf[out + 1] = 0;
            buf[out + 2] = 0;
        }
    }

    for (size_t i = 0; i < text_len; ++i) {
        const int glyph_x = FPS_OVERLAY_SCALE + (int)i * char_advance;
        const int glyph_y = FPS_OVERLAY_SCALE;
        for (int row = 0; row < FPS_OVERLAY_CHAR_H; ++row) {
            const uint8_t bits = fps_font_column(text[i], row);
            for (int col = 0; col < FPS_OVERLAY_CHAR_W; ++col) {
                if ((bits & (1U << (FPS_OVERLAY_CHAR_W - 1 - col))) == 0) {
                    continue;
                }
                for (int sy = 0; sy < FPS_OVERLAY_SCALE; ++sy) {
                    for (int sx = 0; sx < FPS_OVERLAY_SCALE; ++sx) {
                        const int px = glyph_x + (col * FPS_OVERLAY_SCALE) + sx;
                        const int py = glyph_y + (row * FPS_OVERLAY_SCALE) + sy;
                        const size_t out = ((size_t)py * (size_t)width + (size_t)px) * 3U;
                        buf[out + 0] = 255;
                        buf[out + 1] = 255;
                        buf[out + 2] = 255;
                    }
                }
            }
        }
    }

    esp_err_t err = draw_raw_bytes(x_start, y_start, x_start + width, y_start + height, buf, (size_t)width * (size_t)height * 3U);
    free(buf);
    return err;
}
