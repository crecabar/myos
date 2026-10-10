// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file console_input_consumer_test.c
 * @brief Console input consumer and backpressure regressions.
 */

#include "console_input_consumer_test.h"

#include "../console/console_input_consumer.h"
#include "../console/line_buffer.h"
#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../input/input.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static void consumer_test_submit(
    enum input_key_code code,
    enum input_key_state state)
{
    struct input_event event = {
        .type = INPUT_EVENT_KEY,
        .key = {
            .code = code,
            .state = state,
        },
    };

    if (!input_event_submit(&event)) {
        kernel_panic(
            "Console consumer event submission failed"
        );
    }
}

static void consumer_test_basic(void)
{
    input_init();

    struct console_input_consumer consumer;
    console_input_consumer_init(&consumer);

    struct console_line_buffer buffer;
    console_line_buffer_init(&buffer);

    consumer_test_submit(INPUT_KEY_A, INPUT_KEY_PRESSED);
    consumer_test_submit(INPUT_KEY_B, INPUT_KEY_PRESSED);
    consumer_test_submit(INPUT_KEY_ENTER, INPUT_KEY_PRESSED);

    if (
        console_input_consumer_drain(
            &consumer, &buffer, 0U
        ) != 0U ||
        input_event_pending() != 3U ||
        console_input_consumer_drain(
            &consumer, &buffer, 2U
        ) != 2U ||
        input_event_pending() != 1U ||
        buffer.editing_length != 2U ||
        console_input_consumer_drain(
            &consumer, &buffer, 2U
        ) != 1U ||
        input_event_pending() != 0U ||
        consumer.has_pending_event
    ) {
        kernel_panic(
            "Console consumer budget contract failed"
        );
    }

    char output[4] = {0};
    size_t length = 0;

    if (
        !console_line_buffer_read(
            &buffer, output, sizeof(output), &length
        ) ||
        length != 3U ||
        output[0] != 'a' ||
        output[1] != 'b' ||
        output[2] != '\n'
    ) {
        kernel_panic(
            "Console consumer basic text mismatch"
        );
    }
}

static void consumer_test_ignored(void)
{
    input_init();

    struct console_input_consumer consumer;
    console_input_consumer_init(&consumer);

    struct console_line_buffer buffer;
    console_line_buffer_init(&buffer);

    consumer_test_submit(INPUT_KEY_A, INPUT_KEY_RELEASED);
    consumer_test_submit(INPUT_KEY_F1, INPUT_KEY_PRESSED);

    if (
        console_input_consumer_drain(
            &consumer, &buffer, 4U
        ) != 2U ||
        consumer.has_pending_event ||
        buffer.editing_length != 0U ||
        buffer.completed_count != 0U ||
        input_event_pending() != 0U
    ) {
        kernel_panic(
            "Console consumer ignored-event contract failed"
        );
    }
}

static void consumer_test_backpressure(void)
{
    input_init();

    struct console_input_consumer consumer;
    console_input_consumer_init(&consumer);

    struct console_line_buffer buffer;
    console_line_buffer_init(&buffer);

    /*
     * Fill all completed-line slots with empty lines.
     */
    for (
        size_t index = 0;
        index < CONSOLE_LINE_QUEUE_CAPACITY;
        ++index
    ) {
        consumer_test_submit(
            INPUT_KEY_ENTER,
            INPUT_KEY_PRESSED
        );
    }

    if (
        console_input_consumer_drain(
            &consumer, &buffer,
            CONSOLE_LINE_QUEUE_CAPACITY
        ) != CONSOLE_LINE_QUEUE_CAPACITY ||
        buffer.completed_count !=
            CONSOLE_LINE_QUEUE_CAPACITY
    ) {
        kernel_panic(
            "Console consumer queue setup failed"
        );
    }

    /*
     * The first rejected event must remain pending.
     * A newer event must stay in the global input queue.
     */
    consumer_test_submit(INPUT_KEY_A, INPUT_KEY_PRESSED);
    consumer_test_submit(INPUT_KEY_B, INPUT_KEY_PRESSED);
    consumer_test_submit(INPUT_KEY_ENTER, INPUT_KEY_PRESSED);

    if (
        console_input_consumer_drain(
            &consumer, &buffer, 8U
        ) != 0U ||
        !consumer.has_pending_event ||
        consumer.pending_event.key.code != INPUT_KEY_A ||
        input_event_pending() != 2U ||
        buffer.editing_length != 0U
    ) {
        kernel_panic(
            "Console consumer lost saturated event"
        );
    }

    /*
     * Repeated attempts must not remove later events.
     */
    if (
        console_input_consumer_drain(
            &consumer, &buffer, 8U
        ) != 0U ||
        input_event_pending() != 2U
    ) {
        kernel_panic(
            "Console consumer bypassed pending event"
        );
    }

    char output[4] = {0};
    size_t length = 0;

    if (
        !console_line_buffer_read(
            &buffer, output, sizeof(output), &length
        ) ||
        length != 1U ||
        output[0] != '\n'
    ) {
        kernel_panic(
            "Console consumer capacity release failed"
        );
    }

    /*
     * The retained A must precede the queued B and Enter.
     */
    if (
        console_input_consumer_drain(
            &consumer, &buffer, 8U
        ) != 3U ||
        consumer.has_pending_event ||
        input_event_pending() != 0U
    ) {
        kernel_panic(
            "Console consumer failed pending-event recovery"
        );
    }

    /*
     * Three older empty lines must be delivered first.
     */
    for (size_t index = 0; index < 3U; ++index) {
        if (
            !console_line_buffer_read(
                &buffer, output, sizeof(output), &length
            ) ||
            length != 1U ||
            output[0] != '\n'
        ) {
            kernel_panic(
                "Console consumer corrupted earlier FIFO lines"
            );
        }
    }

    if (
        !console_line_buffer_read(
            &buffer, output, sizeof(output), &length
        ) ||
        length != 3U ||
        output[0] != 'a' ||
        output[1] != 'b' ||
        output[2] != '\n'
    ) {
        kernel_panic(
            "Console consumer pending-event order failed"
        );
    }
}

static void consumer_test_empty_and_invalid(void)
{
    input_init();

    struct console_input_consumer consumer;
    console_input_consumer_init(&consumer);

    struct console_line_buffer buffer;
    console_line_buffer_init(&buffer);

    if (
        console_input_consumer_drain(
            &consumer, &buffer, 4U
        ) != 0U ||
        console_input_consumer_drain(
            NULL, &buffer, 4U
        ) != 0U ||
        console_input_consumer_drain(
            &consumer, NULL, 4U
        ) != 0U ||
        consumer.has_pending_event ||
        buffer.completed_count != 0U
    ) {
        kernel_panic(
            "Console consumer empty/invalid contract failed"
        );
    }
}

void console_input_consumer_test_run(void)
{
    consumer_test_basic();
    consumer_test_ignored();
    consumer_test_backpressure();
    consumer_test_empty_and_invalid();

    diagnostics_write(
        "[console] Input consumer tests passed\n"
    );
}
