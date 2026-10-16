// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file main.c
 * @brief Minimal Unix-style echo userspace command.
 */

#include <myos/file.h>

#include <stddef.h>
#include <stdint.h>

#define ECHO_STDOUT_DESCRIPTOR 1U

/**
 * Writes an entire byte sequence to standard output.
 *
 * Short writes are retried. A zero-length transfer or a negative
 * syscall result is treated as a write failure.
 */
static int echo_write_all(
    const char *buffer,
    size_t length)
{
    size_t offset = 0;

    while (offset < length) {
        syscall_result_t written =
            myos_fd_write(
                ECHO_STDOUT_DESCRIPTOR,
                buffer + offset,
                length - offset
            );

        if (written <= 0) {
            return -1;
        }

        if ((uint64_t) written > length - offset) {
            return -1;
        }

        offset += (size_t) written;
    }

    return 0;
}

/**
 * Returns the length of a NUL-terminated argument.
 */
static size_t echo_string_length(
    const char *string)
{
    size_t length = 0;

    while (string[length] != '\0') {
        ++length;
    }

    return length;
}

/**
 * Writes arguments separated by spaces and followed by newline.
 */
int main(
    int argc,
    char *argv[],
    char *envp[])
{
    (void) envp;

    if (argc < 0 || argv == NULL) {
        return 1;
    }

    for (int index = 1; index < argc; ++index) {
        if (argv[index] == NULL) {
            return 1;
        }

        if (index > 1) {
            if (echo_write_all(" ", 1U) != 0) {
                return 1;
            }
        }

        if (
            echo_write_all(
                argv[index],
                echo_string_length(argv[index])
            ) != 0
        ) {
            return 1;
        }
    }

    if (echo_write_all("\n", 1U) != 0) {
        return 1;
    }

    return 0;
}
