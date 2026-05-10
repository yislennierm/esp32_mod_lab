#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "tusb_cdc_acm.h"

#define RX_QUEUE_DEPTH 8
#define RX_BUF_LEN 256
#define COMMAND_LINE_LEN 256
#define BENCH_MAX_FRAMES 256
#define BENCH_MAX_PAYLOAD_LEN (256U * 1024U)
#define BENCH_CHUNK_LEN 1024U

static const char *TAG = "tinyusb_bench";

typedef struct {
    uint8_t data[RX_BUF_LEN];
    size_t len;
    uint8_t itf;
} rx_message_t;

static QueueHandle_t s_rx_queue;
static uint8_t s_rx_buf[RX_BUF_LEN];

static void bench_write(uint8_t itf, const void *data, size_t len)
{
    const uint8_t *cursor = (const uint8_t *)data;
    while (len > 0) {
        size_t chunk = len > BENCH_CHUNK_LEN ? BENCH_CHUNK_LEN : len;
        tinyusb_cdcacm_write_queue(itf, cursor, chunk);
        tinyusb_cdcacm_write_flush(itf, 0);
        cursor += chunk;
        len -= chunk;
    }
}

static void bench_write_str(uint8_t itf, const char *text)
{
    bench_write(itf, text, strlen(text));
}

static uint32_t bench_checksum(uint32_t frame_index, uint32_t payload_len)
{
    uint32_t checksum = 0;
    for (uint32_t offset = 0; offset < payload_len; ++offset) {
        checksum += (uint8_t)((offset + frame_index) & 0xffU);
    }
    return checksum;
}

static void handle_usb_bench_stream_bin(uint8_t itf, const char *line)
{
    int frame_count = 0;
    int payload_len = 0;
    if (sscanf(line, "USB_BENCH_STREAM_BIN %d %d", &frame_count, &payload_len) != 2 ||
        frame_count < 1 ||
        frame_count > BENCH_MAX_FRAMES ||
        payload_len < 0 ||
        payload_len > (int)BENCH_MAX_PAYLOAD_LEN) {
        bench_write_str(itf,
                        "{\"ok\":false,\"error\":\"invalid_arguments\","
                        "\"usage\":\"USB_BENCH_STREAM_BIN <frame_count_1_to_256> "
                        "<payload_len_0_to_262144>\",\"binary_len\":0}\n");
        return;
    }

    uint8_t chunk[BENCH_CHUNK_LEN];
    for (int frame_index = 0; frame_index < frame_count; ++frame_index) {
        const uint32_t checksum = bench_checksum((uint32_t)frame_index, (uint32_t)payload_len);
        char header[256];
        int header_len = snprintf(header,
                                  sizeof(header),
                                  "{\"ok\":true,\"command\":\"USB_BENCH_STREAM_BIN\","
                                  "\"transport\":\"tinyusb_cdc_acm\","
                                  "\"frame_index\":%d,"
                                  "\"binary_len\":%d,"
                                  "\"payload_pattern\":\"byte=(offset+frame_index)&0xff\","
                                  "\"checksum\":%" PRIu32 "}\n",
                                  frame_index,
                                  payload_len,
                                  checksum);
        if (header_len <= 0 || header_len >= (int)sizeof(header)) {
            bench_write_str(itf,
                            "{\"ok\":false,\"error\":\"header_format_failed\","
                            "\"binary_len\":0}\n");
            return;
        }
        bench_write(itf, header, (size_t)header_len);

        uint32_t remaining = (uint32_t)payload_len;
        uint32_t offset = 0;
        while (remaining > 0) {
            uint32_t chunk_len = remaining > BENCH_CHUNK_LEN ? BENCH_CHUNK_LEN : remaining;
            for (uint32_t i = 0; i < chunk_len; ++i) {
                chunk[i] = (uint8_t)((offset + i + (uint32_t)frame_index) & 0xffU);
            }
            bench_write(itf, chunk, chunk_len);
            offset += chunk_len;
            remaining -= chunk_len;
        }

        const char flush_byte = '\n';
        bench_write(itf, &flush_byte, 1);
    }
}

static bool command_equals(const char *line, const char *command)
{
    return strcmp(line, command) == 0;
}

static bool command_has_prefix(const char *line, const char *prefix)
{
    size_t len = strlen(prefix);
    return strncmp(line, prefix, len) == 0 && (line[len] == '\0' || isspace((unsigned char)line[len]));
}

static void handle_command(uint8_t itf, char *line)
{
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n' ||
                       isspace((unsigned char)line[len - 1]))) {
        line[--len] = '\0';
    }

    if (command_equals(line, "PING")) {
        bench_write_str(itf, "{\"ok\":true,\"response\":\"PONG\"}\n");
        return;
    }
    if (command_equals(line, "GET_VERSION")) {
        bench_write_str(itf,
                        "{\"ok\":true,\"name\":\"tinyusb_transport_bench\","
                        "\"version\":\"0.1.0\","
                        "\"phase\":\"transport-benchmark\","
                        "\"gpio_touched\":false,"
                        "\"lcdcam_touched\":false}\n");
        return;
    }
    if (command_has_prefix(line, "USB_BENCH_STREAM_BIN")) {
        handle_usb_bench_stream_bin(itf, line);
        return;
    }

    bench_write_str(itf,
                    "{\"ok\":false,\"error\":\"unknown_command\","
                    "\"known\":[\"PING\",\"GET_VERSION\",\"USB_BENCH_STREAM_BIN\"]}\n");
}

static void process_rx_message(const rx_message_t *msg)
{
    static char line[COMMAND_LINE_LEN];
    static size_t line_len = 0;

    for (size_t i = 0; i < msg->len; ++i) {
        char c = (char)msg->data[i];
        if (c == '\n') {
            line[line_len] = '\0';
            handle_command(msg->itf, line);
            line_len = 0;
            continue;
        }
        if (line_len + 1 < sizeof(line)) {
            line[line_len++] = c;
        } else {
            line_len = 0;
            bench_write_str(msg->itf,
                            "{\"ok\":false,\"error\":\"command_too_long\","
                            "\"binary_len\":0}\n");
        }
    }
}

static void tinyusb_rx_callback(int itf, cdcacm_event_t *event)
{
    (void)event;
    size_t rx_size = 0;
    esp_err_t ret = tinyusb_cdcacm_read(itf, s_rx_buf, sizeof(s_rx_buf), &rx_size);
    if (ret != ESP_OK || rx_size == 0) {
        return;
    }

    rx_message_t msg = {
        .len = rx_size,
        .itf = (uint8_t)itf,
    };
    memcpy(msg.data, s_rx_buf, rx_size);
    (void)xQueueSend(s_rx_queue, &msg, 0);
}

void app_main(void)
{
    ESP_LOGI(TAG, "TinyUSB transport benchmark starting; no GPIO or LCD_CAM setup is performed");

    s_rx_queue = xQueueCreate(RX_QUEUE_DEPTH, sizeof(rx_message_t));
    assert(s_rx_queue != NULL);

    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,
        .string_descriptor = NULL,
        .external_phy = false,
#if (TUD_OPT_HIGH_SPEED)
        .fs_configuration_descriptor = NULL,
        .hs_configuration_descriptor = NULL,
        .qualifier_descriptor = NULL,
#else
        .configuration_descriptor = NULL,
#endif
    };
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    const tinyusb_config_cdcacm_t acm_cfg = {
        .usb_dev = TINYUSB_USBDEV_0,
        .cdc_port = TINYUSB_CDC_ACM_0,
        .rx_unread_buf_sz = RX_BUF_LEN,
        .callback_rx = tinyusb_rx_callback,
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = NULL,
        .callback_line_coding_changed = NULL,
    };
    ESP_ERROR_CHECK(tusb_cdc_acm_init(&acm_cfg));

    ESP_LOGI(TAG, "TinyUSB CDC ACM ready");

    rx_message_t msg;
    while (true) {
        if (xQueueReceive(s_rx_queue, &msg, portMAX_DELAY) == pdTRUE) {
            process_rx_message(&msg);
        }
    }
}
