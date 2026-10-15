// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include "console_render_test.h"

#include "../console/console.h"
#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../drivers/framebuffer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TEST_WIDTH 27U
#define TEST_HEIGHT 27U
#define TEST_PITCH_WORDS 31U
#define TEST_GUARD_WORDS 8U
#define TEST_SENTINEL 0xa5a5a5a5U
#define TEST_FOREGROUND 0x00ffffffU

static uint32_t test_storage[
    TEST_GUARD_WORDS +
    TEST_PITCH_WORDS * TEST_HEIGHT +
    TEST_GUARD_WORDS
];

static struct framebuffer test_framebuffer = {
    .address = &test_storage[TEST_GUARD_WORDS],
    .width = TEST_WIDTH,
    .height = TEST_HEIGHT,
    .pitch = TEST_PITCH_WORDS * sizeof(uint32_t),
    .bpp = 32U,
};

static uint32_t test_pixel(
    uint64_t x,
    uint64_t y)
{
    return test_storage[
        TEST_GUARD_WORDS +
        y * TEST_PITCH_WORDS +
        x
    ];
}

static void test_reset(void)
{
    size_t count =
        sizeof(test_storage) /
        sizeof(test_storage[0]);

    for (size_t i = 0U; i < count; ++i) {
        test_storage[i] = TEST_SENTINEL;
    }

    for (uint64_t y = 0U; y < TEST_HEIGHT; ++y) {
        for (uint64_t x = 0U; x < TEST_WIDTH; ++x) {
            test_storage[
                TEST_GUARD_WORDS +
                y * TEST_PITCH_WORDS + x
            ] = 0U;
        }
    }
}

static void test_expect_storage_safe(void)
{
    size_t frame_end =
        TEST_GUARD_WORDS +
        TEST_PITCH_WORDS * TEST_HEIGHT;

    size_t total =
        sizeof(test_storage) /
        sizeof(test_storage[0]);

    for (size_t i = 0U; i < TEST_GUARD_WORDS; ++i) {
        if (test_storage[i] != TEST_SENTINEL) {
            kernel_panic(
                "Console renderer changed leading guard"
            );
        }
    }

    for (size_t i = frame_end; i < total; ++i) {
        if (test_storage[i] != TEST_SENTINEL) {
            kernel_panic(
                "Console renderer changed trailing guard"
            );
        }
    }

    for (uint64_t y = 0U; y < TEST_HEIGHT; ++y) {
        for (
            uint64_t x = TEST_WIDTH;
            x < TEST_PITCH_WORDS;
            ++x
        ) {
            if (
                test_pixel(x, y) != TEST_SENTINEL
            ) {
                kernel_panic(
                    "Console renderer changed row padding"
                );
            }
        }
    }
}

static bool test_region_has_ink(
    uint64_t x,
    uint64_t y)
{
    for (uint64_t row = y; row < y + 8U; ++row) {
        for (
            uint64_t column = x;
            column < x + 8U;
            ++column
        ) {
            if (test_pixel(column, row) != 0U) {
                return true;
            }
        }
    }

    return false;
}

static void test_basic_erase(void)
{
    test_reset();

    struct console console;

    console_init(
        &console,
        &test_framebuffer,
        0U,
        0U,
        TEST_FOREGROUND,
        1U
    );

    console_put_char(&console, 'A');
    console_put_char(&console, 'B');

    if (
        !test_region_has_ink(0U, 0U) ||
        !test_region_has_ink(9U, 0U) ||
        !console_erase_previous(
            &console, 0U, 0U
        ) ||
        console.cursor_x != 9U ||
        console.cursor_y != 0U ||
        !test_region_has_ink(0U, 0U) ||
        test_region_has_ink(9U, 0U)
    ) {
        kernel_panic(
            "Console basic character erase failed"
        );
    }

    if (
        !console_erase_previous(
            &console, 0U, 0U
        ) ||
        console.cursor_x != 0U ||
        test_region_has_ink(0U, 0U) ||
        console_erase_previous(
            &console, 0U, 0U
        )
    ) {
        kernel_panic(
            "Console erase origin boundary failed"
        );
    }

    test_expect_storage_safe();
}

static void test_edit_boundary(void)
{
    test_reset();

    struct console console;

    console_init(
        &console,
        &test_framebuffer,
        0U,
        0U,
        TEST_FOREGROUND,
        1U
    );

    console_put_char(&console, '>');
    console_put_char(&console, ' ');

    uint64_t minimum_x = console.cursor_x;
    uint64_t minimum_y = console.cursor_y;

    console_put_char(&console, 'X');

    if (
        !console_erase_previous(
            &console,
            minimum_x,
            minimum_y
        ) ||
        console.cursor_x != minimum_x ||
        console.cursor_y != minimum_y ||
        console_erase_previous(
            &console,
            minimum_x,
            minimum_y
        ) ||
        !test_region_has_ink(0U, 0U) ||
        test_region_has_ink(18U, 0U)
    ) {
        kernel_panic(
            "Console erase crossed editing boundary"
        );
    }

    test_expect_storage_safe();
}

static void test_wrapped_erase(void)
{
    test_reset();

    struct console console;

    console_init(
        &console,
        &test_framebuffer,
        0U,
        0U,
        TEST_FOREGROUND,
        1U
    );

    console_put_char(&console, 'A');
    console_put_char(&console, 'B');
    console_put_char(&console, 'C');
    console_put_char(&console, 'D');

    if (
        console.cursor_x != 9U ||
        console.cursor_y != 9U ||
        !console_erase_previous(
            &console, 0U, 0U
        ) ||
        console.cursor_x != 0U ||
        console.cursor_y != 9U ||
        !console_erase_previous(
            &console, 0U, 0U
        ) ||
        console.cursor_x != 18U ||
        console.cursor_y != 0U ||
        test_region_has_ink(0U, 9U) ||
        test_region_has_ink(18U, 0U) ||
        !test_region_has_ink(9U, 0U)
    ) {
        kernel_panic(
            "Console wrapped character erase failed"
        );
    }

    test_expect_storage_safe();
}

static void test_scroll_tracking(void)
{
    test_reset();

    struct console console;

    console_init(
        &console,
        &test_framebuffer,
        0U,
        0U,
        TEST_FOREGROUND,
        1U
    );

    console_put_char(&console, '\n');
    console_put_char(&console, '\n');
    console_put_char(&console, '\n');

    if (
        console.scroll_count != 1U ||
        console.cursor_y != 18U ||
        console.cursor_x != 0U ||
        console_erase_previous(
            &console, 0U, 18U
        )
    ) {
        kernel_panic(
            "Console scroll tracking failed"
        );
    }

    test_expect_storage_safe();
}

static void test_offset_origin_scroll_erase(void)
{
    test_reset();

    struct console console;

    console_init(
        &console,
        &test_framebuffer,
        0U,
        2U,
        TEST_FOREGROUND,
        1U
    );

    /*
     * Simulate nonzero pixels below the last
     * complete text row.
     *
     * Scroll must not copy them into the editable
     * text area or erase them.
     */
    framebuffer_put_pixel(
        &test_framebuffer,
        25U,
        20U,
        TEST_FOREGROUND
    );

    /*
     * The framebuffer fits two text rows with
     * origin_y = 2. Seven characters trigger
     * wrapping followed by one scroll.
     */
    for (char character = 'A';
         character <= 'G';
         ++character) {
        console_put_char(&console, character);
    }

    if (
        test_pixel(25U, 11U) != 0U ||
        test_pixel(25U, 20U) !=
            TEST_FOREGROUND
    ) {
        kernel_panic(
            "Console scroll crossed text-grid boundary"
        );
    }

    if (
        console.scroll_count != 1U ||
        console.cursor_x != 9U ||
        console.cursor_y != 11U ||
        !test_region_has_ink(18U, 2U) ||
        !test_region_has_ink(0U, 11U)
    ) {
        kernel_panic(
            "Console offset-origin scroll alignment failed"
        );
    }

    /*
     * Erase G on the last row, then F on the
     * previous row. Both must use the same
     * origin-aligned text grid.
     */
    if (
        !console_erase_previous(
            &console, 0U, 2U
        ) ||
        console.cursor_x != 0U ||
        console.cursor_y != 11U ||
        test_region_has_ink(0U, 11U) ||
        !console_erase_previous(
            &console, 0U, 2U
        ) ||
        console.cursor_x != 18U ||
        console.cursor_y != 2U ||
        test_region_has_ink(18U, 2U) ||
        !test_region_has_ink(9U, 2U)
    ) {
        kernel_panic(
            "Console offset-origin wrapped erase failed"
        );
    }

    test_expect_storage_safe();
}

void console_render_test_run(void)
{
    test_basic_erase();
    test_edit_boundary();
    test_wrapped_erase();
    test_scroll_tracking();
    test_offset_origin_scroll_erase();

    if (
        console_erase_previous(
            NULL, 0U, 0U
        )
    ) {
        kernel_panic(
            "Console accepted NULL erase target"
        );
    }

    diagnostics_write(
        "[console] Renderer erase tests passed\n"
    );
}
