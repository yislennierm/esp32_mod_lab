#include "diagnostics.h"

#include <stdio.h>

static diagnostics_snapshot_t s_diagnostics;

void diagnostics_init(uint32_t capture_pin_count)
{
    s_diagnostics.boot_count++;
    s_diagnostics.command_count = 0;
    s_diagnostics.unsupported_command_count = 0;
    s_diagnostics.capture_pin_count = capture_pin_count;
    s_diagnostics.dropped_sample_count = 0;
    s_diagnostics.overflow_count = 0;
    s_diagnostics.sync_loss_count = 0;
}

void diagnostics_record_command(void)
{
    s_diagnostics.command_count++;
}

void diagnostics_record_unsupported_command(void)
{
    s_diagnostics.unsupported_command_count++;
}

diagnostics_snapshot_t diagnostics_get_snapshot(void)
{
    return s_diagnostics;
}

int diagnostics_format_json(char *buffer, size_t buffer_len)
{
    diagnostics_snapshot_t snapshot = diagnostics_get_snapshot();

    return snprintf(
        buffer,
        buffer_len,
        "{\"boot_count\":%lu,\"command_count\":%lu,\"unsupported_command_count\":%lu,"
        "\"capture_pin_count\":%lu,\"dropped_sample_count\":%lu,"
        "\"overflow_count\":%lu,\"sync_loss_count\":%lu}",
        (unsigned long)snapshot.boot_count,
        (unsigned long)snapshot.command_count,
        (unsigned long)snapshot.unsupported_command_count,
        (unsigned long)snapshot.capture_pin_count,
        (unsigned long)snapshot.dropped_sample_count,
        (unsigned long)snapshot.overflow_count,
        (unsigned long)snapshot.sync_loss_count);
}
