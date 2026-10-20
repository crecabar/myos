// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#ifndef MYOS_CONSOLE_CONSOLE_INPUT_H
#define MYOS_CONSOLE_CONSOLE_INPUT_H

#include "line_buffer.h"

#include <myos/kernel/input/input.h>

enum console_input_result {
    CONSOLE_INPUT_ACCEPTED,
    CONSOLE_INPUT_IGNORED,
    CONSOLE_INPUT_FULL,
    CONSOLE_INPUT_ACCEPTED_CONTROL,
};

enum console_input_result console_input_process_event(
    struct console_line_buffer *buffer,
    const struct input_event *event
);

/**
 * Processes a normalized input event.
 *
 * ACCEPTED reports one canonical character.
 * ACCEPTED_CONTROL reports a canonical operation
 * without producing a character for echo.
 *
 * accepted_character is written only on ACCEPTED.
 * Other results leave it unchanged.
 */
enum console_input_result
console_input_process_event_with_character(
    struct console_line_buffer *buffer,
    const struct input_event *event,
    char *accepted_character
);

#endif
