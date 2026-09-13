// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file runtime_diagnostics.h
 * @brief Early kernel runtime diagnostics and regression tests.
 */

#ifndef MYOS_TESTS_RUNTIME_DIAGNOSTICS_H
#define MYOS_TESTS_RUNTIME_DIAGNOSTICS_H

/**
 * Runs the low-level runtime diagnostics and regression tests.
 *
 * Exercises kernel memory, paging, process address spaces, stacks, layouts,
 * and x86-64 execution-state invariants established during early MyOS
 * development.
 */
void runtime_diagnostics_run(void);

#endif
