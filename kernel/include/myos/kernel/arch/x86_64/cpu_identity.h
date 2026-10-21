// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#ifndef MYOS_KERNEL_ARCH_X86_64_CPU_IDENTITY_H
#define MYOS_KERNEL_ARCH_X86_64_CPU_IDENTITY_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Gets the hardware identity of the executing CPU.
 *
 * Returns false if the local APIC is not initialized.
 */
bool arch_cpu_apic_id(uint32_t *apic_id);

#endif
