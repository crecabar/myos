// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include <myos/kernel/core/panic.h>
#include <myos/kernel/diagnostics/diagnostics.h>

#if MYOS_QEMU_TEST_EXIT
#include <myos/kernel/arch/x86_64/qemu_test_exit.h>
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
    /*
     * A fatal kernel failure must remain visible
     * even after userspace takes over the console.
     */
    diagnostics_console_mirror_set(true);

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
