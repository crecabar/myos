// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#ifndef MYOS_KERNEL_CORE_EXECUTION_KERNEL_DISPATCH_H
#define MYOS_KERNEL_CORE_EXECUTION_KERNEL_DISPATCH_H

#include <stdbool.h>

struct cpu_local;
struct kernel_context;

/**
 * Dispatches a READY kernel worker on the supplied CPU.
 *
 * Requires execution on the supervisor stack, Ring 0,
 * maskable interrupts disabled, and serialized CPU-local
 * state. Both descriptors must remain alive.
 *
 * Returns true after the worker yields to WAITING or
 * completes in TERMINATED.
 *
 * Does not select the next worker.
 */
bool kernel_context_dispatch(
    struct cpu_local *cpu,
    struct kernel_context *context
);

#endif
