// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#ifndef MYOS_CONSOLE_CONSOLE_INPUT_CONSUMER_H
#define MYOS_CONSOLE_CONSOLE_INPUT_CONSUMER_H

#include "line_buffer.h"

#include <myos/kernel/input/input.h>

#include <stdbool.h>
#include <stddef.h>

/**
 * Observes a character already accepted by the
 * canonical line buffer.
 *
 * Executes synchronously during input draining,
 * outside IRQ context. Must not block, consume
 * input, or recursively drain the same consumer.
 */
typedef void (*console_input_accepted_fn)(
    void *context,
    char character
);

/**
 * Observes an accepted canonical control operation.
 *
 * Currently emitted for Ctrl+D (EOF).
 * It does not represent a printable character.
 *
 * Executes outside IRQ context and must not block
 * or recursively drain the consumer.
 */
typedef void (*console_input_accepted_control_fn)(
    void *context
);

struct console_input_consumer {
    bool has_pending_event;
    struct input_event pending_event;
    console_input_accepted_fn accepted;
    void *accepted_context;
    console_input_accepted_control_fn accepted_control;
    void *accepted_control_context;
};

void console_input_consumer_init(
    struct console_input_consumer *consumer
);

/**
 * Sets or removes an accepted-character observer.
 *
 * NULL callback disables notifications.
 * The consumer does not own context.
 */
void console_input_consumer_accepted_observer_set(
    struct console_input_consumer *consumer,
    console_input_accepted_fn accepted,
    void *context
);

/**
 * Sets or removes the accepted-control observer.
 *
 * Currently reports accepted Ctrl+D operations.
 * NULL disables notifications.
 */
void console_input_consumer_control_observer_set(
    struct console_input_consumer *consumer,
    console_input_accepted_control_fn accepted_control,
    void *context
);

/**
 * Transfers at most budget events into the canonical input buffer.
 *
 * Events rejected because the line buffer is full remain pending.
 * Subsequent calls retry that event before removing another one.
 *
 * A zero budget performs no work.
 *
 * @return Number of accepted or ignored events completed.
 */
size_t console_input_consumer_drain(
    struct console_input_consumer *consumer,
    struct console_line_buffer *buffer,
    size_t budget
);

#endif
