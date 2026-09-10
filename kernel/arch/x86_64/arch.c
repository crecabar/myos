// SPDX-License-Identifier: GPL-2.0-only

#include "arch.h"
#include "gdt.h"
#include "idt.h"

#include "../../diagnostics/diagnostics.h"

void arch_init(void)
{
    diagnostics_write("[arch] Loading GDT\n");
    gdt_init();
    diagnostics_write("[arch] GDT loaded\n");

    diagnostics_write("[arch] Loading IDT\n");
    idt_init();
    diagnostics_write("[arch] IDT loaded\n");
}
