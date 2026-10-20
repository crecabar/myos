// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include <myos/kernel/core/cpu/cpu_local.h>

#include "../../arch/x86_64/cpu_identity.h"
#include <myos/kernel/core/execution/kernel_context.h>

#include <stddef.h>

bool cpu_local_init(
    struct cpu_local *cpu,
    uint32_t logical_id,
    uint32_t apic_id)
{
    if (cpu == NULL) {
        return false;
    }

    *cpu = (struct cpu_local) {
        .logical_id = logical_id,
        .apic_id = apic_id,
        .active_kernel_context = NULL,
        .initialized = true,
    };

    return true;
}

struct kernel_context *cpu_local_active_kernel_context(
    const struct cpu_local *cpu)
{
    if (
        cpu == NULL ||
        !cpu->initialized
    ) {
        return NULL;
    }

    return cpu->active_kernel_context;
}

bool cpu_local_activate_kernel_context(
    struct cpu_local *cpu,
    struct kernel_context *context)
{
    if (
        cpu == NULL ||
        !cpu->initialized ||
        context == NULL ||
        context->state != KERNEL_CONTEXT_RUNNING ||
        !context->prepared ||
        context->supervisor == NULL ||
        context->cpu.rsp == 0 ||
        cpu->active_kernel_context != NULL
    ) {
        return false;
    }

    cpu->active_kernel_context = context;

    return true;
}

bool cpu_local_deactivate_kernel_context(
    struct cpu_local *cpu,
    const struct kernel_context *context)
{
    if (
        cpu == NULL ||
        !cpu->initialized ||
        context == NULL ||
        cpu->active_kernel_context != context
    ) {
        return false;
    }

    cpu->active_kernel_context = NULL;

    return true;
}

bool cpu_local_matches_current_cpu(
    const struct cpu_local *cpu)
{
    if (
        cpu == NULL ||
        !cpu->initialized
    ) {
        return false;
    }

    uint32_t current_apic_id;

    if (!arch_cpu_apic_id(&current_apic_id)) {
        return false;
    }

    return cpu->apic_id == current_apic_id;
}
