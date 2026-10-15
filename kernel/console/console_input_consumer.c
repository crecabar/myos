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

void console_input_consumer_accepted_observer_set(
    struct console_input_consumer *consumer,
    console_input_accepted_fn accepted,
    void *context)
{
    if (consumer == NULL) {
        return;
    }

    consumer->accepted = accepted;

    consumer->accepted_context =
        accepted != NULL ? context : NULL;
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

        char accepted_character = '\0';

        enum console_input_result result =
            console_input_process_event_with_character(
                buffer,
                &consumer->pending_event,
                &accepted_character
            );

        if (result == CONSOLE_INPUT_FULL) {
            /*
             * Preserve the event and stop draining.
             * Never consume a newer event first.
             */
            break;
        }

        /*
         * Notify only after canonical acceptance.
         *
         * A retained FULL event must not produce
         * a duplicate visual or logical effect
         * when it is retried later.
         */
        if (
            result == CONSOLE_INPUT_ACCEPTED &&
            consumer->accepted != NULL
        ) {
            consumer->accepted(
                consumer->accepted_context,
                accepted_character
            );
        }

        consumer->has_pending_event = false;
        ++processed;
    }

    return processed;
}
