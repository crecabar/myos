// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include <stdarg.h>
#include <stddef.h>

#include <myos/kernel/arch/x86_64/serial.h>
#include "../console/console.h"
#include <myos/kernel/format/format.h>
#include "diagnostics.h"

static struct console *diagnostics_console = NULL;
static bool diagnostics_console_mirror_enabled = true;

static void diagnostics_putc(
    char character,
    void *context)
{
    (void) context;

    serial_write_char(character);

    if (
        diagnostics_console_mirror_enabled &&
        diagnostics_console != NULL
    ) {
        console_put_char(
            diagnostics_console,
            character
        );
    }
}

void diagnostics_init(void)
{
    diagnostics_console = NULL;
    diagnostics_console_mirror_enabled = true;
}

void diagnostics_attach_console(
    struct console *console)
{
    diagnostics_console = console;
}

void diagnostics_console_mirror_set(
    bool enabled)
{
    diagnostics_console_mirror_enabled = enabled;
}

void diagnostics_write(
    const char *string)
{
    serial_write_string(string);

    if (
        diagnostics_console_mirror_enabled &&
        diagnostics_console != NULL
    ) {
        console_write(
            diagnostics_console,
            string
        );
    }
}

void diagnostics_vprintf(
    const char *format,
    va_list arguments)
{
    format_vprintf(
        diagnostics_putc,
        NULL,
        format,
        arguments
    );
}

void diagnostics_printf(
    const char *format,
    ...)
{
    va_list arguments;

    va_start(arguments, format);

    diagnostics_vprintf(
        format,
        arguments
    );

    va_end(arguments);
}
