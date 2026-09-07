// SPDX-License-Identifier: GPL-2.0-only

#include <stddef.h>
#include <stdint.h>

#include "framebuffer.h"

void framebuffer_put_pixel(
    struct framebuffer *framebuffer,
    uint64_t x,
    uint64_t y,
    uint32_t color)
{
    uint8_t *row =
        (uint8_t *) framebuffer->address
        + y * framebuffer->pitch;

    uint32_t *pixel =
        (uint32_t *) (row + x * sizeof(uint32_t));

    *pixel = color;
}

void framebuffer_fill_rect(
    struct framebuffer *framebuffer,
    uint64_t x,
    uint64_t y,
    uint64_t width,
    uint64_t height,
    uint32_t color)
{
    for (uint64_t row = 0; row < height; ++row) {
        for (uint64_t column = 0; column < width; ++column) {
            framebuffer_put_pixel(
                framebuffer,
                x + column,
                y + row,
                color
            );
        }
    }
}

uint32_t framebuffer_make_color(
    const struct framebuffer *framebuffer,
    uint8_t red,
    uint8_t green,
    uint8_t blue)
{
    return
        ((uint32_t) red   << framebuffer->red_mask_shift) |
        ((uint32_t) green << framebuffer->green_mask_shift) |
        ((uint32_t) blue  << framebuffer->blue_mask_shift);
}

void framebuffer_draw_glyph(
    struct framebuffer *framebuffer,
    uint64_t x,
    uint64_t y,
    const uint8_t *glyph,
    uint32_t color,
    uint32_t scale)
{
    for (uint64_t row = 0; row < 8; ++row) {
        for (uint64_t column = 0; column < 8; ++column) {
            uint8_t mask = (uint8_t) (1U << (7 - column));

            if ((glyph[row] & mask) != 0) {
                framebuffer_fill_rect(
                    framebuffer,
                    x + column * scale,
                    y + row * scale,
                    scale,
                    scale,
                    color
                );
            }
        }
    }
}
