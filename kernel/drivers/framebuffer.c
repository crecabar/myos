// SPDX-License-Identifier: GPL-2.0-only

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "framebuffer.h"

#define FRAMEBUFFER_BYTES_PER_PIXEL ((uint64_t) sizeof(uint32_t))

static bool framebuffer_geometry_valid(
    const struct framebuffer *framebuffer
);

static bool framebuffer_pixel_pointer(
    struct framebuffer *framebuffer,
    uint64_t x,
    uint64_t y,
    uint32_t **pixel
);

static bool framebuffer_geometry_valid(
    const struct framebuffer *framebuffer)
{
    if (framebuffer == NULL) return false;

    if (framebuffer->address == NULL) return false;

    if (framebuffer->width == 0) return false;

    if (framebuffer->height == 0) return false;

    if (framebuffer->bpp != 32) return false;

    if (((uintptr_t) framebuffer->address % _Alignof(uint32_t)) != 0) {
        return false;
    }

    if (framebuffer->pitch % _Alignof(uint32_t) != 0) {
        return false;
    }

    if (framebuffer->width > UINT64_MAX / FRAMEBUFFER_BYTES_PER_PIXEL) {
        return false;
    }

    uint64_t visible_row_bytes =
        framebuffer->width *
        FRAMEBUFFER_BYTES_PER_PIXEL;

    if (framebuffer->pitch < visible_row_bytes) {
        return false;
    }

    uint64_t last_row = framebuffer->height - 1;

    if (
        last_row != 0 &&
        framebuffer->pitch >
            UINT64_MAX / last_row
    ) {
        return false;
    }

    uint64_t last_row_offset = last_row * framebuffer->pitch;

    if (last_row_offset > UINT64_MAX - visible_row_bytes) {
        return false;
    }

    uint64_t visible_extent = last_row_offset + visible_row_bytes;
    uintptr_t base = (uintptr_t) framebuffer->address;

    if (base > UINTPTR_MAX - (visible_extent - 1)) {
        return false;
    }

    return true;
}

static bool framebuffer_pixel_pointer(
    struct framebuffer *framebuffer,
    uint64_t x,
    uint64_t y,
    uint32_t **pixel)
{
    if (pixel == NULL) return false;

    if (!framebuffer_geometry_valid(framebuffer)) {
        return false;
    }

    if (x >= framebuffer->width) return false;
    if (y >= framebuffer->height) return false;

    uint64_t offset =
        y * framebuffer->pitch +
        x * FRAMEBUFFER_BYTES_PER_PIXEL;

    *pixel =
        (uint32_t *) (
            (uint8_t *) framebuffer->address +
            offset
        );

    return true;
}

void framebuffer_put_pixel(
    struct framebuffer *framebuffer,
    uint64_t x,
    uint64_t y,
    uint32_t color)
{
    uint32_t *pixel;

    if (!framebuffer_pixel_pointer(framebuffer, x, y, &pixel)) {
        return;
    }

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
    if (!framebuffer_geometry_valid(framebuffer)) {
        return;
    }

    if (width == 0 || height == 0) {
        return;
    }

    if (x >= framebuffer->width || y >= framebuffer->height) {
        return;
    }

    uint64_t available_width = framebuffer->width - x;
    uint64_t available_height = framebuffer->height - y;
    uint64_t clipped_width =
        width < available_width
        ? width
        : available_width;
    uint64_t clipped_height =
        height < available_height
        ? height
        : available_height;

    uint64_t x_offset = x * FRAMEBUFFER_BYTES_PER_PIXEL;

    uint8_t *base = (uint8_t *) framebuffer->address;

    for (uint64_t row = 0;
         row < clipped_height;
         ++row) {

        uint64_t row_offset = (y + row) * framebuffer->pitch;

        uint32_t *pixels =
            (uint32_t *) (
                base +
                row_offset +
                x_offset
            );

        for (uint64_t column = 0;
             column < clipped_width;
             ++column) {
            pixels[column] = color;
        }
    }
}

void framebuffer_copy_rect(
    struct framebuffer *framebuffer,
    uint64_t source_x,
    uint64_t source_y,
    uint64_t destination_x,
    uint64_t destination_y,
    uint64_t width,
    uint64_t height)
{
    if (!framebuffer_geometry_valid(framebuffer)) {
        return;
    }

    if (width == 0 || height == 0) {
        return;
    }

    if (
        source_x >= framebuffer->width ||
        source_y >= framebuffer->height ||
        destination_x >= framebuffer->width ||
        destination_y >= framebuffer->height
    ) {
        return;
    }

    uint64_t source_available_width =
        framebuffer->width - source_x;

    uint64_t destination_available_width =
        framebuffer->width - destination_x;

    uint64_t source_available_height =
        framebuffer->height - source_y;

    uint64_t destination_available_height =
        framebuffer->height - destination_y;

    uint64_t clipped_width = width;

    if (clipped_width > source_available_width) {
        clipped_width = source_available_width;
    }

    if (clipped_width > destination_available_width) {
        clipped_width = destination_available_width;
    }

    uint64_t clipped_height = height;

    if (clipped_height > source_available_height) {
        clipped_height = source_available_height;
    }

    if (clipped_height > destination_available_height) {
        clipped_height = destination_available_height;
    }

    uint8_t *base =
        (uint8_t *) framebuffer->address;

    bool copy_rows_backward =
        destination_y > source_y &&
        destination_y - source_y < clipped_height;

    for (uint64_t row_index = 0;
         row_index < clipped_height;
         ++row_index) {

        uint64_t row =
            copy_rows_backward
            ? clipped_height - 1 - row_index
            : row_index;

        uint64_t source_row_offset =
            (source_y + row) * framebuffer->pitch;

        uint64_t destination_row_offset =
            (destination_y + row) * framebuffer->pitch;

        uint32_t *source =
            (uint32_t *) (
                base +
                source_row_offset +
                source_x * FRAMEBUFFER_BYTES_PER_PIXEL
            );

        uint32_t *destination =
            (uint32_t *) (
                base +
                destination_row_offset +
                destination_x * FRAMEBUFFER_BYTES_PER_PIXEL
            );

        bool copy_columns_backward =
            source_y == destination_y &&
            destination_x > source_x &&
            destination_x - source_x < clipped_width;

        if (copy_columns_backward) {
            for (uint64_t column = clipped_width;
                 column > 0;
                 --column) {
                destination[column - 1] =
                    source[column - 1];
            }

            continue;
        }

        for (uint64_t column = 0;
             column < clipped_width;
             ++column) {
            destination[column] =
                source[column];
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
    if (!framebuffer_geometry_valid(framebuffer)) {
        return;
    }

    if (glyph == NULL) return;
    if (scale == 0) return;

    if (x >= framebuffer->width || y >= framebuffer->height) {
        return;
    }

    uint64_t available_width = framebuffer->width - x;
    uint64_t available_height = framebuffer->height - y;

    for (uint64_t row = 0; row < 8; ++row) {
        uint64_t y_offset = row * (uint64_t) scale;

        if (y_offset >= available_height) {
            break;
        }

        for (uint64_t column = 0; column < 8; ++column) {
            uint8_t mask = (uint8_t) (1U << (7 - column));

            if ((glyph[row] & mask) == 0) {
                continue;
            }

            uint64_t x_offset = column * (uint64_t) scale;

            if (x_offset >= available_width) {
                break;
            }

            framebuffer_fill_rect(
                framebuffer,
                x + x_offset,
                y + y_offset,
                scale,
                scale,
                color
            );
        }
    }
}
