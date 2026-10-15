// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#ifndef MYOS_DIAGNOSTICS_DIAGNOSTICS_H
#define MYOS_DIAGNOSTICS_DIAGNOSTICS_H

#include <stdarg.h>
#include <stdbool.h>

struct console;

void diagnostics_init(void);

void diagnostics_attach_console(
    struct console *console
);

/**
 * Enables or disables diagnostic mirroring to the
 * attached framebuffer console.
 *
 * Serial diagnostics remain enabled regardless of
 * this setting.
 *
 * Disabling mirroring does not detach the console.
 * Kernel panic may restore framebuffer diagnostics.
 */
void diagnostics_console_mirror_set(
    bool enabled
);

void diagnostics_write(
    const char *string
);

void diagnostics_vprintf(
    const char *format,
    va_list arguments
);

void diagnostics_printf(
    const char *format,
    ...
);

#endif
