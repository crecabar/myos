// SPDX-License-Identifier: GPL-2.0-only

#include "arch/x86_64/serial.h"

_Noreturn void kernel_main(void)
{
    serial_init();
    serial_write_string("Hello from kernel\n");

    for (;;) {
        __asm__ volatile ("hlt");
    }
}
