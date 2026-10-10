// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include "line_buffer.h"

#include <stddef.h>

void console_line_buffer_init(
    struct console_line_buffer *buffer)
{
    if (buffer == NULL) {
        return;
    }

    *buffer = (struct console_line_buffer) {0};
}

enum console_line_input_result console_line_buffer_submit(
    struct console_line_buffer *buffer,
    char character)
{
    if (buffer == NULL) {
        return CONSOLE_LINE_INPUT_IGNORED;
    }

    if (
        buffer->completed_count >=
        CONSOLE_LINE_QUEUE_CAPACITY
    ) {
        return CONSOLE_LINE_INPUT_FULL;
    }

    if (character == '\b') {
        if (buffer->editing_length == 0) {
            return CONSOLE_LINE_INPUT_IGNORED;
        }

        --buffer->editing_length;

        return CONSOLE_LINE_INPUT_ACCEPTED;
    }

    if (character == '\n') {
        size_t tail = buffer->completed_tail;

        /*
         * The editing buffer always reserves one
         * byte for the terminating newline.
         */
        buffer->editing[
            buffer->editing_length++
        ] = '\n';

        for (
            size_t index = 0;
            index < buffer->editing_length;
            ++index
        ) {
            buffer->completed[tail][index] =
                buffer->editing[index];
        }

        buffer->completed_lengths[tail] =
            buffer->editing_length;

        buffer->completed_tail =
            (tail + 1U) %
            CONSOLE_LINE_QUEUE_CAPACITY;

        ++buffer->completed_count;
        buffer->editing_length = 0;

        return CONSOLE_LINE_INPUT_ACCEPTED;
    }

    if (
        character < ' ' ||
        character > '~'
    ) {
        return CONSOLE_LINE_INPUT_IGNORED;
    }

    if (
        buffer->editing_length >=
        CONSOLE_LINE_CAPACITY - 1U
    ) {
        return CONSOLE_LINE_INPUT_FULL;
    }

    buffer->editing[
        buffer->editing_length++
    ] = character;

    return CONSOLE_LINE_INPUT_ACCEPTED;
}

bool console_line_buffer_read(
    struct console_line_buffer *buffer,
    char *destination,
    size_t capacity,
    size_t *bytes_read)
{
    if (
        buffer == NULL ||
        bytes_read == NULL ||
        (capacity != 0 && destination == NULL)
    ) {
        return false;
    }

    if (capacity == 0) {
        *bytes_read = 0;
        return true;
    }

    if (buffer->completed_count == 0) {
        return false;
    }

    size_t head = buffer->completed_head;

    size_t remaining =
        buffer->completed_lengths[head] -
        buffer->completed_offset;

    size_t count =
        remaining < capacity
            ? remaining
            : capacity;

    for (
        size_t index = 0;
        index < count;
        ++index
    ) {
        destination[index] =
            buffer->completed[head][
                buffer->completed_offset + index
            ];
    }

    buffer->completed_offset += count;

    if (
        buffer->completed_offset ==
        buffer->completed_lengths[head]
    ) {
        buffer->completed_lengths[head] = 0;
        buffer->completed_offset = 0;

        buffer->completed_head =
            (head + 1U) %
            CONSOLE_LINE_QUEUE_CAPACITY;

        --buffer->completed_count;
    }

    *bytes_read = count;

    return true;
}
