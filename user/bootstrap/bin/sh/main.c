// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file main.c
 * @brief Bootstrap userspace shell for MyOS.
 */

#include <myos/file.h>

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

    return 0;
}
