// SPDX-License-Identifier: GPL-2.0-only

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

    uint8_t *base =
        (uint8_t *) framebuffer->address;

    for (uint64_t y = console->origin_y;
         y + line_height < framebuffer->height;
         ++y) {

        uint8_t *destination =
            base + y * framebuffer->pitch;

        uint8_t *source =
            base + (y + line_height) * framebuffer->pitch;

        for (uint64_t byte = 0;
             byte < framebuffer->pitch;
             ++byte) {
            destination[byte] = source[byte];
        }
    }

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
