// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file main.c
 * @brief Bootstrap userspace shell for MyOS.
 */

#include <myos/file.h>

#include <sh/builtin.h>
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
     * Temporary deterministic command sequence.
     * This will eventually be replaced by stdin.
     */
    char command_cd[] = "cd /bin";
    char command_pwd[] = "pwd";
    char command_exit[] = "exit";

    char *lines[] = {
        command_cd,
        command_pwd,
        command_exit,
    };

    for (
        size_t index = 0;
        index < sizeof(lines) / sizeof(lines[0]);
        ++index
    ) {
        char *arguments[SH_MAX_ARGS + 1U];

        int argument_count = sh_parse_line(
            lines[index],
            arguments,
            SH_MAX_ARGS + 1U
        );

        if (argument_count < 0) {
            return 1;
        }

        if (argument_count == 0) {
            continue;
        }

        struct sh_builtin_result builtin =
            sh_builtin_execute(
                argument_count,
                arguments
            );

        if (builtin.handled) {
            if (builtin.exit_requested) {
                return builtin.status;
            }

            if (builtin.status != 0) {
                return builtin.status;
            }

            continue;
        }

        int status = sh_execute(
            argument_count,
            arguments
        );

        if (status != 0) {
            return status < 0 ? 1 : status;
        }
    }

    return 0;
}
