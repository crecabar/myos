// SPDX-License-Identifier: GPL-2.0-only

#include "image.h"
#include "process.h"

#include <stddef.h>

bool process_context_initialize(
    struct process_context *context,
    const struct process_layout *layout)
{
    if (context == NULL) return false;
    if (layout == NULL) return false;

    if (
        layout->initial_rsp <
        layout->stack.base_address ||
        layout->initial_rsp >
        layout->stack.stack_top
    ) {
        return false;
    }

    if ((layout->initial_rsp & 0xFULL) != 0) {
        return false;
    }

    context->r15 = 0;
    context->r14 = 0;
    context->r13 = 0;
    context->r12 = 0;
    context->r11 = 0;
    context->r10 = 0;
    context->r9 = 0;
    context->r8 = 0;

    context->rbp = 0;
    context->rdi = 0;
    context->rsi = 0;
    context->rdx = 0;
    context->rcx = 0;
    context->rbx = 0;
    context->rax = 0;

    context->rip = layout->entry_point;
    context->rsp = layout->initial_rsp;
    context->rflags = 0x202;

    return true;
}

bool process_init(
    struct process *process,
    uint64_t id,
    struct process_memory *memory,
    struct process_layout *layout)
{
    if (process == NULL) return false;
    if (memory == NULL) return false;
    if (layout == NULL) return false;

    if (!process_context_initialize(
        &process->context,
        layout
    )) {
        return false;
    }

    process->id = id;
    process->state = PROCESS_STATE_READY;
    process->termination_reason = PROCESS_TERMINATION_NONE;
    process->exit_status = 0;

    process->sleep_start_ticks = 0;
    process->sleep_duration_ticks = 0;

    process->image = NULL;
    process->memory = memory;
    process->layout = layout;

    return true;
}

bool process_reclaim_resources(struct process *process)
{
    if (process == NULL) return false;
    if (process->state != PROCESS_STATE_TERMINATED) return false;
    if (process->memory == NULL) return false;
    if (process->layout == NULL) return false;

    if (
        process->image != NULL &&
        (
            process->memory !=
                &process->image->memory ||
            process->layout !=
                &process->image->layout
        )
    ) {
        return false;
    }

    if (!process_layout_destroy(
        process->memory,
        process->layout
    )) {
        return false;
    }

    if (!process_memory_destroy(
        process->memory
    )) {
        return false;
    }

    process->image = NULL;
    process->layout = NULL;
    process->memory = NULL;

    return true;
}
