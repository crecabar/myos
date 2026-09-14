// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file qemu_test_exit.c
 * @brief QEMU isa-debug-exit signaling for automated kernel tests.
 */

#include "qemu_test_exit.h"

#include <stdint.h>

#if MYOS_QEMU_TEST_EXIT

static void qemu_test_exit_write(uint32_t value);
static _Noreturn void qemu_test_exit(uint32_t value);

static void qemu_test_exit_write(uint32_t value)
{
    __asm__ volatile (
        "outl %0, %1"
        :
        : "a"(value),
          "Nd"((uint16_t) MYOS_QEMU_TEST_EXIT_PORT)
    );
}

static _Noreturn void qemu_test_exit(uint32_t value)
{
    qemu_test_exit_write(value);

    /*
     * The isa-debug-exit device terminates QEMU during the OUT instruction.
     * If the kernel was accidentally booted without that device, remain in a
     * terminal state instead of continuing execution after a declared result.
     */
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

_Noreturn void qemu_test_exit_success(void)
{
    qemu_test_exit(
        (uint32_t) MYOS_QEMU_TEST_EXIT_SUCCESS_VALUE
    );
}

_Noreturn void qemu_test_exit_failure(void)
{
    qemu_test_exit(
        (uint32_t) MYOS_QEMU_TEST_EXIT_FAILURE_VALUE
    );
}

#endif
