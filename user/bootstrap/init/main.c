// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file main.c
 * @brief Bootstrap userspace init and shell supervisor.
 */

#include "supervisor.h"

#include <myos/abi/syscall.h>
#include <myos/file.h>
#include <myos/process.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define INIT_STDOUT_DESCRIPTOR 1U

static int init_write_all(
    const char *buffer,
    size_t length)
{
    size_t offset = 0U;

    while (offset < length) {
        syscall_result_t written =
            myos_fd_write(
                INIT_STDOUT_DESCRIPTOR,
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

static int init_configure_stdin(void)
{
    static const char console_path[] =
        "/dev/console";

    /*
     * The initial process starts with stdin
     * connected to /dev/null.
     *
     * Replace it once, before launching the
     * first shell. Subsequent children inherit
     * the established descriptor.
     */
    if (myos_fd_close(0U) != 0) {
        return -1;
    }

    syscall_result_t descriptor =
        myos_fd_open(
            console_path,
            sizeof(console_path) - 1U,
            SYSCALL_OPEN_ACCESS_READ
        );

    if (descriptor != 0) {
        if (descriptor > 0) {
            (void) myos_fd_close(
                (uint64_t) descriptor
            );
        }

        return -1;
    }

    return 0;
}

static int init_report_termination(
    const struct syscall_wait_status *status)
{
    if (status == NULL) {
        return -1;
    }

    char message[128];
    int length;

    if (
        status->termination_reason ==
        SYSCALL_WAIT_TERMINATION_EXITED
    ) {
        length = snprintf(
            message,
            sizeof(message),
            "\ninit: shell exited with status %llu\n",
            (unsigned long long) status->exit_status
        );
    } else {
        length = snprintf(
            message,
            sizeof(message),
            "\ninit: shell terminated (reason=%llu)\n",
            (unsigned long long) status->termination_reason
        );
    }

    if (
        length < 0 ||
        (size_t) length >= sizeof(message)
    ) {
        return -1;
    }

    return init_write_all(
        message,
        (size_t) length
    );
}

/**
 * Supervises one foreground shell at a time.
 *
 * A successfully created child is always awaited
 * and reaped before another shell is launched.
 *
 * The initial bootstrap supervisor has no daemon
 * registry or terminal-session management.
 */
int main(
    int argc,
    char *argv[],
    char *envp[])
{
    (void) argc;
    (void) argv;
    (void) envp;

    if (init_configure_stdin() != 0) {
        return 1;
    }

    static const char shell_path[] =
        "/bin/sh";

    const char *const shell_argv[] = {
        shell_path,
    };

    static const char starting_message[] =
        "init: starting shell\n";

    static const char fork_error[] =
        "init: unable to fork shell\n";

    static const char wait_error[] =
        "init: unable to wait for shell\n";

    static const char failure_limit_message[] =
        "init: shell failure limit reached\n";

    struct init_supervisor_state supervisor = {0};

    for (;;) {
        if (
            init_write_all(
                starting_message,
                sizeof(starting_message) - 1U
            ) != 0
        ) {
            return 1;
        }

        syscall_result_t child_pid =
            myos_fork();

        if (child_pid < 0) {
            (void) init_write_all(
                fork_error,
                sizeof(fork_error) - 1U
            );

            return 1;
        }

        if (child_pid == 0) {
            /*
             * Child process.
             *
             * The parent retains its original
             * descriptors and working directory.
             *
             * Successful execve() does not return.
             */
            (void) myos_execve(
                shell_path,
                sizeof(shell_path) - 1U,
                shell_argv,
                1U,
                NULL,
                0U
            );

            /*
             * Bootstrap convention: execve()
             * failure exits with status
             * INIT_SHELL_EXEC_FAILURE_STATUS.
             */
            return (int) INIT_SHELL_EXEC_FAILURE_STATUS;
        }

        /*
         * Parent process.
         *
         * Wait without polling until this specific
         * child has terminated. waitpid() reaps it.
         */
        struct syscall_wait_status status = {0};

        syscall_result_t waited_pid =
            myos_waitpid(
                (uint64_t) child_pid,
                &status,
                0U
            );

        if (waited_pid != child_pid) {
            (void) init_write_all(
                wait_error,
                sizeof(wait_error) - 1U
            );

            return 1;
        }

        if (init_report_termination(&status) != 0) {
            return 1;
        }

        /*
         * Normal shell exits complete a session and
         * reset the consecutive-failure counter.
         *
         * Failed execve() and abnormal terminations
         * consume the provisional restart budget.
         */
        enum init_supervisor_decision decision =
            init_supervisor_observe_termination(
                &supervisor,
                &status
            );

        if (decision == INIT_SUPERVISOR_STOP) {
            (void) init_write_all(
                failure_limit_message,
                sizeof(failure_limit_message) - 1U
            );

            return 1;
        }
    }
}
