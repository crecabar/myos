// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file stdio.h
 * @brief Minimal bootstrap formatted-output interfaces.
 */

#ifndef MYOS_USER_RUNTIME_STDIO_H
#define MYOS_USER_RUNTIME_STDIO_H

#include <stdarg.h>
#include <stddef.h>

/**
 * Formats output into a bounded character buffer.
 *
 * The bootstrap formatter supports:
 *
 *     %%  %c  %s  %d  %i  %u  %x  %X
 *
 * Integer conversions support the l and ll length modifiers. Unsigned
 * conversions additionally support z for size_t.
 *
 * Width, precision, flags and floating-point conversions are not supported.
 * Unsupported or malformed conversion specifications cause this function to
 * return -1.
 *
 * When size is non-zero, buffer must be non-NULL and the produced string is
 * always null-terminated. When size is zero, buffer may be NULL and no bytes
 * are written.
 *
 * The return value is the number of characters that would have been written,
 * excluding the terminating null byte. A result that cannot be represented as
 * int causes the function to return -1.
 *
 * @param buffer Destination buffer, or NULL when size is zero.
 * @param size Total destination capacity including the terminating null byte.
 * @param format Format string.
 * @param arguments Variable argument list.
 *
 * @return Required formatted length, or -1 on malformed/unsupported input or
 *         length overflow.
 */
int vsnprintf(
    char *restrict buffer,
    size_t size,
    const char *restrict format,
    va_list arguments
);

/**
 * Formats output into a bounded character buffer.
 *
 * Semantics and supported conversions are identical to vsnprintf().
 *
 * @param buffer Destination buffer, or NULL when size is zero.
 * @param size Total destination capacity including the terminating null byte.
 * @param format Format string.
 *
 * @return Required formatted length, or -1 on malformed/unsupported input or
 *         length overflow.
 */
int snprintf(
    char *restrict buffer,
    size_t size,
    const char *restrict format,
    ...
);

#endif
