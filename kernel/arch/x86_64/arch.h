// SPDX-License-Identifier: GPL-2.0-only

#ifndef MYOS_ARCH_X86_64_ARCH_H
#define MYOS_ARCH_X86_64_ARCH_H

#include <stdbool.h>

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
 * Requires arch_early_init() to have completed.
 */
void arch_init(void);

#endif