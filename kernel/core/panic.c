// SPDX-License-Identifier: GPL-2.0-only

#include "panic.h"
#include "../diagnostics/diagnostics.h"

#if MYOS_QEMU_TEST_EXIT
#include "../arch/x86_64/qemu_test_exit.h"
#endif

#include <stddef.h>

_Noreturn void kernel_halt(void)
{
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

_Noreturn void kernel_panic(const char *message)
{
    diagnostics_write("\n*** KERNEL PANIC ***\n");

    if (message != NULL) {
        diagnostics_write(message);
        diagnostics_write("\n");
    }

#if MYOS_QEMU_TEST_EXIT
    qemu_test_exit_failure();
#endif

    kernel_halt();
}
