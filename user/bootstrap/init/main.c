// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file main.c
 * @brief Bootstrap userspace init process.
 */

/**
 * Entry point for the first userspace process.
 *
 * The bootstrap init process intentionally performs no policy yet. Its only
 * responsibility at this stage is to remain alive after the kernel transfers
 * system continuation to userspace.
 *
 * Later M9 work will extend init to establish the initial userspace
 * environment and launch the shell.
 *
 * @param argc Number of process arguments.
 * @param argv Process argument vector.
 * @param envp Process environment vector.
 *
 * @return This bootstrap implementation does not return.
 */
int main(
    int argc,
    char *argv[],
    char *envp[])
{
    (void) argc;
    (void) argv;
    (void) envp;

    for (;;) {
    }
}
