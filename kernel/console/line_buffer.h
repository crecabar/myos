// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#ifndef MYOS_CONSOLE_LINE_BUFFER_H
#define MYOS_CONSOLE_LINE_BUFFER_H

#include <stdbool.h>
#include <stddef.h>

#define CONSOLE_LINE_CAPACITY 256U
#define CONSOLE_LINE_QUEUE_CAPACITY 4U

enum console_line_input_result {
    CONSOLE_LINE_INPUT_ACCEPTED,
    CONSOLE_LINE_INPUT_IGNORED,
    CONSOLE_LINE_INPUT_FULL,
};

struct console_line_buffer {
    char editing[CONSOLE_LINE_CAPACITY];
    size_t editing_length;

    char completed[
        CONSOLE_LINE_QUEUE_CAPACITY
    ][CONSOLE_LINE_CAPACITY];

    size_t completed_lengths[
        CONSOLE_LINE_QUEUE_CAPACITY
    ];

    size_t completed_head;
    size_t completed_tail;
    size_t completed_count;
    size_t completed_offset;
};

void console_line_buffer_init(
    struct console_line_buffer *buffer
);

enum console_line_input_result console_line_buffer_submit(
    struct console_line_buffer *buffer,
    char character
);

bool console_line_buffer_read(
    struct console_line_buffer *buffer,
    char *destination,
    size_t capacity,
    size_t *bytes_read
);

#endif
