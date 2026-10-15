// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include <sh/input.h>

#include <myos/file.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SH_STDIN_DESCRIPTOR 0U

enum sh_input_result sh_read_line(
    char *buffer,
    size_t capacity)
{
    if (buffer == NULL || capacity == 0U) {
        return SH_INPUT_RESULT_ERROR;
    }

    size_t length = 0U;
    bool rejected = false;
    bool pending_cr = false;

    for (;;) {
        unsigned char byte = 0U;

        syscall_result_t result =
            myos_fd_read(
                SH_STDIN_DESCRIPTOR,
                &byte,
                1U
            );

        if (result < 0 || result > 1) {
            return SH_INPUT_RESULT_ERROR;
        }

        if (result == 0) {
            if (rejected) {
                return SH_INPUT_RESULT_INVALID_LINE;
            }

            if (length == 0U && !pending_cr) {
                return SH_INPUT_RESULT_EOF;
            }

            buffer[length] = '\0';

            return SH_INPUT_RESULT_LINE;
        }

        if (byte == '\n') {
            if (rejected) {
                return SH_INPUT_RESULT_INVALID_LINE;
            }

            buffer[length] = '\0';

            return SH_INPUT_RESULT_LINE;
        }

        /*
         * A pending CR belongs to the line only
         * when it is not followed by LF or EOF.
         */
        if (pending_cr) {
            if (!rejected) {
                if (length >= capacity - 1U) {
                    rejected = true;
                } else {
                    buffer[length++] = '\r';
                }
            }

            pending_cr = false;
        }

        if (byte == '\r') {
            pending_cr = true;
            continue;
        }

        if (byte == '\0') {
            rejected = true;
            continue;
        }

        if (rejected) {
            continue;
        }

        if (length >= capacity - 1U) {
            rejected = true;
            continue;
        }

        buffer[length++] = (char) byte;
    }
}
