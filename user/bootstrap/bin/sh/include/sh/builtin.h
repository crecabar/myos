// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#ifndef MYOS_SH_BUILTIN_H
#define MYOS_SH_BUILTIN_H

#include <stdbool.h>

struct sh_builtin_result {
    bool handled;
    bool exit_requested;
    int status;
};

/**
 * Executes a shell builtin in the calling process.
 *
 * Unrecognized commands return handled=false.
 * exit requests termination but never terminates
 * the calling process directly.
 */
struct sh_builtin_result sh_builtin_execute(
    int argc,
    char *argv[]
);

#endif
