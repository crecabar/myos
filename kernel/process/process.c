// SPDX-License-Identifier: GPL-2.0-only

#include "process.h"

#include <stddef.h>

bool process_init(
    struct process *process,
    uint64_t id,
    struct process_memory *memory,
    struct process_layout *layout)
{
    if (process == NULL) return false;
    if (memory == NULL) return false;
    if (layout == NULL) return false;

    process->id = id;
    process->state = PROCESS_STATE_READY;
    process->termination_reason = PROCESS_TERMINATION_NONE;
    process->exit_status = 0;
    process->memory = memory;
    process->layout = layout;

    process->context.r15 = 0;
    process->context.r14 = 0;
    process->context.r13 = 0;
    process->context.r12 = 0;
    process->context.r11 = 0;
    process->context.r10 = 0;
    process->context.r9 = 0;
    process->context.r8 = 0;

    process->context.rbp = 0;
    process->context.rdi = 0;
    process->context.rsi = 0;
    process->context.rdx = 0;
    process->context.rcx = 0;
    process->context.rbx = 0;
    process->context.rax = 0;

    process->context.rip = layout->entry_point;
    process->context.rsp = layout->stack.stack_top;
    process->context.rflags = 0x202; //0x002 = reserved, mandatory; 0x200 = IF, interrupt enable flag

    return true;
}
