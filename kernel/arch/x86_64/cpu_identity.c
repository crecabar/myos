// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include <myos/kernel/arch/x86_64/cpu_identity.h>

#include <myos/kernel/arch/x86_64/lapic.h>

#include <stddef.h>

bool arch_cpu_apic_id(uint32_t *apic_id)
{
    if (apic_id == NULL) {
        return false;
    }

    uint64_t physical;
    uint64_t virtual;

    if (!lapic_mapping_info(&physical, &virtual)) {
        return false;
    }

    *apic_id = (uint32_t) lapic_id();

    return true;
}
