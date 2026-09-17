// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file kernel_heap_test.h
 * @brief Minimal kernel heap regression tests.
 */

#ifndef MYOS_TESTS_KERNEL_HEAP_TEST_H
#define MYOS_TESTS_KERNEL_HEAP_TEST_H

/**
 * Verifies the initial page-backed kernel heap allocator.
 *
 * Exercises zero-size rejection, 16-byte alignment, non-overlapping
 * allocations, preservation of allocation contents, and lazy growth across
 * multiple physical backing pages.
 */
void kernel_heap_test_run(void);

#endif
