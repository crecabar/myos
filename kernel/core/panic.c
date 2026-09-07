// SPDX-License-Identifier: GPL-2.0-only

#include "panic.h"
#include "../diagnostics/diagnostics.h"

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

    kernel_halt();
}
