// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#ifndef MYOS_CORE_EXECUTION_KERNEL_SUPERVISOR_H
#define MYOS_CORE_EXECUTION_KERNEL_SUPERVISOR_H

#include "../../arch/x86_64/kernel_switch.h"

#include <stdbool.h>

struct cpu_local;

typedef bool (*kernel_supervisor_dispatch_fn)(
    struct cpu_local *cpu
);

/**
 * Initializes the persistent bootstrap CPU supervisor.
 *
 * Initialization is idempotent on the same CPU.
 * Does not switch stacks, change CR3 or execute workers.
 */
bool kernel_supervisor_bsp_initialize(void);

/**
 * Returns the persistent cooperative supervisor context.
 *
 * The returned storage remains valid for the kernel lifetime.
 * NULL means the supervisor has not been initialized.
 */
struct kernel_switch_context *
kernel_supervisor_bsp_context(void);

/**
 * Executes a dispatch callback on the BSP supervisor stack.
 *
 * Requires Ring 0, interrupts disabled, kernel CR3 active
 * and execution outside IRQ context.
 *
 * The callback may cooperatively dispatch a kernel worker
 * but must eventually return.
 *
 * This synchronous interface is for supervisor dispatch
 * and regression testing. It is not the IRQ-return path.
 */
bool kernel_supervisor_bsp_dispatch(
    kernel_supervisor_dispatch_fn dispatch
);

#endif
