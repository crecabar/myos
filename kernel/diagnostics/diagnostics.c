// SPDX-License-Identifier: GPL-2.0-only

#include <stdarg.h>
#include <stddef.h>

#include "../arch/x86_64/serial.h"
#include "../console/console.h"
#include "../format/format.h"
#include "diagnostics.h"

static struct console *diagnostics_console = NULL;

static void diagnostics_putc(
    char character,
    void *context)
{
    (void) context;

    serial_write_char(character);

    if (diagnostics_console != NULL) {
        console_put_char(
            diagnostics_console,
            character
        );
    }
}

void diagnostics_init(void)
{
    diagnostics_console = NULL;
}

void diagnostics_attach_console(
    struct console *console)
{
    diagnostics_console = console;
}

void diagnostics_write(
    const char *string)
{
    serial_write_string(string);

    if (diagnostics_console != NULL) {
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
