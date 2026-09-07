// SPDX-License-Identifier: GPL-2.0-only

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "format.h"

static void format_write_string(
    format_putc_fn putc,
    void *context,
    const char *string)
{
    while (*string != '\0') {
        putc(*string, context);
        ++string;
    }
}

static void format_write_uint(
    format_putc_fn putc,
    void *context,
    uint64_t value)
{
    char buffer[20];
    uint64_t length = 0;

    if (value == 0) {
        putc('0', context);
        return;
    }

    while (value > 0) {
        buffer[length] =
            (char) ('0' + (value % 10));

        value /= 10;
        ++length;
    }

    while (length > 0) {
        --length;
        putc(buffer[length], context);
    }
}

static void format_write_hex(
    format_putc_fn putc,
    void *context,
    uint64_t value)
{
    static const char digits[] =
        "0123456789abcdef";

    char buffer[16];
    uint64_t length = 0;

    format_write_string(
        putc,
        context,
        "0x"
    );

    if (value == 0) {
        putc('0', context);
        return;
    }

    while (value > 0) {
        buffer[length] =
            digits[value % 16];

        value /= 16;
        ++length;
    }

    while (length > 0) {
        --length;
        putc(buffer[length], context);
    }
}

void format_vprintf(
    format_putc_fn putc,
    void *context,
    const char *format,
    va_list arguments)
{
    while (*format != '\0') {
        if (*format != '%') {
            putc(*format, context);
            ++format;
            continue;
        }

        ++format;

        if (*format == '\0') {
            putc('%', context);
            break;
        }

        switch (*format) {
        case '%':
            putc('%', context);
            break;

        case 'c': {
            int character =
                va_arg(arguments, int);

            putc(
                (char) character,
                context
            );

            break;
        }

        case 's': {
            const char *string =
                va_arg(arguments, const char *);

            format_write_string(
                putc,
                context,
                string != NULL
                    ? string
                    : "(null)"
            );

            break;
        }

        case 'u': {
            uint64_t value =
                va_arg(arguments, uint64_t);

            format_write_uint(
                putc,
                context,
                value
            );

            break;
        }

        case 'x': {
            uint64_t value =
                va_arg(arguments, uint64_t);

            format_write_hex(
                putc,
                context,
                value
            );

            break;
        }

        default:
            putc('%', context);
            putc(*format, context);
            break;
        }

        ++format;
    }
}
