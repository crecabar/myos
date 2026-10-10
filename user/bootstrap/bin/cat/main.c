// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file main.c
 * @brief Minimal Unix-style cat userspace command.
 */

#include <myos/file.h>

#include <stddef.h>
#include <stdint.h>

#define CAT_STDIN_DESCRIPTOR  0U
#define CAT_STDOUT_DESCRIPTOR 1U
#define CAT_BUFFER_CAPACITY  1024U

static int cat_write_all(
    const void *buffer,
    size_t length)
{
    const unsigned char *bytes = buffer;
    size_t offset = 0;

    while (offset < length) {
        syscall_result_t written =
            myos_fd_write(
                CAT_STDOUT_DESCRIPTOR,
                bytes + offset,
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

static int cat_copy(
    uint64_t descriptor)
{
    unsigned char buffer[CAT_BUFFER_CAPACITY];

    for (;;) {
        syscall_result_t bytes_read =
            myos_fd_read(
                descriptor,
                buffer,
                sizeof(buffer)
            );

        if (bytes_read < 0) {
            return -1;
        }

        if (bytes_read == 0) {
            return 0;
        }

        if (
            (uint64_t) bytes_read > sizeof(buffer) ||
            cat_write_all(
                buffer,
                (size_t) bytes_read
            ) != 0
        ) {
            return -1;
        }
    }
}

static size_t cat_string_length(
    const char *string)
{
    size_t length = 0;

    while (string[length] != '\0') {
        ++length;
    }

    return length;
}

static int cat_copy_path(
    const char *path)
{
    size_t path_length = cat_string_length(path);

    if (path_length == 0U) {
        return -1;
    }

    syscall_result_t opened = myos_fd_open(
        path,
        path_length,
        SYSCALL_OPEN_ACCESS_READ
    );

    if (opened < 0) {
        return -1;
    }

    uint64_t descriptor = (uint64_t) opened;

    int copy_result = cat_copy(descriptor);

    syscall_result_t close_result =
        myos_fd_close(descriptor);

    if (
        copy_result != 0 ||
        close_result < 0
    ) {
        return -1;
    }

    return 0;
}

int main(
    int argc,
    char *argv[],
    char *envp[])
{
    (void) envp;

    if (argc < 1 || argv == NULL) {
        return 1;
    }

    if (argc == 1) {
        return cat_copy(CAT_STDIN_DESCRIPTOR) == 0
            ? 0
            : 1;
    }

    int failed = 0;

    for (int index = 1; index < argc; ++index) {
        if (argv[index] == NULL) {
            failed = 1;
            continue;
        }

        if (
            argv[index][0] == '-' &&
            argv[index][1] == '\0'
        ) {
            if (cat_copy(CAT_STDIN_DESCRIPTOR) != 0) {
                failed = 1;
            }

            continue;
        }

        if (cat_copy_path(argv[index]) != 0) {
            failed = 1;
        }
    }

    return failed;
}
