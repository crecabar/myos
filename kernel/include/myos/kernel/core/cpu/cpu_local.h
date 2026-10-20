// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#ifndef MYOS_KERNEL_CORE_CPU_CPU_LOCAL_H
#define MYOS_KERNEL_CORE_CPU_CPU_LOCAL_H

#include <stdbool.h>
#include <stdint.h>

struct kernel_context;

/*
 * Logical execution state associated with one CPU.
 *
 * This structure does not own the active kernel context.
 * Its storage must remain stable while the CPU uses it.
 */
struct cpu_local {
    uint32_t logical_id;
    uint32_t apic_id;

    struct kernel_context *active_kernel_context;

    bool initialized;
};

/*
 * Initializes an explicit CPU-local descriptor.
 *
 * Does not select a CPU, access LAPIC registers,
 * or register this descriptor as the current CPU.
 */
bool cpu_local_init(
    struct cpu_local *cpu,
    uint32_t logical_id,
    uint32_t apic_id
);

/*
 * Returns the borrowed active worker reference.
 *
 * NULL also represents execution outside a kernel worker.
 */
struct kernel_context *cpu_local_active_kernel_context(
    const struct cpu_local *cpu
);

/**
 * Registers a borrowed worker reference in an idle CPU-local
 * execution slot.
 *
 * Does not perform a context switch.
 *
 * The caller must serialize access to this CPU-local state.
 */
bool cpu_local_activate_kernel_context(
    struct cpu_local *cpu,
    struct kernel_context *context
);

/**
 * Removes the currently registered worker.
 *
 * Only the matching context may be deactivated.
 * Does not perform a context switch.
 */
bool cpu_local_deactivate_kernel_context(
    struct cpu_local *cpu,
    const struct kernel_context *context
);

/**
 * Checks whether this CPU-local descriptor corresponds
 * to the processor currently executing the caller.
 *
 * Does not modify the descriptor.
 */
bool cpu_local_matches_current_cpu(
    const struct cpu_local *cpu
);

#endif
