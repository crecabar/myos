// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file main.c
 * @brief Unix-style true userspace command.
 */

/**
 * Returns success without producing output.
 *
 * @param argc Number of process arguments.
 * @param argv Process argument vector.
 * @param envp Process environment vector.
 *
 * @return Zero, indicating success.
 */
int main(
    int argc,
    char *argv[],
    char *envp[])
{
    (void) argc;
    (void) argv;
    (void) envp;

    return 0;
}
