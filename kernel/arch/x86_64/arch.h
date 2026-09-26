// SPDX-License-Identifier: GPL-2.0-only

#ifndef MYOS_ARCH_X86_64_ARCH_H
#define MYOS_ARCH_X86_64_ARCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Installs kernel-owned GDT, TSS and IDT before bootloader-memory
 * reclamation. Leaves maskable interrupts disabled.
 */
void arch_early_init(void);

/**
 * Reports whether the early CPU descriptor-table handoff completed.
 */
bool arch_early_init_complete(void);

/**
 * Verifies the CPU state required at the boot-memory reclamation point.
 *
 * Requires kernel-owned descriptor tables and disabled maskable
 * interrupts.
 */
bool arch_boot_reclaim_ready(void);

/**
 * Initializes interrupt controllers, timer and remaining CPU state.
 *
 * Discovers the interrupt topology from the kernel-owned RSDP snapshot.
 * Requires arch_early_init() and installation of the kernel-owned direct
 * map to have completed.
 *
 * @param rsdp Kernel-owned RSDP snapshot.
 * @param rsdp_size Number of available snapshot bytes.
 */
void arch_init(
    const uint8_t *rsdp,
    size_t rsdp_size
);

/**
 * Resets the machine.
 *
 * Attempts the legacy i8042 CPU-reset command first and falls back
 * to an architectural triple fault if the controller does not reset
 * the processor.
 */
_Noreturn void arch_reset(void);

#endif
