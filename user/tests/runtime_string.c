// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file runtime_string.c
 * @brief Ring-3 regression for bootstrap memory and string primitives.
 */

#include <string.h>

#include <stddef.h>

int main(
    int argc,
    char *argv[],
    char *envp[])
{
    (void) argc;
    (void) argv;
    (void) envp;

    char source[] = {
        'M', 'y', 'O', 'S', '\0'
    };

    char destination[8];

    if (
        memcpy(
            destination,
            source,
            sizeof(source)
        ) != destination
    ) {
        return 1;
    }

    if (strcmp(destination, "MyOS") != 0) {
        return 2;
    }

    if (strlen(destination) != 4) {
        return 3;
    }

    if (
        memset(
            destination,
            'x',
            3
        ) != destination
    ) {
        return 4;
    }

    if (
        destination[0] != 'x' ||
        destination[1] != 'x' ||
        destination[2] != 'x'
    ) {
        return 5;
    }

    char overlap_right[] =
        "abcdef";

    if (
        memmove(
            overlap_right + 2,
            overlap_right,
            4
        ) != overlap_right + 2
    ) {
        return 6;
    }

    if (
        memcmp(
            overlap_right,
            "ababcd",
            6
        ) != 0
    ) {
        return 7;
    }

    char overlap_left[] =
        "abcdef";

    if (
        memmove(
            overlap_left,
            overlap_left + 2,
            4
        ) != overlap_left
    ) {
        return 8;
    }

    if (
        memcmp(
            overlap_left,
            "cdefef",
            6
        ) != 0
    ) {
        return 9;
    }

    if (
        memcmp("abc", "abc", 3) != 0 ||
        memcmp("abc", "abd", 3) >= 0 ||
        memcmp("abd", "abc", 3) <= 0
    ) {
        return 10;
    }

    if (
        strcmp("abc", "abc") != 0 ||
        strcmp("abc", "abd") >= 0 ||
        strcmp("abd", "abc") <= 0
    ) {
        return 11;
    }

    if (
        strncmp("abcdef", "abcxyz", 3) != 0 ||
        strncmp("abcdef", "abcxyz", 4) >= 0 ||
        strncmp("abcxyz", "abcdef", 4) <= 0
    ) {
        return 12;
    }

    if (strncmp("abc", "abd", 0) != 0) {
        return 13;
    }

    return 0;
}
