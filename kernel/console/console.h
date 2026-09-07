// SPDX-License-Identifier: GPL-2.0-only

#ifndef MYOS_CONSOLE_CONSOLE_H
#define MYOS_CONSOLE_CONSOLE_H

#include <stdint.h>
#include <stdarg.h>

#include <limine.h>

struct console {
    struct framebuffer *framebuffer;

    uint64_t origin_x;
    uint64_t origin_y;

    uint64_t cursor_x;
    uint64_t cursor_y;

    uint32_t foreground;
    uint64_t scale;
};

void console_init(
    struct console *console,
    struct framebuffer *framebuffer,
    uint64_t x,
    uint64_t y,
    uint32_t foreground,
    uint64_t scale
);

void console_write(
    struct console *console,
    const char *string
);

void console_put_char(
    struct console *console,
    char character
);

void console_write_uint(
    struct console *console,
    uint64_t value
);

void console_write_hex(
    struct console *console,
    uint64_t value
);

void console_printf(
    struct console *console,
    const char *format,
    ...
);

void console_vprintf(
    struct console *console,
    const char *format,
    va_list arguments
);

#endif
