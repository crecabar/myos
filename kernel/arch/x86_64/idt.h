// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#ifndef MYOS_ARCH_X86_64_IDT_H
#define MYOS_ARCH_X86_64_IDT_H

#include <stdbool.h>

void idt_init(void);

/**
 * Verifies that the CPU uses the kernel-owned IDT.
 */
bool idt_kernel_state_active(void);

#endif
