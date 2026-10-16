// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file format.h
 * @brief Private bootstrap userspace formatting engine.
 */

#ifndef MYOS_USER_RUNTIME_FORMAT_H
#define MYOS_USER_RUNTIME_FORMAT_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * Receives one formatted byte sequence.
 *
 * Returning false aborts formatting.
 */
typedef bool (*runtime_format_write_fn)(
    const char *data,
    size_t size,
    void *context
);

/**
 * Formats one variable argument list through a caller-provided sink.
 *
 * The returned length counts every byte that would be emitted by the format,
 * independently of whether the sink chooses to retain all of them.
 *
 * @param write Output sink.
 * @param context Opaque sink context.
 * @param format Format string.
 * @param arguments Variable argument list.
 * @param length Receives the formatted byte count.
 *
 * @return true when the complete format was valid and emitted successfully;
 * false otherwise.
 */
bool __myos_format_vprintf(
    runtime_format_write_fn write,
    void *context,
    const char *format,
    va_list arguments,
    size_t *length
);

#endif
