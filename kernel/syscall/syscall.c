// SPDX-License-Identifier: GPL-2.0-only

#include "syscall.h"

#include "../diagnostics/diagnostics.h"
#include "../scheduler/scheduler.h"

uint64_t syscall_dispatch(uint64_t number, uint64_t argument0)
{
    switch (number) {
        case SYSCALL_DEBUG_PUTC:
            diagnostics_printf(
                "%c",
                (char) argument0
            );

            return 0;

        default:
            return UINT64_MAX;
    }
}
