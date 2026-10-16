// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file main.c
 * @brief Interactive bootstrap userspace shell for MyOS.
 */

#include <myos/file.h>

#include <sh/builtin.h>
#include <sh/execute.h>
#include <sh/input.h>
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

    static const char prompt[] =
        "myos $ ";

    static const char invalid_line[] =
        "sh: invalid input line\n";

    static const char invalid_command[] =
        "sh: too many arguments\n";

    static const char execution_error[] =
        "sh: command execution failed\n";

    static const char input_error[] =
        "sh: input read failed\n";

    if (
        sh_write_all(
            message,
            sizeof(message) - 1U
        ) != 0
    ) {
        return 1;
    }

    int last_status = 0;

    for (;;) {
        if (
            sh_write_all(
                prompt,
                sizeof(prompt) - 1U
            ) != 0
        ) {
            return 1;
        }

        char line[SH_LINE_CAPACITY];

        enum sh_input_result input_result =
            sh_read_line(
                line,
                sizeof(line)
            );

        if (input_result == SH_INPUT_RESULT_EOF) {
            return last_status;
        }

        if (input_result == SH_INPUT_RESULT_ERROR) {
            (void) sh_write_all(
                input_error,
                sizeof(input_error) - 1U
            );

            return 1;
        }

        if (
            input_result ==
            SH_INPUT_RESULT_INVALID_LINE
        ) {
            if (
                sh_write_all(
                    invalid_line,
                    sizeof(invalid_line) - 1U
                ) != 0
            ) {
                return 1;
            }

            last_status = 1;
            continue;
        }

        if (input_result != SH_INPUT_RESULT_LINE) {
            return 1;
        }

        char *arguments[SH_MAX_ARGS + 1U];

        int argument_count =
            sh_parse_line(
                line,
                arguments,
                SH_MAX_ARGS + 1U
            );

        if (argument_count < 0) {
            if (
                sh_write_all(
                    invalid_command,
                    sizeof(invalid_command) - 1U
                ) != 0
            ) {
                return 1;
            }

            last_status = 2;
            continue;
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
            last_status = builtin.status;

            if (builtin.exit_requested) {
                return last_status;
            }

            continue;
        }

        int status =
            sh_execute(
                argument_count,
                arguments
            );

        if (status < 0) {
            last_status = 1;

            if (
                sh_write_all(
                    execution_error,
                    sizeof(execution_error) - 1U
                ) != 0
            ) {
                return 1;
            }

            continue;
        }

        last_status = status;
    }
}
