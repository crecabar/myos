// SPDX-License-Identifier: GPL-2.0-only

#include <stdbool.h>
#include <stdint.h>

#include "arch.h"
#include "gdt.h"
#include "idt.h"
#include "interrupt_topology.h"
#include "ioapic.h"
#include "lapic.h"
#include "pic.h"
#include "ps2_keyboard.h"
#include "timer.h"
#include "../../firmware/acpi.h"
#include "../../firmware/acpi_discovery.h"

#include "../../diagnostics/diagnostics.h"
#include "../../core/panic.h"

#define X86_CR0_TASK_SWITCHED (1ULL << 3)

static bool early_arch_initialized;

static void fp_simd_trap_on_use(void)
{
    uint64_t cr0;

    __asm__ volatile (
        "mov %%cr0, %0"
        : "=r" (cr0)
    );

    cr0 |= X86_CR0_TASK_SWITCHED;

    __asm__ volatile (
        "mov %0, %%cr0"
        :
        : "r" (cr0)
        : "memory"
    );
}

void arch_early_init(void)
{
    if (early_arch_initialized) {
        kernel_panic(
            "Early x86-64 initialization already completed"
        );
    }

    /*
     * Keep maskable interrupts disabled throughout the descriptor-table
     * handoff and subsequent bootloader-memory reclamation.
     */
    __asm__ volatile ("cli" ::: "memory");

    diagnostics_write("[arch] Loading GDT\n");
    gdt_init();
    diagnostics_write("[arch] GDT loaded\n");

    diagnostics_write("[arch] Loading IDT\n");
    idt_init();
    diagnostics_write("[arch] IDT loaded\n");

    early_arch_initialized = true;
}

bool arch_early_init_complete(void)
{
    return early_arch_initialized;
}

bool arch_boot_reclaim_ready(void)
{
    if (!arch_early_init_complete()) {
        return false;
    }

    uint64_t rflags;

    __asm__ volatile (
        "pushfq\n\t"
        "popq %0"
        : "=r" (rflags)
        :
        : "memory"
    );

    /*
     * RFLAGS.IF is bit 9. No maskable interrupt may run while
     * ownership of bootloader-reclaimable memory is transferred.
     */
    if ((rflags & (1ULL << 9)) != 0) {
        return false;
    }

    return
        gdt_kernel_state_active() &&
        idt_kernel_state_active();
}

void arch_init(
    const uint8_t *rsdp,
    size_t rsdp_size)
{
    if (!arch_early_init_complete()) {
        kernel_panic(
            "Early x86-64 initialization has not completed"
        );
    }

    /*
     * Discover the firmware topology before enabling maskable
     * interrupts or programming the IOAPIC.
     *
     * No Q35 fallback is used: unsupported or invalid firmware
     * topology must be reported explicitly.
     */
    struct acpi_madt madt;

    if (
        !acpi_madt_discover(
            rsdp,
            rsdp_size,
            &madt
        )
    ) {
        kernel_panic(
            "Unable to discover ACPI MADT"
        );
    }

    struct interrupt_topology topology;

    if (
        !interrupt_topology_from_madt(
            &madt,
            &topology
        )
    ) {
        kernel_panic(
            "ACPI MADT describes an unsupported interrupt topology"
        );
    }

    uint64_t firmware_lapic_physical;

    if (
        !acpi_madt_local_apic_address_get(
            &madt,
            &firmware_lapic_physical
        )
    ) {
        kernel_panic(
            "Unable to resolve ACPI Local APIC address"
        );
    }

    /*
     * lapic_init() checks that the processor is using the
     * xAPIC MMIO interface and maps the address advertised
     * by IA32_APIC_BASE_MSR.
     */
    if (!lapic_init()) {
        kernel_panic(
            "Unable to initialize Local APIC"
        );
    }

    uint64_t active_lapic_physical;
    uint64_t active_lapic_virtual;

    if (
        !lapic_mapping_info(
            &active_lapic_physical,
            &active_lapic_virtual
        )
    ) {
        kernel_panic(
            "Unable to inspect active Local APIC mapping"
        );
    }

    if (
        active_lapic_physical !=
        firmware_lapic_physical
    ) {
        kernel_panic(
            "ACPI Local APIC address differs from active xAPIC"
        );
    }

    diagnostics_printf(
        "[arch] ACPI topology: LAPIC=%x IOAPIC=%x "
        "GSI-base=%u PIT-IRQ=%u PIT-GSI=%u "
        "active-low=%u level=%u\n",
        firmware_lapic_physical,
        topology.ioapic_physical_address,
        (uint64_t) topology.ioapic_gsi_base,
        (uint64_t) topology.timer_route.irq,
        (uint64_t) topology.timer_route.gsi,
        (uint64_t) topology.timer_route.active_low,
        (uint64_t) topology.timer_route.level_triggered
    );

    if (
        !ioapic_init(
            topology.ioapic_physical_address,
            topology.ioapic_gsi_base
        )
    ) {
        kernel_panic(
            "Unable to initialize IOAPIC"
        );
    }

    pic_disable();

    /*
     * PS/2 is optional platform hardware. A machine without a usable
     * i8042 controller must still be able to boot.
     */
    if (ps2_keyboard_init()) {
        struct interrupt_route keyboard_route;

        if (
            !interrupt_topology_isa_route_from_madt(
                &madt,
                PS2_KEYBOARD_ISA_IRQ,
                &keyboard_route
            )
        ) {
            kernel_panic(
                "Unable to resolve PS/2 keyboard interrupt route"
            );
        }

        if (
            !ioapic_route(
                keyboard_route.gsi,
                PS2_KEYBOARD_INTERRUPT_VECTOR,
                lapic_id(),
                keyboard_route.active_low,
                keyboard_route.level_triggered
            )
        ) {
            kernel_panic(
                "Unable to route PS/2 keyboard interrupt"
            );
        }

        diagnostics_printf(
            "[arch] PS/2 keyboard: IRQ=%u GSI=%u vector=%x "
            "active-low=%u level=%u\n",
            (uint64_t) keyboard_route.irq,
            (uint64_t) keyboard_route.gsi,
            (uint64_t) PS2_KEYBOARD_INTERRUPT_VECTOR,
            (uint64_t) keyboard_route.active_low,
            (uint64_t) keyboard_route.level_triggered
        );
    } else {
        diagnostics_write(
            "[arch] PS/2 keyboard unavailable\n"
        );
    }

    /*
     * ioapic_route() checks that the selected GSI belongs to
     * the initialized IOAPIC's redirection-entry range.
     */
    if (
        !ioapic_route(
            topology.timer_route.gsi,
            TIMER_INTERRUPT_VECTOR,
            lapic_id(),
            topology.timer_route.active_low,
            topology.timer_route.level_triggered
        )
    ) {
        kernel_panic(
            "Unable to route PIT interrupt"
        );
    }

    timer_init(100);

    diagnostics_write(
        "[arch] Enabling FP/SIMD trap-on-use policy\n"
    );

    fp_simd_trap_on_use();

    __asm__ volatile ("sti");
}
