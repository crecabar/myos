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
#include <myos/kernel/core/panic.h>
#include <myos/kernel/diagnostics/diagnostics.h>
#include <myos/kernel/input/input.h>

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

struct consumer_accepted_probe {
    char characters[8];
    size_t count;
};

static void consumer_test_accepted_notification(
    void *context,
    char character)
{
    struct consumer_accepted_probe *probe = context;

    if (
        probe == NULL ||
        probe->count >= sizeof(probe->characters)
    ) {
        kernel_panic(
            "Console accepted observer received invalid state"
        );
    }

    probe->characters[probe->count++] = character;
}

struct consumer_control_probe {
    size_t calls;
};

static void consumer_test_control_notification(
    void *context)
{
    struct consumer_control_probe *probe = context;

    if (probe == NULL) {
        kernel_panic(
            "Console control observer invalid context"
        );
    }

    ++probe->calls;
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

static void consumer_test_accepted_notifications(void)
{
    input_init();

    struct console_input_consumer consumer;
    console_input_consumer_init(&consumer);

    struct console_line_buffer buffer;
    console_line_buffer_init(&buffer);

    struct consumer_accepted_probe probe = {0};

    console_input_consumer_accepted_observer_set(
        &consumer,
        consumer_test_accepted_notification,
        &probe
    );

    consumer_test_submit(
        INPUT_KEY_A, INPUT_KEY_PRESSED
    );
    consumer_test_submit(
        INPUT_KEY_BACKSPACE, INPUT_KEY_PRESSED
    );
    consumer_test_submit(
        INPUT_KEY_BACKSPACE, INPUT_KEY_PRESSED
    );
    consumer_test_submit(
        INPUT_KEY_B, INPUT_KEY_PRESSED
    );
    consumer_test_submit(
        INPUT_KEY_ENTER, INPUT_KEY_PRESSED
    );
    consumer_test_submit(
        INPUT_KEY_C, INPUT_KEY_RELEASED
    );

    if (
        console_input_consumer_drain(
            &consumer, &buffer, 8U
        ) != 6U ||
        probe.count != 4U ||
        probe.characters[0] != 'a' ||
        probe.characters[1] != '\b' ||
        probe.characters[2] != 'b' ||
        probe.characters[3] != '\n' ||
        consumer.has_pending_event ||
        input_event_pending() != 0U
    ) {
        kernel_panic(
            "Console accepted-character observer mismatch"
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
        length != 2U ||
        output[0] != 'b' ||
        output[1] != '\n'
    ) {
        kernel_panic(
            "Console observer changed canonical input"
        );
    }

    console_input_consumer_accepted_observer_set(
        &consumer,
        NULL,
        NULL
    );

    consumer_test_submit(
        INPUT_KEY_D, INPUT_KEY_PRESSED
    );

    if (
        console_input_consumer_drain(
            &consumer, &buffer, 1U
        ) != 1U ||
        probe.count != 4U ||
        buffer.editing_length != 1U
    ) {
        kernel_panic(
            "Console observer removal failed"
        );
    }
}

static void consumer_test_accepted_backpressure(void)
{
    input_init();

    struct console_input_consumer consumer;
    console_input_consumer_init(&consumer);

    struct console_line_buffer buffer;
    console_line_buffer_init(&buffer);

    /*
     * Saturate the canonical completed-line queue
     * without passing through the observer.
     */
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
                "Console observer fixture setup failed"
            );
        }
    }

    struct consumer_accepted_probe probe = {0};

    console_input_consumer_accepted_observer_set(
        &consumer,
        consumer_test_accepted_notification,
        &probe
    );

    consumer_test_submit(
        INPUT_KEY_A, INPUT_KEY_PRESSED
    );
    consumer_test_submit(
        INPUT_KEY_B, INPUT_KEY_PRESSED
    );
    consumer_test_submit(
        INPUT_KEY_ENTER, INPUT_KEY_PRESSED
    );

    if (
        console_input_consumer_drain(
            &consumer, &buffer, 8U
        ) != 0U ||
        !consumer.has_pending_event ||
        probe.count != 0U ||
        input_event_pending() != 2U
    ) {
        kernel_panic(
            "Console observer notified saturated input"
        );
    }

    /*
     * Retrying the same blocked event must not
     * produce a notification either.
     */
    if (
        console_input_consumer_drain(
            &consumer, &buffer, 8U
        ) != 0U ||
        probe.count != 0U
    ) {
        kernel_panic(
            "Console observer duplicated pending input"
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
        output[0] != '\n'
    ) {
        kernel_panic(
            "Console observer fixture read failed"
        );
    }

    if (
        console_input_consumer_drain(
            &consumer, &buffer, 8U
        ) != 3U ||
        probe.count != 3U ||
        probe.characters[0] != 'a' ||
        probe.characters[1] != 'b' ||
        probe.characters[2] != '\n' ||
        consumer.has_pending_event ||
        input_event_pending() != 0U
    ) {
        kernel_panic(
            "Console observer backpressure recovery failed"
        );
    }
}

static void consumer_test_control_eof(void)
{
    input_init();

    struct console_input_consumer consumer;
    console_input_consumer_init(&consumer);

    struct console_line_buffer buffer;
    console_line_buffer_init(&buffer);

    struct consumer_accepted_probe probe = {0};

    console_input_consumer_accepted_observer_set(
        &consumer,
        consumer_test_accepted_notification,
        &probe
    );

    struct consumer_control_probe control = {0};

    console_input_consumer_control_observer_set(
        &consumer,
        consumer_test_control_notification,
        &control
    );

    /*
     * input_event_submit() maintains modifier
     * snapshots, so submit physical Ctrl make
     * and break events.
     */
    consumer_test_submit(
        INPUT_KEY_LEFT_CTRL, INPUT_KEY_PRESSED
    );
    consumer_test_submit(
        INPUT_KEY_D, INPUT_KEY_PRESSED
    );
    consumer_test_submit(
        INPUT_KEY_LEFT_CTRL, INPUT_KEY_RELEASED
    );
    consumer_test_submit(
        INPUT_KEY_A, INPUT_KEY_PRESSED
    );
    consumer_test_submit(
        INPUT_KEY_ENTER, INPUT_KEY_PRESSED
    );

    if (
        console_input_consumer_drain(
            &consumer,
            &buffer,
            8U
        ) != 5U ||
        consumer.has_pending_event ||
        input_event_pending() != 0U ||
        probe.count != 2U ||
        probe.characters[0] != 'a' ||
        probe.characters[1] != '\n' ||
        control.calls != 1U ||
        buffer.completed_count != 2U
    ) {
        kernel_panic(
            "Console Ctrl+D consumer notification failed"
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
        !console_line_buffer_read(
            &buffer,
            output,
            sizeof(output),
            &length
        ) ||
        length != 2U ||
        output[0] != 'a' ||
        output[1] != '\n' ||
        buffer.completed_count != 0U
    ) {
        kernel_panic(
            "Console Ctrl+D consumer FIFO failed"
        );
    }
}

static void consumer_test_control_eof_backpressure(void)
{
    input_init();

    struct console_input_consumer consumer;
    console_input_consumer_init(&consumer);

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
                "Console Ctrl+D consumer fixture failed"
            );
        }
    }

    struct consumer_accepted_probe probe = {0};

    console_input_consumer_accepted_observer_set(
        &consumer,
        consumer_test_accepted_notification,
        &probe
    );

    struct consumer_control_probe control = {0};

    console_input_consumer_control_observer_set(
        &consumer,
        consumer_test_control_notification,
        &control
    );

    consumer_test_submit(
        INPUT_KEY_LEFT_CTRL, INPUT_KEY_PRESSED
    );
    consumer_test_submit(
        INPUT_KEY_D, INPUT_KEY_PRESSED
    );
    consumer_test_submit(
        INPUT_KEY_LEFT_CTRL, INPUT_KEY_RELEASED
    );

    if (
        console_input_consumer_drain(
            &consumer, &buffer, 8U
        ) != 1U ||
        !consumer.has_pending_event ||
        consumer.pending_event.key.code !=
            INPUT_KEY_D ||
        input_event_pending() != 1U ||
        control.calls != 0U ||
        probe.count != 0U
    ) {
        kernel_panic(
            "Console Ctrl+D pending event lost"
        );
    }

    if (
        console_input_consumer_drain(
            &consumer, &buffer, 8U
        ) != 0U ||
        probe.count != 0U
    ) {
        kernel_panic(
            "Console Ctrl+D pending event duplicated"
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
        console_input_consumer_drain(
            &consumer, &buffer, 8U
        ) != 2U ||
        consumer.has_pending_event ||
        input_event_pending() != 0U ||
        control.calls != 1U ||
        probe.count != 0U
    ) {
        kernel_panic(
            "Console Ctrl+D pending recovery failed"
        );
    }

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
                "Console Ctrl+D changed earlier records"
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
            "Console Ctrl+D was not delivered once"
        );
    }
}

void console_input_consumer_test_run(void)
{
    consumer_test_basic();
    consumer_test_ignored();
    consumer_test_backpressure();
    consumer_test_empty_and_invalid();

    consumer_test_accepted_notifications();
    consumer_test_accepted_backpressure();

    consumer_test_control_eof();
    consumer_test_control_eof_backpressure();

    diagnostics_write(
        "[console] Input consumer tests passed\n"
    );
}
