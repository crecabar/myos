// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include <myos/kernel/console/console_input.h>

#include <myos/kernel/input/keyboard_text.h>

#include <stddef.h>

enum console_input_result console_input_process_event(
    struct console_line_buffer *buffer,
    const struct input_event *event)
{
    return console_input_process_event_with_character(
        buffer,
        event,
        NULL
    );
}

enum console_input_result
console_input_process_event_with_character(
    struct console_line_buffer *buffer,
    const struct input_event *event,
    char *accepted_character)
{
    if (buffer == NULL || event == NULL) {
        return CONSOLE_INPUT_IGNORED;
    }

    if (event->type != INPUT_EVENT_KEY) {
        return CONSOLE_INPUT_IGNORED;
    }

    /*
     * Control combinations belong to canonical
     * input processing, not text translation.
     *
     * Ctrl+D is currently the only supported
     * canonical control operation.
     */
    if (
        event->key.state == INPUT_KEY_PRESSED
    ) {
        const uint16_t control_mask =
            INPUT_MODIFIER_LEFT_CTRL |
            INPUT_MODIFIER_RIGHT_CTRL;

        const uint16_t reserved_mask =
            INPUT_MODIFIER_LEFT_ALT |
            INPUT_MODIFIER_RIGHT_ALT |
            INPUT_MODIFIER_LEFT_GUI |
            INPUT_MODIFIER_RIGHT_GUI;

        if (
            (event->key.modifiers & control_mask) != 0U
        ) {
            if (
                event->key.code != INPUT_KEY_D ||
                (
                    event->key.modifiers &
                    reserved_mask
                ) != 0U
            ) {
                return CONSOLE_INPUT_IGNORED;
            }

            enum console_line_input_result result =
                console_line_buffer_submit_eof(
                    buffer
                );

            switch (result) {
                case CONSOLE_LINE_INPUT_ACCEPTED:
                    return CONSOLE_INPUT_ACCEPTED_CONTROL;

                case CONSOLE_LINE_INPUT_FULL:
                    return CONSOLE_INPUT_FULL;

                case CONSOLE_LINE_INPUT_IGNORED:
                    return CONSOLE_INPUT_IGNORED;
            }

            return CONSOLE_INPUT_IGNORED;
        }
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
            if (accepted_character != NULL) {
                *accepted_character = character;
            }

            return CONSOLE_INPUT_ACCEPTED;

        case CONSOLE_LINE_INPUT_IGNORED:
            return CONSOLE_INPUT_IGNORED;

        case CONSOLE_LINE_INPUT_FULL:
            return CONSOLE_INPUT_FULL;
    }

    return CONSOLE_INPUT_IGNORED;
}
