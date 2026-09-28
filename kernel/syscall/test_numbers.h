// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file test_numbers.h
 * @brief Kernel-test-only syscall numbers.
 *
 * These numbers are private to the kernel regression harness and are not part
 * of the published MyOS userspace ABI.
 */

#ifndef MYOS_SYSCALL_TEST_NUMBERS_H
#define MYOS_SYSCALL_TEST_NUMBERS_H

#define SYSCALL_TEST_SLEEP         5
#define SYSCALL_TEST_BLOCK         6
#define SYSCALL_TEST_EXEC          7
#define SYSCALL_TEST_CONTEXT_GATE  8

#endif
