// SPDX-License-Identifier: GPL-2.0-only

_Noreturn void kernel_main(void)
{
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
