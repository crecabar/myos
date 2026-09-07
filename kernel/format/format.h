// SPDX-License-Identifier: GPL-2.0-only

#ifndef MYOS_FORMAT_FORMAT_H
#define MYOS_FORMAT_FORMAT_H

#include <stdarg.h>

typedef void (*format_putc_fn)(
    char character,
    void *context
);

void format_vprintf(
    format_putc_fn putc,
    void *context,
    const char *format,
    va_list arguments
);

#endif
