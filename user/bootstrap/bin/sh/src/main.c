// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file main.c
 * @brief Bootstrap userspace shell for MyOS.
 */

#include <myos/file.h>

#include <sh/execute.h>
#include <sh/parser.h>

#include <stddef.h>
#include <stdint.h>

#define SH_STDOUT_DESCRIPTOR 1U

static int sh_write_all(
    const char *buffer,
    size_t length)
{
    size_t offset = 0;

    while (offset < length) {
        syscall_result_t written =
            myos_fd_write(
                SH_STDOUT_DESCRIPTOR,
                buffer + offset,
                length - offset
            );

        if (
            written <= 0 ||
            (uint64_t) written > length - offset
        ) {
            return -1;
        }

        offset += (size_t) written;
    }

    return 0;
}

int main(
    int argc,
    char *argv[],
    char *envp[])
{
    (void) argc;
    (void) argv;
    (void) envp;

    static const char message[] =
        "MyOS shell bootstrap\n";

    if (
        sh_write_all(
            message,
            sizeof(message) - 1U
        ) != 0
    ) {
        return 1;
    }

    /*
     * Temporary deterministic input.
     *
     * Interactive console input will replace
     * this fixture in a later slice.
     */
    char line[] = "uname -a";

    char *arguments[SH_MAX_ARGS + 1U];

    int argument_count =
        sh_parse_line(
            line,
            arguments,
            SH_MAX_ARGS + 1U
        );

    if (argument_count <= 0) {
        return 1;
    }

    int result =
        sh_execute(
            argument_count,
            arguments
        );

    return result < 0 ? 1 : result;
}
