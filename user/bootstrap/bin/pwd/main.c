// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file main.c
 * @brief Minimal Unix-style pwd userspace command.
 */

#include <myos/file.h>
#include <myos/process.h>

#include <stddef.h>
#include <stdint.h>

#define PWD_STDOUT_DESCRIPTOR 1U
#define PWD_PATH_CAPACITY 4096U

static int pwd_write_all(
    const char *buffer,
    size_t length)
{
    size_t offset = 0;

    while (offset < length) {
        syscall_result_t written =
            myos_fd_write(
                PWD_STDOUT_DESCRIPTOR,
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
    (void) argv;
    (void) envp;

    if (argc != 1) {
        return 1;
    }

    char path[PWD_PATH_CAPACITY];

    syscall_result_t length =
        myos_getcwd(
            path,
            sizeof(path)
        );

    if (
        length < 0 ||
        (uint64_t) length >= sizeof(path)
    ) {
        return 1;
    }

    if (
        pwd_write_all(
            path,
            (size_t) length
        ) != 0
    ) {
        return 1;
    }

    if (pwd_write_all("\n", 1U) != 0) {
        return 1;
    }

    return 0;
}
