// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#ifndef MYOS_CONSOLE_CONSOLE_INPUT_H
#define MYOS_CONSOLE_CONSOLE_INPUT_H

#include "line_buffer.h"

#include "../input/input.h"

enum console_input_result {
    CONSOLE_INPUT_ACCEPTED,
    CONSOLE_INPUT_IGNORED,
    CONSOLE_INPUT_FULL,
};

enum console_input_result console_input_process_event(
    struct console_line_buffer *buffer,
    const struct input_event *event
);

/**
 * Processes an event and optionally reports the
 * character accepted into the canonical buffer.
 *
 * accepted_character is written only on ACCEPTED.
 * On IGNORED or FULL its value is left untouched.
 */
enum console_input_result
console_input_process_event_with_character(
    struct console_line_buffer *buffer,
    const struct input_event *event,
    char *accepted_character
);

#endif
