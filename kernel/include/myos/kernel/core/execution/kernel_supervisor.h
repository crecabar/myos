// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#ifndef MYOS_KERNEL_CORE_EXECUTION_KERNEL_SUPERVISOR_H
#define MYOS_KERNEL_CORE_EXECUTION_KERNEL_SUPERVISOR_H

#include <stdbool.h>

struct cpu_local;
struct kernel_switch_context;

typedef bool (*kernel_supervisor_dispatch_fn)(
    struct cpu_local *cpu
);

/**
 * Final continuation after a one-way supervisor dispatch.
 *
 * The continuation must not return. It is responsible
 * for transferring execution to a valid next context.
 *
 * It receives whether a worker was actually dispatched.
 */
typedef void (*kernel_supervisor_completion_fn)(
    bool dispatched,
    void *argument
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

/**
 * Enters the BSP supervisor through a one-way stack transition.
 *
 * Preconditions:
 * - BSP supervisor initialized.
 * - Running on the BSP in Ring 0.
 * - Maskable interrupts disabled.
 * - Kernel mappings available in the active address space.
 * - No supervisor dispatch already active.
 *
 * The original stack is abandoned. The supervisor switches
 * to the kernel address space before dispatching the worker.
 *
 * completion must transfer execution elsewhere and never
 * return. Its argument must remain valid after the original
 * stack has been abandoned.
 *
 * This function does not consume scheduler handoffs,
 * perform IRETQ or acknowledge an IRQ.
 */
_Noreturn void kernel_supervisor_bsp_enter(
    kernel_supervisor_dispatch_fn dispatch,
    kernel_supervisor_completion_fn completion,
    void *argument
);

#endif
