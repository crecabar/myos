// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file main.c
 * @brief Minimal Unix-style uname userspace command.
 */

#include <myos/file.h>
#include <myos/process.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UNAME_STDOUT_DESCRIPTOR 1U

static size_t uname_string_length(
    const char *string)
{
    size_t length = 0;

    while (string[length] != '\0') {
        ++length;
    }

    return length;
}

static int uname_write_all(
    const char *buffer,
    size_t length)
{
    size_t offset = 0;

    while (offset < length) {
        syscall_result_t written =
            myos_fd_write(
                UNAME_STDOUT_DESCRIPTOR,
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

static int uname_write_field(
    const char *field,
    bool *first)
{
    if (field[0] == '\0') {
        return 0;
    }

    if (
        !*first &&
        uname_write_all(" ", 1U) != 0
    ) {
        return -1;
    }

    if (
        uname_write_all(
            field,
            uname_string_length(field)
        ) != 0
    ) {
        return -1;
    }

    *first = false;

    return 0;
}

int main(
    int argc,
    char *argv[],
    char *envp[])
{
    (void) envp;

    if (
        argc < 1 ||
        argc > 2 ||
        argv == NULL
    ) {
        return 1;
    }

    char option = 's';

    if (argc == 2) {
        if (
            argv[1] == NULL ||
            argv[1][0] != '-' ||
            argv[1][1] == '\0' ||
            argv[1][2] != '\0'
        ) {
            return 1;
        }

        option = argv[1][1];

        if (
            option != 's' &&
            option != 'r' &&
            option != 'm' &&
            option != 'a'
        ) {
            return 1;
        }
    }

    struct syscall_utsname identity = {0};

    if (myos_uname(&identity) != 0) {
        return 1;
    }

    bool first = true;

    if (option == 'a') {
        const char *fields[] = {
            identity.sysname,
            identity.nodename,
            identity.release,
            identity.version,
            identity.machine,
        };

        for (
            size_t index = 0;
            index < sizeof(fields) / sizeof(fields[0]);
            ++index
        ) {
            if (
                uname_write_field(
                    fields[index],
                    &first
                ) != 0
            ) {
                return 1;
            }
        }
    } else {
        const char *field = identity.sysname;

        if (option == 'r') {
            field = identity.release;
        } else if (option == 'm') {
            field = identity.machine;
        }

        if (
            uname_write_field(
                field,
                &first
            ) != 0
        ) {
            return 1;
        }
    }

    if (
        uname_write_all("\n", 1U) != 0
    ) {
        return 1;
    }

    return 0;
}
