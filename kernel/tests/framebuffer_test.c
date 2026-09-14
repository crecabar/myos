// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file framebuffer_test.c
 * @brief Framebuffer clipping and bounds regression tests.
 */

#include "framebuffer_test.h"

#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../drivers/framebuffer.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define FRAMEBUFFER_TEST_WIDTH 4
#define FRAMEBUFFER_TEST_HEIGHT 3

#define FRAMEBUFFER_TEST_VISIBLE_ROW_WORDS \
    FRAMEBUFFER_TEST_WIDTH

#define FRAMEBUFFER_TEST_ROW_WORDS 6

#define FRAMEBUFFER_TEST_PITCH \
    (FRAMEBUFFER_TEST_ROW_WORDS * sizeof(uint32_t))

#define FRAMEBUFFER_TEST_GUARD_WORDS 4

#define FRAMEBUFFER_TEST_FRAME_WORDS \
    (FRAMEBUFFER_TEST_ROW_WORDS * FRAMEBUFFER_TEST_HEIGHT)

#define FRAMEBUFFER_TEST_STORAGE_WORDS \
    (FRAMEBUFFER_TEST_GUARD_WORDS + \
     FRAMEBUFFER_TEST_FRAME_WORDS + \
     FRAMEBUFFER_TEST_GUARD_WORDS)

#define FRAMEBUFFER_TEST_SENTINEL \
    0xA5A5A5A5U

#define FRAMEBUFFER_TEST_COLOR \
    0x11223344U

static uint32_t framebuffer_test_storage[
    FRAMEBUFFER_TEST_STORAGE_WORDS
];

static struct framebuffer framebuffer_test_make(void);

static void framebuffer_test_reset(void);

static void framebuffer_test_expect_unchanged(void);

static void framebuffer_test_expect_only_pixel(
    uint64_t x,
    uint64_t y,
    uint32_t color
);

static void framebuffer_test_expect_region(
    uint64_t x,
    uint64_t y,
    uint64_t width,
    uint64_t height,
    uint32_t color
);

void framebuffer_test_run(void)
{
    struct framebuffer framebuffer =
        framebuffer_test_make();

    /*
     * A valid edge pixel must remain writable.
     */
    framebuffer_test_reset();

    framebuffer_put_pixel(
        &framebuffer,
        FRAMEBUFFER_TEST_WIDTH - 1,
        FRAMEBUFFER_TEST_HEIGHT - 1,
        FRAMEBUFFER_TEST_COLOR
    );

    framebuffer_test_expect_only_pixel(
        FRAMEBUFFER_TEST_WIDTH - 1,
        FRAMEBUFFER_TEST_HEIGHT - 1,
        FRAMEBUFFER_TEST_COLOR
    );

    /*
     * Fully out-of-bounds pixels must perform no writes.
     */
    framebuffer_test_reset();

    framebuffer_put_pixel(
        &framebuffer,
        FRAMEBUFFER_TEST_WIDTH,
        FRAMEBUFFER_TEST_HEIGHT - 1,
        FRAMEBUFFER_TEST_COLOR
    );

    framebuffer_put_pixel(
        &framebuffer,
        FRAMEBUFFER_TEST_WIDTH - 1,
        FRAMEBUFFER_TEST_HEIGHT,
        FRAMEBUFFER_TEST_COLOR
    );

    framebuffer_put_pixel(
        &framebuffer,
        UINT64_MAX,
        UINT64_MAX,
        FRAMEBUFFER_TEST_COLOR
    );

    framebuffer_test_expect_unchanged();

    /*
     * A huge rectangle beginning at the bottom-right pixel must clip to
     * exactly that one visible pixel without integer overflow.
     */
    framebuffer_test_reset();

    framebuffer_fill_rect(
        &framebuffer,
        FRAMEBUFFER_TEST_WIDTH - 1,
        FRAMEBUFFER_TEST_HEIGHT - 1,
        UINT64_MAX,
        UINT64_MAX,
        FRAMEBUFFER_TEST_COLOR
    );

    framebuffer_test_expect_only_pixel(
        FRAMEBUFFER_TEST_WIDTH - 1,
        FRAMEBUFFER_TEST_HEIGHT - 1,
        FRAMEBUFFER_TEST_COLOR
    );

    /*
     * A partially visible rectangle must stop at the visible right and
     * bottom boundaries without touching row padding.
     */
    framebuffer_test_reset();

    framebuffer_fill_rect(
        &framebuffer,
        2,
        1,
        UINT64_MAX,
        UINT64_MAX,
        FRAMEBUFFER_TEST_COLOR
    );

    framebuffer_test_expect_region(
        2,
        1,
        2,
        2,
        FRAMEBUFFER_TEST_COLOR
    );

    /*
     * Empty and fully out-of-bounds rectangles must perform no writes.
     */
    framebuffer_test_reset();

    framebuffer_fill_rect(
        &framebuffer,
        0,
        0,
        0,
        UINT64_MAX,
        FRAMEBUFFER_TEST_COLOR
    );

    framebuffer_fill_rect(
        &framebuffer,
        FRAMEBUFFER_TEST_WIDTH,
        0,
        UINT64_MAX,
        UINT64_MAX,
        FRAMEBUFFER_TEST_COLOR
    );

    framebuffer_test_expect_unchanged();

    /*
     * Scaled glyph drawing must inherit rectangle clipping. With a huge scale
     * at the bottom-right corner only one framebuffer pixel remains visible.
     */
    static const uint8_t full_glyph[8] = {
        0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF,
    };

    framebuffer_test_reset();

    framebuffer_draw_glyph(
        &framebuffer,
        FRAMEBUFFER_TEST_WIDTH - 1,
        FRAMEBUFFER_TEST_HEIGHT - 1,
        full_glyph,
        FRAMEBUFFER_TEST_COLOR,
        UINT32_MAX
    );

    framebuffer_test_expect_only_pixel(
        FRAMEBUFFER_TEST_WIDTH - 1,
        FRAMEBUFFER_TEST_HEIGHT - 1,
        FRAMEBUFFER_TEST_COLOR
    );

    /*
     * NULL glyphs and zero scales must perform no writes.
     */
    framebuffer_test_reset();

    framebuffer_draw_glyph(
        &framebuffer,
        0,
        0,
        NULL,
        FRAMEBUFFER_TEST_COLOR,
        1
    );

    framebuffer_draw_glyph(
        &framebuffer,
        0,
        0,
        full_glyph,
        FRAMEBUFFER_TEST_COLOR,
        0
    );

    framebuffer_test_expect_unchanged();

    /*
     * Geometry with a pitch smaller than one visible row is invalid and must
     * not permit framebuffer writes.
     */
    framebuffer_test_reset();

    uint64_t valid_pitch =
        framebuffer.pitch;

    framebuffer.pitch =
        (FRAMEBUFFER_TEST_WIDTH - 1) *
        sizeof(uint32_t);

    framebuffer_fill_rect(
        &framebuffer,
        0,
        0,
        FRAMEBUFFER_TEST_WIDTH,
        FRAMEBUFFER_TEST_HEIGHT,
        FRAMEBUFFER_TEST_COLOR
    );

    framebuffer_test_expect_unchanged();

    framebuffer.pitch =
        valid_pitch;

    diagnostics_write(
        "[framebuffer] Bounds and clipping test passed\n"
    );
}

static struct framebuffer framebuffer_test_make(void)
{
    struct framebuffer framebuffer = {
        .address =
            &framebuffer_test_storage[
                FRAMEBUFFER_TEST_GUARD_WORDS
            ],
        .width =
            FRAMEBUFFER_TEST_WIDTH,
        .height =
            FRAMEBUFFER_TEST_HEIGHT,
        .pitch =
            FRAMEBUFFER_TEST_PITCH,
        .bpp =
            32,
        .red_mask_size =
            8,
        .red_mask_shift =
            16,
        .green_mask_size =
            8,
        .green_mask_shift =
            8,
        .blue_mask_size =
            8,
        .blue_mask_shift =
            0,
    };

    return framebuffer;
}

static void framebuffer_test_reset(void)
{
    for (size_t index = 0;
         index < FRAMEBUFFER_TEST_STORAGE_WORDS;
         ++index) {
        framebuffer_test_storage[index] =
            FRAMEBUFFER_TEST_SENTINEL;
    }
}

static void framebuffer_test_expect_unchanged(void)
{
    for (size_t index = 0;
         index < FRAMEBUFFER_TEST_STORAGE_WORDS;
         ++index) {
        if (
            framebuffer_test_storage[index] !=
            FRAMEBUFFER_TEST_SENTINEL
        ) {
            kernel_panic(
                "Framebuffer out-of-bounds test modified protected memory"
            );
        }
    }
}

static void framebuffer_test_expect_only_pixel(
    uint64_t x,
    uint64_t y,
    uint32_t color)
{
    framebuffer_test_expect_region(
        x,
        y,
        1,
        1,
        color
    );
}

static void framebuffer_test_expect_region(
    uint64_t x,
    uint64_t y,
    uint64_t width,
    uint64_t height,
    uint32_t color)
{
    for (size_t index = 0;
         index < FRAMEBUFFER_TEST_STORAGE_WORDS;
         ++index) {

        uint32_t expected =
            FRAMEBUFFER_TEST_SENTINEL;

        if (
            index >= FRAMEBUFFER_TEST_GUARD_WORDS &&
            index <
                FRAMEBUFFER_TEST_GUARD_WORDS +
                FRAMEBUFFER_TEST_FRAME_WORDS
        ) {
            size_t framebuffer_index =
                index - FRAMEBUFFER_TEST_GUARD_WORDS;

            uint64_t row =
                framebuffer_index /
                FRAMEBUFFER_TEST_ROW_WORDS;

            uint64_t column =
                framebuffer_index %
                FRAMEBUFFER_TEST_ROW_WORDS;

            bool visible =
                column <
                FRAMEBUFFER_TEST_VISIBLE_ROW_WORDS;

            bool inside_expected_region =
                visible &&
                column >= x &&
                column - x < width &&
                row >= y &&
                row - y < height;

            if (inside_expected_region) {
                expected = color;
            }
        }

        if (framebuffer_test_storage[index] != expected) {
            kernel_panic(
                "Framebuffer clipping test produced unexpected memory writes"
            );
        }
    }
}
