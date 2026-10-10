// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#ifndef MYOS_CONSOLE_CONSOLE_INPUT_CONSUMER_H
#define MYOS_CONSOLE_CONSOLE_INPUT_CONSUMER_H

#include "line_buffer.h"

#include "../input/input.h"

#include <stdbool.h>
#include <stddef.h>

struct console_input_consumer {
    bool has_pending_event;
    struct input_event pending_event;
};

void console_input_consumer_init(
    struct console_input_consumer *consumer
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
