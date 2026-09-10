// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file lapic.h
 * @brief x86 Local APIC interface.
 */

#ifndef MYOS_ARCH_X86_64_LAPIC_H
#define MYOS_ARCH_X86_64_LAPIC_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Initializes the processor Local APIC.
 *
 * The Local APIC physical base is obtained from IA32_APIC_BASE rather than
 * from platform-specific constants.
 *
 * @return true when the Local APIC was initialized; false otherwise.
 */
bool lapic_init(void);

/**
 * Returns the current Local APIC identifier.
 *
 * @return Local APIC ID of the executing processor.
 */
uint8_t lapic_id(void);

/**
 * Signals completion of the current APIC interrupt.
 */
void lapic_send_eoi(void);

#endif
