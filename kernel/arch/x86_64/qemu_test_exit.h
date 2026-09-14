// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file qemu_test_exit.h
 * @brief QEMU kernel-test exit signaling.
 */

#ifndef MYOS_ARCH_X86_64_QEMU_TEST_EXIT_H
#define MYOS_ARCH_X86_64_QEMU_TEST_EXIT_H

#if MYOS_QEMU_TEST_EXIT

/**
 * Terminates an automated QEMU kernel-test run successfully.
 *
 * This function does not return when the QEMU isa-debug-exit device is
 * present.
 */
_Noreturn void qemu_test_exit_success(void);

/**
 * Terminates an automated QEMU kernel-test run with failure.
 *
 * This function does not return when the QEMU isa-debug-exit device is
 * present.
 */
_Noreturn void qemu_test_exit_failure(void);

#endif

#endif
