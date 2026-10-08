// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file runtime_stdio.c
 * @brief Ring-3 regression for bootstrap snprintf and vsnprintf.
 */

#include <stdio.h>

#include <string.h>

#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

static int runtime_test_vsnprintf(
    char *buffer,
    size_t size,
    const char *format,
    ...)
{
    va_list arguments;

    va_start(
        arguments,
        format
    );

    int result =
        vsnprintf(
            buffer,
            size,
            format,
            arguments
        );

    va_end(arguments);

    return result;
}

int main(
    int argc,
    char *argv[],
    char *envp[])
{
    (void) argc;
    (void) argv;
    (void) envp;

    char buffer[128];

    int result =
        snprintf(
            buffer,
            sizeof(buffer),
            "MyOS"
        );

    if (
        result != 4 ||
        strcmp(buffer, "MyOS") != 0
    ) {
        return 1;
    }

    result =
        snprintf(
            buffer,
            sizeof(buffer),
            "%c:%s:%%",
            'A',
            "MyOS"
        );

    if (
        result != 8 ||
        strcmp(
            buffer,
            "A:MyOS:%"
        ) != 0
    ) {
        return 2;
    }

    result =
        snprintf(
            buffer,
            sizeof(buffer),
            "%d %i",
            -123,
            456
        );

    if (
        result != 8 ||
        strcmp(
            buffer,
            "-123 456"
        ) != 0
    ) {
        return 3;
    }

    result =
        snprintf(
            buffer,
            sizeof(buffer),
            "%d",
            INT_MIN
        );

    if (
        result != 11 ||
        strcmp(
            buffer,
            "-2147483648"
        ) != 0
    ) {
        return 4;
    }

    result =
        snprintf(
            buffer,
            sizeof(buffer),
            "%ld",
            LONG_MIN
        );

    if (
        result != 20 ||
        strcmp(
            buffer,
            "-9223372036854775808"
        ) != 0
    ) {
        return 5;
    }

    result =
        snprintf(
            buffer,
            sizeof(buffer),
            "%lld",
            LLONG_MIN
        );

    if (
        result != 20 ||
        strcmp(
            buffer,
            "-9223372036854775808"
        ) != 0
    ) {
        return 6;
    }

    result =
        snprintf(
            buffer,
            sizeof(buffer),
            "%u",
            UINT_MAX
        );

    if (
        result != 10 ||
        strcmp(
            buffer,
            "4294967295"
        ) != 0
    ) {
        return 7;
    }

    result =
        snprintf(
            buffer,
            sizeof(buffer),
            "%llu",
            ULLONG_MAX
        );

    if (
        result != 20 ||
        strcmp(
            buffer,
            "18446744073709551615"
        ) != 0
    ) {
        return 8;
    }

    result =
        snprintf(
            buffer,
            sizeof(buffer),
            "%x %X",
            0xdeadbeefU,
            0xdeadbeefU
        );

    if (
        result != 17 ||
        strcmp(
            buffer,
            "deadbeef DEADBEEF"
        ) != 0
    ) {
        return 9;
    }

    result =
        snprintf(
            buffer,
            sizeof(buffer),
            "%zu",
            (size_t) 123456789U
        );

    if (
        result != 9 ||
        strcmp(
            buffer,
            "123456789"
        ) != 0
    ) {
        return 10;
    }

    result =
        snprintf(
            buffer,
            sizeof(buffer),
            "%llx",
            ULLONG_MAX
        );

    if (
        result != 16 ||
        strcmp(
            buffer,
            "ffffffffffffffff"
        ) != 0
    ) {
        return 11;
    }

    char truncated[5];

    result =
        snprintf(
            truncated,
            sizeof(truncated),
            "abcdef"
        );

    if (
        result != 6 ||
        strcmp(
            truncated,
            "abcd"
        ) != 0
    ) {
        return 12;
    }

    char one_byte[1] = {
        'x',
    };

    result =
        snprintf(
            one_byte,
            sizeof(one_byte),
            "abcdef"
        );

    if (
        result != 6 ||
        one_byte[0] != '\0'
    ) {
        return 13;
    }

    char untouched =
        'Q';

    result =
        snprintf(
            &untouched,
            0,
            "abcdef"
        );

    if (
        result != 6 ||
        untouched != 'Q'
    ) {
        return 14;
    }

    result =
        snprintf(
            NULL,
            0,
            "abcdef"
        );

    if (result != 6) {
        return 15;
    }

    char exact[7];

    result =
        snprintf(
            exact,
            sizeof(exact),
            "abcdef"
        );

    if (
        result != 6 ||
        strcmp(
            exact,
            "abcdef"
        ) != 0
    ) {
        return 16;
    }

    result =
        snprintf(
            buffer,
            sizeof(buffer),
            "%p",
            (void *) 0
        );

    if (result != -1) {
        return 17;
    }

    result =
        snprintf(
            buffer,
            sizeof(buffer),
            "%"
        );

    if (result != -1) {
        return 18;
    }

    result =
        snprintf(
            buffer,
            sizeof(buffer),
            "%2d",
            7
        );

    if (result != -1) {
        return 19;
    }

    result =
        snprintf(
            NULL,
            1,
            "x"
        );

    if (result != -1) {
        return 20;
    }

    result =
        runtime_test_vsnprintf(
            buffer,
            sizeof(buffer),
            "%s:%d:%x",
            "vsnprintf",
            -7,
            0x2aU
        );

    if (
        result != 15 ||
        strcmp(
            buffer,
            "vsnprintf:-7:2a"
        ) != 0
    ) {
        return 21;
    }

    return 0;
}
