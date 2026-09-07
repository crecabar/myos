// SPDX-License-Identifier: GPL-2.0-only

#ifndef MYOS_DRIVERS_FRAMEBUFFER_H
#define MYOS_DRIVERS_FRAMEBUFFER_H

#include <stdint.h>

struct framebuffer {
    void *address;

    uint64_t width;
    uint64_t height;
    uint64_t pitch;

    uint16_t bpp;

    uint8_t red_mask_size;
    uint8_t red_mask_shift;

    uint8_t green_mask_size;
    uint8_t green_mask_shift;

    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
};

uint32_t framebuffer_make_color(
    const struct framebuffer *framebuffer,
    uint8_t red,
    uint8_t green,
    uint8_t blue
);

void framebuffer_put_pixel(
    struct framebuffer *framebuffer,
    uint64_t x,
    uint64_t y,
    uint32_t color
);

void framebuffer_fill_rect(
    struct framebuffer *framebuffer,
    uint64_t x,
    uint64_t y,
    uint64_t width,
    uint64_t height,
    uint32_t color
);

void framebuffer_draw_glyph(
    struct framebuffer *framebuffer,
    uint64_t x,
    uint64_t y,
    const uint8_t *glyph,
    uint32_t color,
    uint32_t scale
);

#endif
