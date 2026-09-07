// SPDX-License-Identifier: GPL-2.0-only

#ifndef MYOS_DRIVERS_FRAMEBUFFER_H
#define MYOS_DRIVERS_FRAMEBUFFER_H

#include <stdint.h>

#include <limine.h>

void framebuffer_put_pixel(
    struct limine_framebuffer *framebuffer,
    uint64_t x,
    uint64_t y,
    uint32_t color
);

void framebuffer_fill_rect(
    struct limine_framebuffer *framebuffer,
    uint64_t x,
    uint64_t y,
    uint64_t width,
    uint64_t height,
    uint32_t color
);

uint32_t framebuffer_make_color(
    struct limine_framebuffer *framebuffer,
    uint8_t red,
    uint8_t green,
    uint8_t blue
);

void framebuffer_draw_glyph(
    struct limine_framebuffer *framebuffer,
    uint64_t x,
    uint64_t y,
    const uint8_t glyph[8],
    uint32_t color,
    uint64_t scale
);

#endif
