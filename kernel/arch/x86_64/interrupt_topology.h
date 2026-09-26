// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file interrupt_topology.h
 * @brief x86 interrupt-controller topology discovered from the platform.
 */

#ifndef MYOS_ARCH_X86_64_INTERRUPT_TOPOLOGY_H
#define MYOS_ARCH_X86_64_INTERRUPT_TOPOLOGY_H

#include <stdbool.h>
#include <stdint.h>

struct acpi_madt;

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
 * Resolves one legacy ISA IRQ through a validated MADT.
 *
 * When no Interrupt Source Override exists for the requested IRQ, the
 * identity ISA mapping is used: IRQ N -> GSI N, active-high and
 * edge-triggered.
 *
 * Duplicate overrides and unsupported polarity/trigger encodings are
 * rejected.
 *
 * @param madt Previously validated MADT.
 * @param irq Legacy ISA IRQ number.
 * @param route Receives the resolved interrupt route.
 *
 * @return true when an unambiguous route was obtained; false otherwise.
 */
bool interrupt_topology_isa_route_from_madt(
    const struct acpi_madt *madt,
    uint8_t irq,
    struct interrupt_route *route
);

/**
 * Builds the interrupt topology required by MyOS from a validated MADT.
 *
 * This initial implementation supports one IOAPIC. It resolves the ISA
 * IRQ0 route using its interrupt source override, if present, or the
 * identity mapping otherwise.
 *
 * It does not initialize interrupt controllers or access MMIO.
 *
 * @param madt Previously validated MADT.
 * @param topology Receives the resulting topology.
 *
 * @return true when the MADT describes a topology supported by this
 *         implementation; false otherwise.
 */
bool interrupt_topology_from_madt(
    const struct acpi_madt *madt,
    struct interrupt_topology *topology
);

#endif
