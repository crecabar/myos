// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file format.c
 * @brief Private bootstrap userspace formatting engine.
 */

#include "format.h"

#include <string.h>

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

enum runtime_format_length {
    RUNTIME_FORMAT_LENGTH_DEFAULT,
    RUNTIME_FORMAT_LENGTH_LONG,
    RUNTIME_FORMAT_LENGTH_LONG_LONG,
    RUNTIME_FORMAT_LENGTH_SIZE,
};

static bool runtime_format_emit(
    runtime_format_write_fn write,
    void *context,
    const char *data,
    size_t size,
    size_t *length
);

static bool runtime_format_emit_character(
    runtime_format_write_fn write,
    void *context,
    char character,
    size_t *length
);

static bool runtime_format_emit_unsigned(
    runtime_format_write_fn write,
    void *context,
    uintmax_t value,
    unsigned int base,
    bool uppercase,
    size_t *length
);

static bool runtime_format_emit_signed(
    runtime_format_write_fn write,
    void *context,
    intmax_t value,
    size_t *length
);

static bool runtime_format_emit(
    runtime_format_write_fn write,
    void *context,
    const char *data,
    size_t size,
    size_t *length)
{
    if (
        write == NULL ||
        length == NULL ||
        (
            size != 0 &&
            data == NULL
        )
    ) {
        return false;
    }

    if (
        size >
        SIZE_MAX - *length
    ) {
        return false;
    }

    if (
        size != 0 &&
        !write(
            data,
            size,
            context
        )
    ) {
        return false;
    }

    *length +=
        size;

    return true;
}

static bool runtime_format_emit_character(
    runtime_format_write_fn write,
    void *context,
    char character,
    size_t *length)
{
    return runtime_format_emit(
        write,
        context,
        &character,
        1,
        length
    );
}

static bool runtime_format_emit_unsigned(
    runtime_format_write_fn write,
    void *context,
    uintmax_t value,
    unsigned int base,
    bool uppercase,
    size_t *length)
{
    if (
        base != 10 &&
        base != 16
    ) {
        return false;
    }

    static const char lowercase_digits[] =
        "0123456789abcdef";

    static const char uppercase_digits[] =
        "0123456789ABCDEF";

    const char *digits =
        uppercase
            ? uppercase_digits
            : lowercase_digits;

    char buffer[
        sizeof(uintmax_t) *
        CHAR_BIT
    ];

    size_t position =
        sizeof(buffer);

    do {
        unsigned int digit =
            (unsigned int)
                (value %
                 (uintmax_t) base);

        --position;

        buffer[position] =
            digits[digit];

        value /=
            (uintmax_t) base;
    } while (value != 0);

    return runtime_format_emit(
        write,
        context,
        &buffer[position],
        sizeof(buffer) - position,
        length
    );
}

static bool runtime_format_emit_signed(
    runtime_format_write_fn write,
    void *context,
    intmax_t value,
    size_t *length)
{
    uintmax_t magnitude;

    if (value < 0) {
        if (!runtime_format_emit_character(
            write,
            context,
            '-',
            length
        )) {
            return false;
        }

        magnitude =
            (uintmax_t)
                (-(value + 1));

        ++magnitude;
    } else {
        magnitude =
            (uintmax_t) value;
    }

    return runtime_format_emit_unsigned(
        write,
        context,
        magnitude,
        10,
        false,
        length
    );
}

bool __myos_format_vprintf(
    runtime_format_write_fn write,
    void *context,
    const char *format,
    va_list arguments,
    size_t *length)
{
    if (
        write == NULL ||
        format == NULL ||
        length == NULL
    ) {
        return false;
    }

    *length =
        0;

    while (*format != '\0') {
        if (*format != '%') {
            if (!runtime_format_emit_character(
                write,
                context,
                *format,
                length
            )) {
                return false;
            }

            ++format;

            continue;
        }

        ++format;

        if (*format == '\0') {
            return false;
        }

        enum runtime_format_length conversion_length =
            RUNTIME_FORMAT_LENGTH_DEFAULT;

        if (*format == 'l') {
            ++format;

            if (*format == 'l') {
                conversion_length =
                    RUNTIME_FORMAT_LENGTH_LONG_LONG;

                ++format;
            } else {
                conversion_length =
                    RUNTIME_FORMAT_LENGTH_LONG;
            }
        } else if (*format == 'z') {
            conversion_length =
                RUNTIME_FORMAT_LENGTH_SIZE;

            ++format;
        }

        if (*format == '\0') {
            return false;
        }

        switch (*format) {
        case '%':
            if (
                conversion_length !=
                RUNTIME_FORMAT_LENGTH_DEFAULT
            ) {
                return false;
            }

            if (!runtime_format_emit_character(
                write,
                context,
                '%',
                length
            )) {
                return false;
            }

            break;

        case 'c': {
            if (
                conversion_length !=
                RUNTIME_FORMAT_LENGTH_DEFAULT
            ) {
                return false;
            }

            int value =
                va_arg(
                    arguments,
                    int
                );

            if (!runtime_format_emit_character(
                write,
                context,
                (char) value,
                length
            )) {
                return false;
            }

            break;
        }

        case 's': {
            if (
                conversion_length !=
                RUNTIME_FORMAT_LENGTH_DEFAULT
            ) {
                return false;
            }

            const char *string =
                va_arg(
                    arguments,
                    const char *
                );

            if (string == NULL) {
                string =
                    "(null)";
            }

            if (!runtime_format_emit(
                write,
                context,
                string,
                strlen(string),
                length
            )) {
                return false;
            }

            break;
        }

        case 'd':
        case 'i': {
            intmax_t value;

            switch (conversion_length) {
            case RUNTIME_FORMAT_LENGTH_DEFAULT:
                value =
                    (intmax_t)
                        va_arg(
                            arguments,
                            int
                        );

                break;

            case RUNTIME_FORMAT_LENGTH_LONG:
                value =
                    (intmax_t)
                        va_arg(
                            arguments,
                            long
                        );

                break;

            case RUNTIME_FORMAT_LENGTH_LONG_LONG:
                value =
                    (intmax_t)
                        va_arg(
                            arguments,
                            long long
                        );

                break;

            case RUNTIME_FORMAT_LENGTH_SIZE:
                return false;
            }

            if (!runtime_format_emit_signed(
                write,
                context,
                value,
                length
            )) {
                return false;
            }

            break;
        }

        case 'u':
        case 'x':
        case 'X': {
            uintmax_t value;

            switch (conversion_length) {
            case RUNTIME_FORMAT_LENGTH_DEFAULT:
                value =
                    (uintmax_t)
                        va_arg(
                            arguments,
                            unsigned int
                        );

                break;

            case RUNTIME_FORMAT_LENGTH_LONG:
                value =
                    (uintmax_t)
                        va_arg(
                            arguments,
                            unsigned long
                        );

                break;

            case RUNTIME_FORMAT_LENGTH_LONG_LONG:
                value =
                    (uintmax_t)
                        va_arg(
                            arguments,
                            unsigned long long
                        );

                break;

            case RUNTIME_FORMAT_LENGTH_SIZE:
                value =
                    (uintmax_t)
                        va_arg(
                            arguments,
                            size_t
                        );

                break;
            }

            unsigned int base =
                *format == 'u'
                    ? 10U
                    : 16U;

            bool uppercase =
                *format == 'X';

            if (!runtime_format_emit_unsigned(
                write,
                context,
                value,
                base,
                uppercase,
                length
            )) {
                return false;
            }

            break;
        }

        default:
            return false;
        }

        ++format;
    }

    return true;
}
