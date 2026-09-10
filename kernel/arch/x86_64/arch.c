// SPDX-License-Identifier: GPL-2.0-only

#include "arch.h"
#include "gdt.h"
#include "idt.h"
#include "interrupt_topology.h"
#include "ioapic.h"
#include "lapic.h"
#include "pic.h"
#include "timer.h"

#include "../../diagnostics/diagnostics.h"
#include "../../core/panic.h"

void arch_init(void)
{
    diagnostics_write("[arch] Loading GDT\n");
    gdt_init();

    diagnostics_write("[arch] GDT loaded\n");

    diagnostics_write("[arch] Loading IDT\n");
    idt_init();

    diagnostics_write("[arch] IDT loaded\n");

    struct interrupt_topology topology;

    if (!interrupt_topology_discover(&topology)) {
        kernel_panic("Unable to discover interrupt topology");
    }

    if (!lapic_init()) {
        kernel_panic("Unable to initialize Local APIC");
    }

    if (!ioapic_init(
        topology.ioapic_physical_address,
        topology.ioapic_gsi_base
    )) {
        kernel_panic("Unable to initialize IOAPIC");
    }

    pic_disable();

    if (!ioapic_route(
        topology.timer_route.gsi,
        TIMER_INTERRUPT_VECTOR,
        lapic_id(),
        topology.timer_route.active_low,
        topology.timer_route.level_triggered
    )) {
        kernel_panic("Unable to route PIT interrupt");
    }

    timer_init(100);

    __asm__ volatile ("sti");
}
