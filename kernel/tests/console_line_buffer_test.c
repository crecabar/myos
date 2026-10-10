// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include "console_line_buffer_test.h"

#include "../console/line_buffer.h"
#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"

#include <stddef.h>
#include <stdint.h>

static void console_line_buffer_test_line(void)
{
    struct console_line_buffer buffer;
    console_line_buffer_init(&buffer);

    char output[8] = {0};
    size_t length = SIZE_MAX;

    if (
        console_line_buffer_submit(&buffer, 'a') !=
            CONSOLE_LINE_INPUT_ACCEPTED ||
        console_line_buffer_submit(&buffer, 'b') !=
            CONSOLE_LINE_INPUT_ACCEPTED ||
        console_line_buffer_read(
            &buffer, output, sizeof(output), &length
        )
    ) {
        kernel_panic("Console accepted an incomplete line");
    }

    if (
        console_line_buffer_submit(&buffer, '\n') !=
            CONSOLE_LINE_INPUT_ACCEPTED ||
        !console_line_buffer_read(
            &buffer, output, sizeof(output), &length
        ) ||
        length != 3 ||
        output[0] != 'a' ||
        output[1] != 'b' ||
        output[2] != '\n'
    ) {
        kernel_panic("Console completed-line delivery failed");
    }
}

static void console_line_buffer_test_backspace(void)
{
    struct console_line_buffer buffer;
    console_line_buffer_init(&buffer);

    console_line_buffer_submit(&buffer, 'a');
    console_line_buffer_submit(&buffer, 'b');
    console_line_buffer_submit(&buffer, '\b');
    console_line_buffer_submit(&buffer, '\n');

    char output[8] = {0};
    size_t length = 0;

    if (
        !console_line_buffer_read(
            &buffer, output, sizeof(output), &length
        ) ||
        length != 2 ||
        output[0] != 'a' ||
        output[1] != '\n'
    ) {
        kernel_panic("Console line backspace failed");
    }
}

static void console_line_buffer_test_partial_read(void)
{
    struct console_line_buffer buffer;
    console_line_buffer_init(&buffer);

    console_line_buffer_submit(&buffer, 'a');
    console_line_buffer_submit(&buffer, 'b');
    console_line_buffer_submit(&buffer, '\n');

    char output[4] = {0};
    size_t length = 0;

    if (
        !console_line_buffer_read(&buffer, output, 1, &length) ||
        length != 1 ||
        output[0] != 'a'
    ) {
        kernel_panic("Console first partial read failed");
    }

    if (
        !console_line_buffer_read(&buffer, output, 2, &length) ||
        length != 2 ||
        output[0] != 'b' ||
        output[1] != '\n'
    ) {
        kernel_panic("Console remaining partial read failed");
    }

    if (console_line_buffer_read(&buffer, output, 1, &length)) {
        kernel_panic("Console returned consumed line twice");
    }
}

static void console_line_buffer_test_interleaved(void)
{
    struct console_line_buffer buffer;
    console_line_buffer_init(&buffer);

    const char *first = "abc\n";
    const char *second = "xy\n";

    for (size_t index = 0; index < 4U; ++index) {
        if (
            console_line_buffer_submit(&buffer, first[index]) !=
            CONSOLE_LINE_INPUT_ACCEPTED
        ) {
            kernel_panic("Console first line submission failed");
        }
    }

    char output[8] = {0};
    size_t length = 0;

    if (
        !console_line_buffer_read(&buffer, output, 2U, &length) ||
        length != 2U ||
        output[0] != 'a' ||
        output[1] != 'b'
    ) {
        kernel_panic("Console interleaved first read failed");
    }

    for (size_t index = 0; index < 3U; ++index) {
        if (
            console_line_buffer_submit(&buffer, second[index]) !=
            CONSOLE_LINE_INPUT_ACCEPTED
        ) {
            kernel_panic("Console second line submission failed");
        }
    }

    if (
        !console_line_buffer_read(&buffer, output, sizeof(output), &length) ||
        length != 2U ||
        output[0] != 'c' ||
        output[1] != '\n'
    ) {
        kernel_panic("Console interleaved remaining bytes failed");
    }

    if (
        !console_line_buffer_read(&buffer, output, sizeof(output), &length) ||
        length != 3U ||
        output[0] != 'x' ||
        output[1] != 'y' ||
        output[2] != '\n'
    ) {
        kernel_panic("Console interleaved FIFO ordering failed");
    }

    if (console_line_buffer_read(&buffer, output, sizeof(output), &length)) {
        kernel_panic("Console interleaved queue did not empty");
    }
}

static void console_line_buffer_test_wraparound(void)
{
    struct console_line_buffer buffer;
    console_line_buffer_init(&buffer);

    char output[4] = {0};
    size_t length = 0;

    for (size_t round = 0; round < 3U; ++round) {
        for (
            size_t index = 0;
            index < CONSOLE_LINE_QUEUE_CAPACITY;
            ++index
        ) {
            char character = (char) ('a' + round * 4U + index);

            if (
                console_line_buffer_submit(&buffer, character) !=
                    CONSOLE_LINE_INPUT_ACCEPTED ||
                console_line_buffer_submit(&buffer, '\n') !=
                    CONSOLE_LINE_INPUT_ACCEPTED
            ) {
                kernel_panic("Console wraparound submission failed");
            }
        }

        for (
            size_t index = 0;
            index < CONSOLE_LINE_QUEUE_CAPACITY;
            ++index
        ) {
            char expected = (char) ('a' + round * 4U + index);

            if (
                !console_line_buffer_read(
                    &buffer, output, sizeof(output), &length
                ) ||
                length != 2U ||
                output[0] != expected ||
                output[1] != '\n'
            ) {
                kernel_panic("Console wraparound FIFO mismatch");
            }
        }
    }

    if (
        buffer.completed_count != 0 ||
        buffer.completed_head != buffer.completed_tail
    ) {
        kernel_panic("Console wraparound state mismatch");
    }
}

static void console_line_buffer_test_pending(void)
{
    struct console_line_buffer buffer;
    console_line_buffer_init(&buffer);

    /*
     * Four completed lines must remain in FIFO order.
     */
    for (
        size_t index = 0;
        index < CONSOLE_LINE_QUEUE_CAPACITY;
        ++index
    ) {
        if (
            console_line_buffer_submit(
                &buffer,
                (char) ('a' + index)
            ) != CONSOLE_LINE_INPUT_ACCEPTED ||
            console_line_buffer_submit(
                &buffer,
                '\n'
            ) != CONSOLE_LINE_INPUT_ACCEPTED
        ) {
            kernel_panic(
                "Console FIFO line submission failed"
            );
        }
    }

    /*
     * Once full, new input must be rejected without
     * modifying any completed line.
     */
    if (
        console_line_buffer_submit(&buffer, 'z') !=
            CONSOLE_LINE_INPUT_FULL
    ) {
        kernel_panic(
            "Console FIFO accepted input while full"
        );
    }

    char output[4] = {0};
    size_t length = 0;

    for (
        size_t index = 0;
        index < CONSOLE_LINE_QUEUE_CAPACITY;
        ++index
    ) {
        if (
            !console_line_buffer_read(
                &buffer,
                output,
                sizeof(output),
                &length
            ) ||
            length != 2U ||
            output[0] != (char) ('a' + index) ||
            output[1] != '\n'
        ) {
            kernel_panic(
                "Console FIFO order failed"
            );
        }
    }

    /*
     * Consuming the queue must restore capacity.
     */
    if (
        console_line_buffer_submit(&buffer, 'z') !=
            CONSOLE_LINE_INPUT_ACCEPTED ||
        console_line_buffer_submit(&buffer, '\n') !=
            CONSOLE_LINE_INPUT_ACCEPTED ||
        !console_line_buffer_read(
            &buffer,
            output,
            sizeof(output),
            &length
        ) ||
        length != 2U ||
        output[0] != 'z' ||
        output[1] != '\n'
    ) {
        kernel_panic(
            "Console FIFO capacity recovery failed"
        );
    }
}

static void console_line_buffer_test_capacity(void)
{
    struct console_line_buffer buffer;
    console_line_buffer_init(&buffer);

    for (size_t index = 0;
         index < CONSOLE_LINE_CAPACITY - 1U;
         ++index) {
        if (
            console_line_buffer_submit(&buffer, 'x') !=
                CONSOLE_LINE_INPUT_ACCEPTED
        ) {
            kernel_panic("Console capacity fill failed");
        }
    }

    if (
        console_line_buffer_submit(&buffer, 'y') !=
            CONSOLE_LINE_INPUT_FULL ||
        console_line_buffer_submit(&buffer, '\n') !=
            CONSOLE_LINE_INPUT_ACCEPTED
    ) {
        kernel_panic("Console capacity boundary failed");
    }

    char output[CONSOLE_LINE_CAPACITY];
    size_t length = 0;

    if (
        !console_line_buffer_read(
            &buffer, output, sizeof(output), &length
        ) ||
        length != CONSOLE_LINE_CAPACITY ||
        output[length - 1U] != '\n'
    ) {
        kernel_panic("Console maximum line delivery failed");
    }
}

static void console_line_buffer_test_zero_read(void)
{
    struct console_line_buffer buffer;
    console_line_buffer_init(&buffer);

    console_line_buffer_submit(&buffer, 'a');
    console_line_buffer_submit(&buffer, '\n');

    size_t length = SIZE_MAX;

    if (
        !console_line_buffer_read(&buffer, NULL, 0, &length) ||
        length != 0
    ) {
        kernel_panic("Console zero-length read failed");
    }

    char output[2];

    if (
        !console_line_buffer_read(&buffer, output, 2, &length) ||
        length != 2 ||
        output[0] != 'a' ||
        output[1] != '\n'
    ) {
        kernel_panic("Console zero read consumed input");
    }
}

void console_line_buffer_test_run(void)
{
    console_line_buffer_test_line();
    console_line_buffer_test_backspace();
    console_line_buffer_test_partial_read();
    console_line_buffer_test_pending();
    console_line_buffer_test_capacity();
    console_line_buffer_test_zero_read();
    console_line_buffer_test_interleaved();
    console_line_buffer_test_wraparound();

    diagnostics_write(
        "[console] Line buffer tests passed\n"
    );
}
