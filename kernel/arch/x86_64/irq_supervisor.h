// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#ifndef MYOS_ARCH_X86_64_IRQ_SUPERVISOR_H
#define MYOS_ARCH_X86_64_IRQ_SUPERVISOR_H

#include <stdint.h>

/**
 * Enters a kernel supervisor on an independent Ring 0 stack.
 *
 * This is a one-way stack transition, not an IRETQ.
 *
 * The caller must already be in Ring 0, with maskable
 * interrupts disabled and any required IRQ acknowledgment
 * completed.
 *
 * stack_top must be 16-byte aligned and refer to a
 * writable kernel-owned stack.
 *
 * entry must not return. The supervisor is responsible
 * for the subsequent execution transition.
 *
 * Does not modify CR3, TSS, process state or IRQ frames.
 */
_Noreturn void irq_supervisor_enter(
    uintptr_t stack_top,
    void (*entry)(void *),
    void *argument
);

#endif
