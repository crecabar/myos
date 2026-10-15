// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#include "../drivers/framebuffer.h"
#include "../font/font8x8.h"
#include "../format/format.h"
#include "console.h"

#define FONT_WIDTH 8
#define FONT_HEIGHT 8

static void console_scroll(struct console *console);
static void console_new_line(struct console *console);

static void console_format_putc(char character,void *context);


void console_init(
    struct console *console,
    struct framebuffer *framebuffer,
    uint64_t x,
    uint64_t y,
    uint32_t foreground,
    uint64_t scale)
{
    console->framebuffer = framebuffer;

    console->origin_x = x;
    console->origin_y = y;

    console->cursor_x = x;
    console->cursor_y = y;

    console->foreground = foreground;
    console->scale = scale;

    console->scroll_count = 0U;
}

void console_write(
    struct console *console,
    const char *string)
{
    while (*string != '\0') {
        console_put_char(console, *string);
        ++string;
    }
}

void console_put_char(
    struct console *console,
    char character)
{
    uint64_t character_width =
        FONT_WIDTH * console->scale;

    uint64_t spacing =
        console->scale;

    uint64_t advance =
        character_width + spacing;

    if (character == '\n') {
        console_new_line(console);
        return;
    }

    if (console->cursor_x + character_width >
        console->framebuffer->width) {
        console_new_line(console);
    }

    if (character == ' ') {
        console->cursor_x += advance;
        return;
    }

    const uint8_t *glyph =
        font8x8_get_glyph(character);

    if (glyph != NULL) {
        framebuffer_draw_glyph(
            console->framebuffer,
            console->cursor_x,
            console->cursor_y,
            glyph,
            console->foreground,
            console->scale
        );
    }

    console->cursor_x += advance;
}

bool console_erase_previous(
    struct console *console,
    uint64_t minimum_x,
    uint64_t minimum_y)
{
    if (
        console == NULL ||
        console->framebuffer == NULL ||
        console->scale == 0U ||
        console->scale > UINT64_MAX / 9U
    ) {
        return false;
    }

    struct framebuffer *framebuffer =
        console->framebuffer;

    uint64_t character_width =
        FONT_WIDTH * console->scale;

    uint64_t advance =
        (FONT_WIDTH + 1U) * console->scale;

    uint64_t line_height =
        (FONT_HEIGHT + 1U) * console->scale;

    if (
        console->origin_x >= framebuffer->width ||
        console->origin_y >= framebuffer->height ||
        character_width >
            framebuffer->width - console->origin_x ||
        console->cursor_x < console->origin_x ||
        console->cursor_y < console->origin_y ||
        console->cursor_y >= framebuffer->height ||
        minimum_x < console->origin_x ||
        minimum_y < console->origin_y
    ) {
        return false;
    }

    uint64_t erase_x;
    uint64_t erase_y =
        console->cursor_y;

    if (
        console->cursor_x >
        console->origin_x
    ) {
        if (
            console->cursor_x -
                console->origin_x <
            advance
        ) {
            return false;
        }

        erase_x =
            console->cursor_x - advance;
    } else {
        /*
         * The cursor is at the beginning of a
         * displayed row. The preceding cell may
         * be on the previous automatically
         * wrapped row.
         */
        if (
            console->cursor_x !=
                console->origin_x ||
            console->cursor_y -
                console->origin_y <
                line_height
        ) {
            return false;
        }

        erase_y -= line_height;

        uint64_t available =
            framebuffer->width -
            console->origin_x -
            character_width;

        erase_x =
            console->origin_x +
            (available / advance) * advance;
    }

    /*
     * Never erase before the caller's editing
     * boundary. The boundary is inclusive:
     * the first editable cell may be erased.
     */
    if (
        erase_y < minimum_y ||
        (
            erase_y == minimum_y &&
            erase_x < minimum_x
        )
    ) {
        return false;
    }

    /*
     * The console currently uses a black
     * background. Clear both the glyph and
     * its spacing cell.
     */
    framebuffer_fill_rect(
        framebuffer,
        erase_x,
        erase_y,
        advance,
        line_height,
        0U
    );

    console->cursor_x = erase_x;
    console->cursor_y = erase_y;

    return true;
}

void console_write_uint(
    struct console *console,
    uint64_t value)
{
    char buffer[20];
    uint64_t length = 0;

    if (value == 0) {
        console_put_char(console, '0');
        return;
    }

    while (value > 0) {
        uint64_t digit = value % 10;

        buffer[length] =
            (char) ('0' + digit);

        value /= 10;
        ++length;
    }

    while (length > 0) {
        --length;

        console_put_char(
            console,
            buffer[length]
        );
    }
}

void console_write_hex(
    struct console *console,
    uint64_t value)
{
    static const char hex_digits[] =
        "0123456789abcdef";

    char buffer[16];
    uint64_t length = 0;

    console_write(console, "0x");

    if (value == 0) {
        console_put_char(console, '0');
        return;
    }

    while (value > 0) {
        uint64_t digit =
            value % 16;

        buffer[length] =
            hex_digits[digit];

        value /= 16;
        ++length;
    }

    while (length > 0) {
        --length;

        console_put_char(
            console,
            buffer[length]
        );
    }
}

void console_printf(
    struct console *console,
    const char *format,
    ...)
{
    va_list arguments;

    va_start(arguments, format);
    console_vprintf(console, format, arguments);
    va_end(arguments);
}

void console_vprintf(
    struct console *console,
    const char *format,
    va_list arguments)
{
    format_vprintf(
        console_format_putc,
        console,
        format,
        arguments
    );
}

static void console_new_line(struct console *console)
{
    uint64_t line_height =
        FONT_HEIGHT * console->scale + console->scale;

    console->cursor_x =
        console->origin_x;

    console->cursor_y +=
        line_height;

    if (console->cursor_y + line_height >
        console->framebuffer->height) {
        console_scroll(console);
    }
}

static void console_scroll(struct console *console)
{
    uint64_t line_height =
        FONT_HEIGHT * console->scale + console->scale;

    struct framebuffer *framebuffer =
        console->framebuffer;

    framebuffer_copy_rect(
        framebuffer,
        0,
        console->origin_y + line_height,
        0,
        console->origin_y,
        framebuffer->width,
        framebuffer->height -
            console->origin_y -
            line_height
    );

    framebuffer_fill_rect(
        framebuffer,
        0,
        framebuffer->height - line_height,
        framebuffer->width,
        line_height,
        0
    );

    console->cursor_x =
        console->origin_x;

    console->cursor_y =
        framebuffer->height - line_height;
    ++console->scroll_count;
}

static void console_format_putc(
    char character,
    void *context)
{
    struct console *console =
        (struct console *) context;

    console_put_char(
        console,
        character
    );
}
