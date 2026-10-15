// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file console_input_test.c
 * @brief Console input adapter regression tests.
 */

#include "console_input_test.h"

#include "../console/console_input.h"
#include "../console/line_buffer.h"
#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../input/input.h"

#include <stddef.h>
#include <stdint.h>

static enum console_input_result console_input_test_key(
    struct console_line_buffer *buffer,
    enum input_key_code code,
    enum input_key_state state,
    uint16_t modifiers,
    uint8_t locks)
{
    struct input_event event = {
        .type = INPUT_EVENT_KEY,
        .key = {
            .code = code,
            .state = state,
            .modifiers = modifiers,
            .locks = locks,
        },
    };

    return console_input_process_event(
        buffer,
        &event
    );
}

static void console_input_test_text(void)
{
    struct console_line_buffer buffer;
    console_line_buffer_init(&buffer);

    if (
        console_input_test_key(
            &buffer,
            INPUT_KEY_A,
            INPUT_KEY_PRESSED,
            0,
            0
        ) != CONSOLE_INPUT_ACCEPTED ||
        console_input_test_key(
            &buffer,
            INPUT_KEY_B,
            INPUT_KEY_PRESSED,
            INPUT_MODIFIER_LEFT_SHIFT,
            0
        ) != CONSOLE_INPUT_ACCEPTED ||
        console_input_test_key(
            &buffer,
            INPUT_KEY_C,
            INPUT_KEY_PRESSED,
            0,
            INPUT_LOCK_CAPS
        ) != CONSOLE_INPUT_ACCEPTED ||
        console_input_test_key(
            &buffer,
            INPUT_KEY_MINUS,
            INPUT_KEY_PRESSED,
            0,
            0
        ) != CONSOLE_INPUT_ACCEPTED ||
        console_input_test_key(
            &buffer,
            INPUT_KEY_SLASH,
            INPUT_KEY_PRESSED,
            0,
            0
        ) != CONSOLE_INPUT_ACCEPTED ||
        console_input_test_key(
            &buffer,
            INPUT_KEY_ENTER,
            INPUT_KEY_PRESSED,
            0,
            0
        ) != CONSOLE_INPUT_ACCEPTED
    ) {
        kernel_panic(
            "Console input text translation failed"
        );
    }

    char output[8] = {0};
    size_t length = 0;

    if (
        !console_line_buffer_read(
            &buffer,
            output,
            sizeof(output),
            &length
        ) ||
        length != 6U ||
        output[0] != 'a' ||
        output[1] != 'B' ||
        output[2] != 'C' ||
        output[3] != '-' ||
        output[4] != '/' ||
        output[5] != '\n'
    ) {
        kernel_panic(
            "Console input translated line mismatch"
        );
    }
}

static void console_input_test_punctuation(void)
{
    struct console_line_buffer buffer;
    console_line_buffer_init(&buffer);

    if (
        console_input_test_key(
            &buffer,
            INPUT_KEY_A,
            INPUT_KEY_PRESSED,
            0U,
            0U
        ) != CONSOLE_INPUT_ACCEPTED ||
        console_input_test_key(
            &buffer,
            INPUT_KEY_COMMA,
            INPUT_KEY_PRESSED,
            0U,
            0U
        ) != CONSOLE_INPUT_ACCEPTED ||
        console_input_test_key(
            &buffer,
            INPUT_KEY_B,
            INPUT_KEY_PRESSED,
            0U,
            0U
        ) != CONSOLE_INPUT_ACCEPTED ||
        console_input_test_key(
            &buffer,
            INPUT_KEY_PERIOD,
            INPUT_KEY_PRESSED,
            0U,
            0U
        ) != CONSOLE_INPUT_ACCEPTED ||
        console_input_test_key(
            &buffer,
            INPUT_KEY_C,
            INPUT_KEY_PRESSED,
            0U,
            0U
        ) != CONSOLE_INPUT_ACCEPTED ||
        console_input_test_key(
            &buffer,
            INPUT_KEY_APOSTROPHE,
            INPUT_KEY_PRESSED,
            0U,
            INPUT_LOCK_CAPS
        ) != CONSOLE_INPUT_ACCEPTED ||
        console_input_test_key(
            &buffer,
            INPUT_KEY_APOSTROPHE,
            INPUT_KEY_PRESSED,
            INPUT_MODIFIER_LEFT_SHIFT,
            0U
        ) != CONSOLE_INPUT_ACCEPTED
    ) {
        kernel_panic(
            "Console punctuation translation failed"
        );
    }

    /*
     * Shifted comma and period are not yet
     * supported by the bootstrap layout.
     */
    if (
        console_input_test_key(
            &buffer,
            INPUT_KEY_COMMA,
            INPUT_KEY_PRESSED,
            INPUT_MODIFIER_LEFT_SHIFT,
            0U
        ) != CONSOLE_INPUT_IGNORED ||
        console_input_test_key(
            &buffer,
            INPUT_KEY_PERIOD,
            INPUT_KEY_PRESSED,
            INPUT_MODIFIER_RIGHT_SHIFT,
            0U
        ) != CONSOLE_INPUT_IGNORED ||
        console_input_test_key(
            &buffer,
            INPUT_KEY_ENTER,
            INPUT_KEY_PRESSED,
            0U,
            0U
        ) != CONSOLE_INPUT_ACCEPTED
    ) {
        kernel_panic(
            "Console punctuation modifier contract failed"
        );
    }

    static const char expected[] = "a,b.c'\"\n";

    char output[sizeof(expected)] = {0};
    size_t length = 0U;

    if (
        !console_line_buffer_read(
            &buffer,
            output,
            sizeof(output),
            &length
        ) ||
        length != sizeof(expected) - 1U
    ) {
        kernel_panic(
            "Console punctuation line length mismatch"
        );
    }

    for (size_t index = 0U; index < length; ++index) {
        if (output[index] != expected[index]) {
            kernel_panic(
                "Console punctuation canonical bytes mismatch"
            );
        }
    }
}

static void console_input_test_editing(void)
{
    struct console_line_buffer buffer;
    console_line_buffer_init(&buffer);

    if (
        console_input_test_key(
            &buffer, INPUT_KEY_A, INPUT_KEY_PRESSED, 0, 0
        ) != CONSOLE_INPUT_ACCEPTED ||
        console_input_test_key(
            &buffer, INPUT_KEY_B, INPUT_KEY_PRESSED, 0, 0
        ) != CONSOLE_INPUT_ACCEPTED ||
        console_input_test_key(
            &buffer, INPUT_KEY_BACKSPACE, INPUT_KEY_PRESSED, 0, 0
        ) != CONSOLE_INPUT_ACCEPTED ||
        console_input_test_key(
            &buffer, INPUT_KEY_ENTER, INPUT_KEY_PRESSED, 0, 0
        ) != CONSOLE_INPUT_ACCEPTED
    ) {
        kernel_panic(
            "Console input editing failed"
        );
    }

    char output[4] = {0};
    size_t length = 0;

    if (
        !console_line_buffer_read(
            &buffer, output, sizeof(output), &length
        ) ||
        length != 2U ||
        output[0] != 'a' ||
        output[1] != '\n'
    ) {
        kernel_panic(
            "Console input backspace result mismatch"
        );
    }
}

static void console_input_test_ignored(void)
{
    struct console_line_buffer buffer;
    console_line_buffer_init(&buffer);

    struct input_event pointer = {
        .type = INPUT_EVENT_POINTER,
    };

    if (
        console_input_process_event(
            &buffer, &pointer
        ) != CONSOLE_INPUT_IGNORED ||
        console_input_test_key(
            &buffer, INPUT_KEY_A, INPUT_KEY_RELEASED, 0, 0
        ) != CONSOLE_INPUT_IGNORED ||
        console_input_test_key(
            &buffer, INPUT_KEY_F1, INPUT_KEY_PRESSED, 0, 0
        ) != CONSOLE_INPUT_IGNORED ||
        console_input_process_event(
            &buffer, NULL
        ) != CONSOLE_INPUT_IGNORED ||
        console_input_process_event(
            NULL, &pointer
        ) != CONSOLE_INPUT_IGNORED
    ) {
        kernel_panic(
            "Console input ignored-event contract failed"
        );
    }

    if (buffer.editing_length != 0 ||
        buffer.completed_count != 0) {
        kernel_panic(
            "Ignored console input modified line state"
        );
    }
}

static void console_input_test_full(void)
{
    struct console_line_buffer buffer;
    console_line_buffer_init(&buffer);

    for (
        size_t index = 0;
        index < CONSOLE_LINE_QUEUE_CAPACITY;
        ++index
    ) {
        if (
            console_input_test_key(
                &buffer, INPUT_KEY_ENTER, INPUT_KEY_PRESSED, 0, 0
            ) != CONSOLE_INPUT_ACCEPTED
        ) {
            kernel_panic(
                "Console input queue fill failed"
            );
        }
    }

    if (
        console_input_test_key(
            &buffer, INPUT_KEY_A, INPUT_KEY_PRESSED, 0, 0
        ) != CONSOLE_INPUT_FULL ||
        buffer.completed_count !=
            CONSOLE_LINE_QUEUE_CAPACITY ||
        buffer.editing_length != 0
    ) {
        kernel_panic(
            "Console input saturation contract failed"
        );
    }

    char output[4];
    size_t length = 0;

    if (
        !console_line_buffer_read(
            &buffer, output, sizeof(output), &length
        ) ||
        length != 1U ||
        output[0] != '\n' ||
        console_input_test_key(
            &buffer, INPUT_KEY_A, INPUT_KEY_PRESSED, 0, 0
        ) != CONSOLE_INPUT_ACCEPTED
    ) {
        kernel_panic(
            "Console input saturation recovery failed"
        );
    }
}

static void console_input_test_control_eof(void)
{
    struct console_line_buffer buffer;
    console_line_buffer_init(&buffer);

    struct input_event event = {
        .type = INPUT_EVENT_KEY,
        .key = {
            .code = INPUT_KEY_D,
            .state = INPUT_KEY_PRESSED,
            .modifiers = INPUT_MODIFIER_LEFT_CTRL,
            .locks = INPUT_LOCK_CAPS,
        },
    };

    char accepted_character = '?';

    if (
        console_input_process_event_with_character(
            &buffer,
            &event,
            &accepted_character
        ) != CONSOLE_INPUT_ACCEPTED_CONTROL ||
        accepted_character != '?' ||
        buffer.completed_count != 1U ||
        !buffer.completed_eof[buffer.completed_head]
    ) {
        kernel_panic(
            "Console Ctrl+D EOF translation failed"
        );
    }

    char output[4] = {0};
    size_t length = SIZE_MAX;

    if (
        !console_line_buffer_read(
            &buffer,
            output,
            sizeof(output),
            &length
        ) ||
        length != 0U ||
        console_line_buffer_read(
            &buffer,
            output,
            sizeof(output),
            &length
        )
    ) {
        kernel_panic(
            "Console Ctrl+D one-shot EOF failed"
        );
    }

    /*
     * Ctrl+D also flushes pending text without
     * appending a newline.
     */
    if (
        console_input_test_key(
            &buffer,
            INPUT_KEY_A,
            INPUT_KEY_PRESSED,
            0U,
            0U
        ) != CONSOLE_INPUT_ACCEPTED ||
        console_input_test_key(
            &buffer,
            INPUT_KEY_B,
            INPUT_KEY_PRESSED,
            0U,
            0U
        ) != CONSOLE_INPUT_ACCEPTED
    ) {
        kernel_panic(
            "Console Ctrl+D text setup failed"
        );
    }

    event.key.modifiers =
        INPUT_MODIFIER_RIGHT_CTRL;

    if (
        console_input_process_event_with_character(
            &buffer,
            &event,
            &accepted_character
        ) != CONSOLE_INPUT_ACCEPTED_CONTROL ||
        accepted_character != '?' ||
        !console_line_buffer_read(
            &buffer,
            output,
            sizeof(output),
            &length
        ) ||
        length != 2U ||
        output[0] != 'a' ||
        output[1] != 'b' ||
        buffer.completed_count != 0U
    ) {
        kernel_panic(
            "Console Ctrl+D pending-text flush failed"
        );
    }

    /*
     * Other Ctrl combinations must not fall
     * through and insert printable characters.
     */
    event.key.code = INPUT_KEY_C;
    event.key.modifiers =
        INPUT_MODIFIER_LEFT_CTRL;

    if (
        console_input_process_event(
            &buffer,
            &event
        ) != CONSOLE_INPUT_IGNORED
    ) {
        kernel_panic(
            "Console inserted unsupported Ctrl sequence"
        );
    }

    event.key.code = INPUT_KEY_D;
    event.key.state = INPUT_KEY_RELEASED;

    if (
        console_input_process_event(
            &buffer,
            &event
        ) != CONSOLE_INPUT_IGNORED
    ) {
        kernel_panic(
            "Console accepted released Ctrl+D"
        );
    }

    event.key.state = INPUT_KEY_PRESSED;
    event.key.modifiers =
        INPUT_MODIFIER_LEFT_CTRL |
        INPUT_MODIFIER_LEFT_ALT;

    if (
        console_input_process_event(
            &buffer,
            &event
        ) != CONSOLE_INPUT_IGNORED ||
        buffer.completed_count != 0U ||
        buffer.editing_length != 0U
    ) {
        kernel_panic(
            "Console accepted reserved control chord"
        );
    }
}

static void console_input_test_control_eof_full(void)
{
    struct console_line_buffer buffer;
    console_line_buffer_init(&buffer);

    for (
        size_t index = 0U;
        index < CONSOLE_LINE_QUEUE_CAPACITY;
        ++index
    ) {
        if (
            console_line_buffer_submit(
                &buffer, '\n'
            ) != CONSOLE_LINE_INPUT_ACCEPTED
        ) {
            kernel_panic(
                "Console Ctrl+D full fixture failed"
            );
        }
    }

    struct input_event event = {
        .type = INPUT_EVENT_KEY,
        .key = {
            .code = INPUT_KEY_D,
            .state = INPUT_KEY_PRESSED,
            .modifiers = INPUT_MODIFIER_LEFT_CTRL,
        },
    };

    char accepted_character = '?';

    if (
        console_input_process_event_with_character(
            &buffer,
            &event,
            &accepted_character
        ) != CONSOLE_INPUT_FULL ||
        accepted_character != '?' ||
        buffer.completed_count !=
            CONSOLE_LINE_QUEUE_CAPACITY
    ) {
        kernel_panic(
            "Console Ctrl+D backpressure failed"
        );
    }

    char output[4] = {0};
    size_t length = 0U;

    if (
        !console_line_buffer_read(
            &buffer,
            output,
            sizeof(output),
            &length
        ) ||
        length != 1U ||
        output[0] != '\n' ||
        console_input_process_event_with_character(
            &buffer,
            &event,
            &accepted_character
        ) != CONSOLE_INPUT_ACCEPTED_CONTROL ||
        accepted_character != '?'
    ) {
        kernel_panic(
            "Console Ctrl+D backpressure recovery failed"
        );
    }

    /*
     * Three older records must precede EOF.
     */
    for (size_t index = 0U; index < 3U; ++index) {
        if (
            !console_line_buffer_read(
                &buffer,
                output,
                sizeof(output),
                &length
            ) ||
            length != 1U ||
            output[0] != '\n'
        ) {
            kernel_panic(
                "Console Ctrl+D changed FIFO ordering"
            );
        }
    }

    if (
        !console_line_buffer_read(
            &buffer,
            output,
            sizeof(output),
            &length
        ) ||
        length != 0U ||
        buffer.completed_count != 0U
    ) {
        kernel_panic(
            "Console Ctrl+D deferred EOF missing"
        );
    }
}

void console_input_test_run(void)
{
    console_input_test_text();
    console_input_test_punctuation();
    console_input_test_editing();
    console_input_test_ignored();
    console_input_test_full();
    console_input_test_control_eof();
    console_input_test_control_eof_full();

    diagnostics_write(
        "[console] Input adapter tests passed\n"
    );
}
