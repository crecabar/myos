// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file process_memory_test.h
 * @brief Process address-space memory regression tests.
 */

#ifndef MYOS_TESTS_PROCESS_MEMORY_TEST_H
#define MYOS_TESTS_PROCESS_MEMORY_TEST_H

/**
 * Verifies read/write access through a process address space.
 *
 * Creates a temporary process address space and layout, writes known data into
 * user memory, reads it back, verifies the contents, and releases all
 * associated resources.
 */
void process_memory_test_run(void);

#endif
