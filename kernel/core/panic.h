// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#ifndef MYOS_KERNEL_CORE_PANIC_H
#define MYOS_KERNEL_CORE_PANIC_H

_Noreturn void kernel_halt(void);
_Noreturn void kernel_panic(const char *message);

#endif
