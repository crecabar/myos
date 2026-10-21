// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#ifndef MYOS_KERNEL_CONSOLE_CONSOLE_H
#define MYOS_KERNEL_CONSOLE_CONSOLE_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

struct framebuffer;

struct console {
    struct framebuffer *framebuffer;

    uint64_t origin_x;
    uint64_t origin_y;

    uint64_t cursor_x;
    uint64_t cursor_y;

    uint32_t foreground;
    uint64_t scale;

    /*
     * Number of framebuffer scroll operations.
     * An input editor may use this to keep its
     * editing boundary aligned after scrolling.
     */
    uint64_t scroll_count;
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

/**
 * Returns the framebuffer height of one console
 * text row, including interline spacing.
 *
 * Returns zero for invalid arguments or scale.
 */
uint64_t console_row_height(
    const struct console *console
);

/**
 * Erases the character cell immediately preceding
 * the cursor, including across an automatic wrap.
 *
 * minimum_x and minimum_y identify the earliest
 * editable cell in the current display coordinates.
 *
 * The caller must only request erasure when it has
 * an accepted character to undo. This primitive
 * does not maintain input history or distinguish
 * explicit newlines from automatic wraps.
 *
 * Returns false without modifying the cursor or
 * framebuffer when the operation is not possible.
 */
bool console_erase_previous(
    struct console *console,
    uint64_t minimum_x,
    uint64_t minimum_y
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
