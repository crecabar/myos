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

/**
 * Draws one pixel when the coordinate lies inside the visible framebuffer.
 *
 * Invalid framebuffer geometry and out-of-bounds coordinates are ignored.
 */
void framebuffer_put_pixel(
    struct framebuffer *framebuffer,
    uint64_t x,
    uint64_t y,
    uint32_t color
);

/**
 * Fills the visible intersection of a rectangle with the framebuffer.
 *
 * Rectangles extending beyond the right or bottom edge are clipped. Empty,
 * fully out-of-bounds, or invalid requests perform no writes.
 */
void framebuffer_fill_rect(
    struct framebuffer *framebuffer,
    uint64_t x,
    uint64_t y,
    uint64_t width,
    uint64_t height,
    uint32_t color
);

/**
 * Copies a rectangle within the visible framebuffer.
 *
 * Source and destination rectangles are clipped to the visible framebuffer.
 * Overlapping copies preserve the original source contents with memmove-like
 * semantics. Empty, fully out-of-bounds, or invalid requests perform no writes.
 */
void framebuffer_copy_rect(
    struct framebuffer *framebuffer,
    uint64_t source_x,
    uint64_t source_y,
    uint64_t destination_x,
    uint64_t destination_y,
    uint64_t width,
    uint64_t height
);

/**
 * Draws an 8x8 glyph using bounded framebuffer rectangle primitives.
 *
 * Glyph blocks extending outside the visible framebuffer are clipped.
 * A NULL glyph or zero scale performs no writes.
 */
void framebuffer_draw_glyph(
    struct framebuffer *framebuffer,
    uint64_t x,
    uint64_t y,
    const uint8_t *glyph,
    uint32_t color,
    uint32_t scale
);

#endif
