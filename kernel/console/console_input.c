// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include "console_input.h"

#include "../input/keyboard_text.h"

#include <stddef.h>

enum console_input_result console_input_process_event(
    struct console_line_buffer *buffer,
    const struct input_event *event)
{
    if (buffer == NULL || event == NULL) {
        return CONSOLE_INPUT_IGNORED;
    }

    if (event->type != INPUT_EVENT_KEY) {
        return CONSOLE_INPUT_IGNORED;
    }

    char character;

    if (
        !keyboard_text_from_key_event(
            &event->key,
            &character
        )
    ) {
        return CONSOLE_INPUT_IGNORED;
    }

    enum console_line_input_result result =
        console_line_buffer_submit(
            buffer,
            character
        );

    switch (result) {
        case CONSOLE_LINE_INPUT_ACCEPTED:
            return CONSOLE_INPUT_ACCEPTED;

        case CONSOLE_LINE_INPUT_IGNORED:
            return CONSOLE_INPUT_IGNORED;

        case CONSOLE_LINE_INPUT_FULL:
            return CONSOLE_INPUT_FULL;
    }

    return CONSOLE_INPUT_IGNORED;
}
