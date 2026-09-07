// SPDX-License-Identifier: GPL-2.0-only

#include "arch.h"
#include "idt.h"

void arch_init(void)
{
    idt_init();
}
