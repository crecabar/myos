// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file interrupt_topology.h
 * @brief x86 interrupt-controller topology discovered from the platform.
 */

#ifndef MYOS_ARCH_X86_64_INTERRUPT_TOPOLOGY_H
#define MYOS_ARCH_X86_64_INTERRUPT_TOPOLOGY_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Describes the routing of one legacy ISA interrupt through the IOAPIC.
 */
struct interrupt_route {
    uint8_t irq;
    uint32_t gsi;
    bool active_low;
    bool level_triggered;
};

/**
 * Describes the interrupt-controller topology required by MyOS.
 */
struct interrupt_topology {
    uint64_t ioapic_physical_address;
    uint32_t ioapic_gsi_base;
    struct interrupt_route timer_route;
};

/**
 * Discovers the platform interrupt topology.
 *
 * The current implementation supplies the known QEMU q35 topology as an
 * early bring-up mechanism. This function is intentionally isolated so it can
 * later be backed by ACPI MADT discovery without changing the LAPIC, IOAPIC,
 * timer, or scheduler interfaces.
 *
 * @param topology Structure that receives the discovered topology.
 *
 * @return true when a usable interrupt topology was obtained; false otherwise.
 */
bool interrupt_topology_discover(struct interrupt_topology *topology);

#endif
