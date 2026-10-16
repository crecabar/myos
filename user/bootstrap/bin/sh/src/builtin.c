// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include <sh/builtin.h>

#include <myos/process.h>

#include <stddef.h>
#include <string.h>

static int sh_builtin_cd(
    int argc,
    char *argv[])
{
    if (
        argc != 2 ||
        argv[1] == NULL ||
        argv[1][0] == '\0'
    ) {
        return 1;
    }

    return myos_chdir(
        argv[1],
        strlen(argv[1])
    ) == 0 ? 0 : 1;
}

static int sh_builtin_exit_status(
    int argc,
    char *argv[],
    int *status)
{
    if (argc == 1) {
        *status = 0;
        return 0;
    }

    if (argc != 2 || argv[1] == NULL) {
        return -1;
    }

    const char *cursor = argv[1];

    if (*cursor == '\0') {
        return -1;
    }

    unsigned int value = 0U;

    while (*cursor != '\0') {
        if (*cursor < '0' || *cursor > '9') {
            return -1;
        }

        unsigned int digit =
            (unsigned int) (*cursor - '0');

        if (value > (255U - digit) / 10U) {
            return -1;
        }

        value = value * 10U + digit;
        ++cursor;
    }

    *status = (int) value;

    return 0;
}

struct sh_builtin_result sh_builtin_execute(
    int argc,
    char *argv[])
{
    struct sh_builtin_result result = {
        .handled = false,
        .exit_requested = false,
        .status = 0,
    };

    if (
        argc <= 0 ||
        argv == NULL ||
        argv[0] == NULL
    ) {
        return result;
    }

    if (strcmp(argv[0], "cd") == 0) {
        result.handled = true;
        result.status = sh_builtin_cd(argc, argv);
        return result;
    }

    if (strcmp(argv[0], "exit") == 0) {
        result.handled = true;

        int status = 0;

        if (
            sh_builtin_exit_status(
                argc,
                argv,
                &status
            ) != 0
        ) {
            result.status = 1;
            return result;
        }

        result.exit_requested = true;
        result.status = status;

        return result;
    }

    return result;
}
