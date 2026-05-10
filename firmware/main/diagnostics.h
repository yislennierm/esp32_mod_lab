#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t boot_count;
    uint32_t command_count;
    uint32_t unsupported_command_count;
    uint32_t capture_pin_count;
    uint32_t dropped_sample_count;
    uint32_t overflow_count;
    uint32_t sync_loss_count;
} diagnostics_snapshot_t;

void diagnostics_init(uint32_t capture_pin_count);
void diagnostics_record_command(void);
void diagnostics_record_unsupported_command(void);
diagnostics_snapshot_t diagnostics_get_snapshot(void);
int diagnostics_format_json(char *buffer, size_t buffer_len);
