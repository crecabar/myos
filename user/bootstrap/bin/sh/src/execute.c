// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include <sh/execute.h>

#include <myos/process.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SH_EXEC_PATH_CAPACITY 256U

static int sh_resolve_path(
    const char *command,
    char *path,
    size_t capacity)
{
    if (
        command == NULL ||
        command[0] == '\0' ||
        path == NULL
    ) {
        return -1;
    }

    const char *prefix = "/bin/";
    size_t prefix_length = strlen(prefix);
    size_t command_length = strlen(command);

    /*
     * Initial bootstrap supports command names,
     * not arbitrary paths or PATH resolution.
     */
    for (size_t index = 0; index < command_length; ++index) {
        if (command[index] == '/') {
            return -1;
        }
    }

    if (
        prefix_length >= capacity ||
        command_length >= capacity - prefix_length
    ) {
        return -1;
    }

    memcpy(path, prefix, prefix_length);
    memcpy(
        path + prefix_length,
        command,
        command_length + 1U
    );

    return 0;
}

int sh_execute(
    int argc,
    char *argv[])
{
    if (
        argc <= 0 ||
        argv == NULL ||
        argv[0] == NULL
    ) {
        return -1;
    }

    char path[SH_EXEC_PATH_CAPACITY];

    if (
        sh_resolve_path(
            argv[0],
            path,
            sizeof(path)
        ) != 0
    ) {
        return -1;
    }

    syscall_result_t child_pid = myos_fork();

    if (child_pid < 0) {
        return -1;
    }

    if (child_pid == 0) {
        /*
         * Successful execve() never returns.
         */
        (void) myos_execve(
            path,
            strlen(path),
            (const char *const *) argv,
            (size_t) argc,
            NULL,
            0U
        );

        return 127;
    }

    struct syscall_wait_status status = {0};

    syscall_result_t waited =
        myos_waitpid(
            (uint64_t) child_pid,
            &status,
            0U
        );

    if (
        waited != child_pid ||
        status.termination_reason !=
            SYSCALL_WAIT_TERMINATION_EXITED ||
        status.exit_status > 255U
    ) {
        return -1;
    }

    return (int) status.exit_status;
}
