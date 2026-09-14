// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file interrupt_wait_test.h
 * @brief x86-64 interruptible wait regression tests.
 */

#ifndef MYOS_ARCH_X86_64_TESTS_INTERRUPT_WAIT_TEST_H
#define MYOS_ARCH_X86_64_TESTS_INTERRUPT_WAIT_TEST_H

/**
 * Verifies that the CPU interrupt wait primitive wakes and restores its
 * disabled-interrupt return invariant.
 */
void interrupt_wait_test_run(void);

#endif
