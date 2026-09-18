// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file runtime_diagnostics.h
 * @brief Early kernel runtime diagnostics and regression tests.
 */

#ifndef MYOS_TESTS_RUNTIME_DIAGNOSTICS_H
#define MYOS_TESTS_RUNTIME_DIAGNOSTICS_H

struct framebuffer;

/**
 * Runs the low-level runtime diagnostics and regression tests.
 *
 * Exercises kernel memory, paging, process address spaces, stacks, layouts,
 * and x86-64 execution-state invariants established during early MyOS
 * development.
 *
 * @param framebuffer Active boot framebuffer mapping.
 */
void runtime_diagnostics_run(
    const struct framebuffer *framebuffer
);

#endif
