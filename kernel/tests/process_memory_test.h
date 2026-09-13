// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file process_memory_test.h
 * @brief Process address-space memory regression tests.
 */

#ifndef MYOS_TESTS_PROCESS_MEMORY_TEST_H
#define MYOS_TESTS_PROCESS_MEMORY_TEST_H

/**
 * Verifies process virtual-memory isolation and valid memory access.
 *
 * Exercises userspace virtual-address bounds, rejects invalid higher-half and
 * cross-boundary mutations without modifying paging state, and verifies normal
 * read/write access through a valid process address space.
 */
void process_memory_test_run(void);

#endif
