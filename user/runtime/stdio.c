// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file stdio.c
 * @brief Minimal bootstrap formatted-output implementation.
 */

#include <stdio.h>

#include "format.h"

#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

struct runtime_snprintf_context {
    char *buffer;
    size_t capacity;
    size_t stored;
};

static bool runtime_snprintf_write(
    const char *data,
    size_t size,
    void *context
);

static bool runtime_snprintf_write(
    const char *data,
    size_t size,
    void *context)
{
    if (context == NULL) {
        return false;
    }

    struct runtime_snprintf_context *output =
        context;

    if (
        output->capacity == 0 ||
        size == 0
    ) {
        return true;
    }

    if (output->buffer == NULL) {
        return false;
    }

    size_t maximum_stored =
        output->capacity - 1;

    if (
        output->stored >=
        maximum_stored
    ) {
        return true;
    }

    size_t available =
        maximum_stored -
        output->stored;

    size_t copied =
        size < available
            ? size
            : available;

    for (
        size_t index = 0;
        index < copied;
        ++index
    ) {
        output->buffer[
            output->stored + index
        ] =
            data[index];
    }

    output->stored +=
        copied;

    return true;
}

int vsnprintf(
    char *restrict buffer,
    size_t size,
    const char *restrict format,
    va_list arguments)
{
    if (format == NULL) {
        return -1;
    }

    if (
        size != 0 &&
        buffer == NULL
    ) {
        return -1;
    }

    struct runtime_snprintf_context context = {
        .buffer =
            buffer,
        .capacity =
            size,
        .stored =
            0,
    };

    size_t length;

    bool formatted =
        __myos_format_vprintf(
            runtime_snprintf_write,
            &context,
            format,
            arguments,
            &length
        );

    if (size != 0) {
        buffer[context.stored] =
            '\0';
    }

    if (
        !formatted ||
        length >
            (size_t) INT_MAX
    ) {
        return -1;
    }

    return
        (int) length;
}

int snprintf(
    char *restrict buffer,
    size_t size,
    const char *restrict format,
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
