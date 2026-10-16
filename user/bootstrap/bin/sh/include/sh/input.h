// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#ifndef MYOS_SH_INPUT_H
#define MYOS_SH_INPUT_H

#include <stddef.h>

#define SH_LINE_CAPACITY 256U

enum sh_input_result {
    SH_INPUT_RESULT_LINE,
    SH_INPUT_RESULT_EOF,
    SH_INPUT_RESULT_INVALID_LINE,
    SH_INPUT_RESULT_ERROR,
};

/**
 * Reads one line from standard input.
 *
 * LINE returns a NUL-terminated string without its
 * trailing newline. EOF after a partial line returns
 * that line; EOF without data returns EOF.
 *
 * Oversized lines and embedded NUL bytes are rejected.
 * The remaining input through newline is discarded.
 *
 * Only LINE guarantees usable output in buffer.
 */
enum sh_input_result sh_read_line(
    char *buffer,
    size_t capacity
);

#endif
