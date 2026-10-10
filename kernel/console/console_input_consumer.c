// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include "console_input_consumer.h"

#include "console_input.h"

#include <stddef.h>

void console_input_consumer_init(
    struct console_input_consumer *consumer)
{
    if (consumer == NULL) {
        return;
    }

    *consumer = (struct console_input_consumer) {0};
}

size_t console_input_consumer_drain(
    struct console_input_consumer *consumer,
    struct console_line_buffer *buffer,
    size_t budget)
{
    if (
        consumer == NULL ||
        buffer == NULL ||
        budget == 0
    ) {
        return 0;
    }

    size_t processed = 0;

    while (processed < budget) {
        if (!consumer->has_pending_event) {
            if (!input_event_pop(
                &consumer->pending_event
            )) {
                break;
            }

            consumer->has_pending_event = true;
        }

        enum console_input_result result =
            console_input_process_event(
                buffer,
                &consumer->pending_event
            );

        if (result == CONSOLE_INPUT_FULL) {
            /*
             * Preserve the event and stop draining.
             * Never consume a newer event first.
             */
            break;
        }

        consumer->has_pending_event = false;
        ++processed;
    }

    return processed;
}
