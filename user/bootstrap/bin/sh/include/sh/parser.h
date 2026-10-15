// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#ifndef MYOS_SH_PARSER_H
#define MYOS_SH_PARSER_H

#include <stddef.h>

#define SH_MAX_ARGS 16U

/**
 * Tokenizes a mutable, NUL-terminated command line.
 *
 * Spaces and tabs delimit arguments. The input buffer is
 * modified in place, and argv points into that buffer.
 *
 * capacity includes the terminating NULL entry.
 *
 * Returns the number of arguments, including zero for an
 * empty line, or -1 for invalid arguments or overflow.
 *
 * The caller must not use argv when parsing fails.
 */
int sh_parse_line(
    char *line,
    char *argv[],
    size_t capacity
);

#endif
