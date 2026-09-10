// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file ioapic.h
 * @brief x86 I/O APIC interrupt routing interface.
 */

#ifndef MYOS_ARCH_X86_64_IOAPIC_H
#define MYOS_ARCH_X86_64_IOAPIC_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Initializes access to an I/O APIC.
 *
 * @param physical_address Physical MMIO base discovered from platform data.
 * @param gsi_base
 *
 * @return true when the I/O APIC was initialized; false otherwise.
 */
bool ioapic_init(
    uint64_t physical_address,
    uint32_t gsi_base
);

/**
 * Routes a global system interrupt to a Local APIC.
 *
 * @param gsi Global System Interrupt number.
 * @param vector IDT vector delivered to the processor.
 * @param lapic_id Destination Local APIC identifier.
 * @param active_low Whether the interrupt signal is active-low.
 * @param level_triggered Whether the interrupt is level-triggered.
 *
 * @return true when the redirection entry was configured; false otherwise.
 */
bool ioapic_route(
    uint32_t gsi,
    uint8_t vector,
    uint8_t lapic_id,
    bool active_low,
    bool level_triggered
);

#endif
