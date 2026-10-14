// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file main.c
 * @brief Bootstrap userspace init process.
 */

#include <myos/abi/syscall.h>
#include <myos/file.h>
#include <myos/process.h>

#include <stdint.h>

static int init_configure_stdin(void)
{
    static const char console_path[] =
        "/dev/console";

    /*
     * The initial process starts with stdin connected
     * to /dev/null.
     *
     * Replace that descriptor through the ordinary
     * userspace file API before creating the shell.
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

    /*
     * The descriptor allocator uses the lowest
     * available slot. Stdin must remain descriptor 0.
     */
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

/**
 * Entry point for the first userspace process.
 *
 * The bootstrap init process intentionally performs no policy yet. Its only
 * responsibility at this stage is to remain alive after the kernel transfers
 * system continuation to userspace.
 *
 * Later M9 work will extend init to establish the initial userspace
 * environment and launch the shell.
 *
 * @param argc Number of process arguments.
 * @param argv Process argument vector.
 * @param envp Process environment vector.
 *
 * @return This bootstrap implementation does not return.
 */
 int main(
    int argc,
    char *argv[],
    char *envp[])
{
    (void) argc;
    (void) argv;
    (void) envp;

    /*
     * Establish the shell's standard input before fork().
     *
     * The child inherits this descriptor and execve()
     * preserves it across image replacement.
     */
    if (init_configure_stdin() != 0) {
        return 1;
    }

    static const char shell_path[] = "/bin/sh";

    const char *const shell_argv[] = {
        shell_path,
    };

    syscall_result_t child_pid =
        myos_fork();

    if (child_pid == 0) {
        /*
         * Child process.
         *
         * Successful execve() replaces this image and
         * never returns to the original /init code.
         */
        syscall_result_t result =
            myos_execve(
                shell_path,
                sizeof(shell_path) - 1U,
                shell_argv,
                1U,
                NULL,
                0U
            );

        (void) result;

        /*
         * Reaching this point means execve() failed.
         */
        return 127;
    }

    if (child_pid > 0) {
        struct syscall_wait_status status = {0};

        syscall_result_t waited_pid =
            myos_waitpid(
                (uint64_t) child_pid,
                &status,
                0U
            );

        if (
            waited_pid != child_pid ||
            status.termination_reason !=
                SYSCALL_WAIT_TERMINATION_EXITED ||
            status.exit_status != 0U
        ) {
            /*
             * Later, init will report and handle
             * child failures through its own policy.
             */
            for (;;) {
            }
        }
    }

    /*
     * Bootstrap PID 1 must remain alive.
     *
     * Shell supervision and restart policy
     * will be developed in later slices.
     */
    for (;;) {
    }
}
