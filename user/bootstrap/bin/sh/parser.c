// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include "parser.h"

static int sh_is_separator(char character)
{
    return character == ' ' || character == '\t';
}

int sh_parse_line(
    char *line,
    char *argv[],
    size_t capacity)
{
    if (
        line == NULL ||
        argv == NULL ||
        capacity == 0U
    ) {
        return -1;
    }

    size_t argc = 0U;
    char *cursor = line;

    argv[0] = NULL;

    while (*cursor != '\0') {
        while (sh_is_separator(*cursor)) {
            ++cursor;
        }

        if (*cursor == '\0') {
            break;
        }

        if (argc >= SH_MAX_ARGS ||
            argc + 1U >= capacity) {
            return -1;
        }

        argv[argc++] = cursor;

        while (
            *cursor != '\0' &&
            !sh_is_separator(*cursor)
        ) {
            ++cursor;
        }

        if (*cursor != '\0') {
            *cursor++ = '\0';
        }
    }

    argv[argc] = NULL;

    return (int) argc;
}
