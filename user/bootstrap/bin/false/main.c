// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file main.c
 * @brief Unix-style false userspace command.
 */

/**
 * Returns failure without producing output.
 *
 * @param argc Number of process arguments.
 * @param argv Process argument vector.
 * @param envp Process environment vector.
 *
 * @return One, indicating failure.
 */
int main(
    int argc,
    char *argv[],
    char *envp[])
{
    (void) argc;
    (void) argv;
    (void) envp;

    return 1;
}
