// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#ifndef MYOS_DIAGNOSTICS_DIAGNOSTICS_H
#define MYOS_DIAGNOSTICS_DIAGNOSTICS_H

#include <stdarg.h>

struct console;

void diagnostics_init(void);

void diagnostics_attach_console(
    struct console *console
);

void diagnostics_write(
    const char *string
);

void diagnostics_vprintf(
    const char *format,
    va_list arguments
);

void diagnostics_printf(
    const char *format,
    ...
);

#endif
