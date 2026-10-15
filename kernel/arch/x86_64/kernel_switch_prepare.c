// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include "kernel_switch.h"

#include <stddef.h>
#include <stdint.h>

bool kernel_switch_prepare(
    struct kernel_switch_context *cpu,
    uintptr_t stack_bottom,
    uintptr_t stack_top,
    void (*entry)(void))
{
    if (cpu == NULL || entry == NULL) {
        return false;
    }

    if (
        stack_bottom == 0 ||
        stack_top <= stack_bottom ||
        (stack_bottom & 15U) != 0 ||
        (stack_top & 15U) != 0
    ) {
        return false;
    }

    if (stack_top - stack_bottom < 32U) {
        return false;
    }

    /*
     * The first switch ends in RET.
     *
     * Place the initial instruction address at top - 16,
     * giving the entry function RSP % 16 == 8.
     */
    uintptr_t initial_rsp = stack_top - 16U;

    void (**return_slot)(void) =
        (void (**)(void)) initial_rsp;

    *return_slot = entry;

    *cpu = (struct kernel_switch_context) {
        .rsp = initial_rsp,
    };

    return true;
}
