// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#ifndef MYOS_SH_EXECUTE_H
#define MYOS_SH_EXECUTE_H

/**
 * Executes a command using the native MyOS process ABI.
 *
 * The initial bootstrap resolves unqualified commands
 * exclusively under /bin.
 *
 * Returns the child's exit status on normal termination,
 * or -1 when execution, waiting, or termination fails.
 */
int sh_execute(
    int argc,
    char *argv[]
);

#endif
