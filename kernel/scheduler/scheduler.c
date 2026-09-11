// SPDX-License-Identifier: GPL-2.0-only

#include "scheduler.h"

#include "../arch/x86_64/paging.h"
#include "../arch/x86_64/usermode.h"
#include "../arch/x86_64/gdt.h"
#include "../arch/x86_64/interrupts.h"
#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"

#include <stddef.h>

#define SCHEDULER_MAX_PROCESSES 8
#define SCHEDULER_QUANTUM_TICKS 10

static struct process *processes[SCHEDULER_MAX_PROCESSES];
static size_t process_count;
static size_t next_process_index;

static struct process *current_process;
static uint64_t current_quantum_ticks;

static struct process *scheduler_find_next_ready(void);
static _Noreturn void scheduler_enter_process(struct process *process);
static _Noreturn void scheduler_idle(void);

static void scheduler_save_context(
    struct process *process,
    const struct interrupt_context *context
);

static void scheduler_load_context(
    struct interrupt_context *context,
    const struct process *process
);

static void scheduler_switch_from_interrupt(
    struct interrupt_context *context,
    struct process *next
);

void scheduler_init(void)
{
    process_count = 0;
    current_process = NULL;
    next_process_index = 0;
    current_quantum_ticks = 0;
}

bool scheduler_add(struct process *process)
{
    if (process == NULL) return false;
    if (process->state != PROCESS_STATE_READY) return false;
    if (process_count >= SCHEDULER_MAX_PROCESSES) return false;

    processes[process_count] = process;
    ++process_count;

    return true;
}

struct process *scheduler_current(void)
{
    return current_process;
}

static struct process *scheduler_find_next_ready(void)
{
    if (process_count == 0) {
        return NULL;
    }

    for (size_t offset = 0; offset < process_count; ++offset) {
        size_t index =
            (next_process_index + offset) % process_count;

        if (processes[index]->state == PROCESS_STATE_READY) {
            next_process_index =
                (index + 1) % process_count;

            return processes[index];
        }
    }

    return NULL;
}

static _Noreturn void scheduler_enter_process(struct process *process)
{
    if (process == NULL) {
        kernel_panic("Scheduler attempted to run null process");
    }

    if (!paging_address_space_activate(
        &process->memory->address_space
    )) {
        kernel_panic("Unable to activate scheduled process address space");
    }

    current_quantum_ticks = 0;

    current_process = process;
    process->state = PROCESS_STATE_RUNNING;

    diagnostics_printf(
        "[scheduler] Running PID %u\n",
        process->id
    );

    usermode_enter(
        process->layout->entry_point,
        process->layout->stack.stack_top
    );
}

_Noreturn void scheduler_run(void)
{
    struct process *next = scheduler_find_next_ready();

    if (next == NULL) {
        scheduler_idle();
    }

    scheduler_enter_process(next);
}

static _Noreturn void scheduler_idle(void)
{
    diagnostics_write("[scheduler] No runnable processes\n");
    diagnostics_write("[scheduler] System idle\n");

    for (;;) {
        __asm__ volatile ("hlt");
    }
}

void scheduler_terminate_current_from_interrupt(
    struct interrupt_context *context,
    enum process_termination_reason reason)
{
    if (context == NULL) {
        kernel_panic("Scheduler received null interrupt context");
    }

    if (current_process == NULL) {
        kernel_panic("Scheduler has no current process");
    }

    struct process *terminated_process = current_process;

    terminated_process->state = PROCESS_STATE_TERMINATED;
    terminated_process->termination_reason = reason;
    terminated_process->exit_status = 0;

    diagnostics_printf(
        "[scheduler] PID %u terminated\n",
        terminated_process->id
    );

    current_process = NULL;

    struct process *next = scheduler_find_next_ready();

    if (next == NULL) {
        scheduler_idle();
    }

    scheduler_switch_from_interrupt(
        context,
        next
    );
}

void scheduler_exit_current(
    struct interrupt_context *context,
    uint64_t status)
{
    if (context == NULL) {
        kernel_panic("Scheduler received null interrupt context");
    }

    if (current_process == NULL) {
        kernel_panic("Scheduler has no current process");
    }

    struct process *exiting_process = current_process;

    exiting_process->state = PROCESS_STATE_TERMINATED;
    exiting_process->termination_reason = PROCESS_TERMINATION_EXITED;
    exiting_process->exit_status = status;

    diagnostics_printf(
        "[scheduler] PID %u exited with status %u\n",
        exiting_process->id,
        status
    );

    current_process = NULL;

    struct process *next = scheduler_find_next_ready();

    if (next == NULL) {
        scheduler_idle();
    }

    scheduler_switch_from_interrupt(
        context,
        next
    );
}

void scheduler_yield_current(struct interrupt_context *context)
{
    if (context == NULL) {
        kernel_panic("Scheduler received null interrupt context");
    }

    if (current_process == NULL) {
        kernel_panic("Scheduler has no current process");
    }

    struct process *yielding_process = current_process;

    scheduler_save_context(
        yielding_process,
        context
    );

    /*
     * yield() returns zero when this process eventually resumes.
     */
    yielding_process->context.rax = 0;

    yielding_process->state = PROCESS_STATE_READY;

    current_process = NULL;

    struct process *next = scheduler_find_next_ready();

    if (next == NULL) {
        kernel_panic("Yield left scheduler without runnable process");
    }

    scheduler_switch_from_interrupt(
        context,
        next
    );
}

void scheduler_preempt_current(struct interrupt_context *context)
{
    if (context == NULL) {
        kernel_panic("Scheduler received null interrupt context");
    }

    if (current_process == NULL) {
        kernel_panic("Scheduler has no current process to preempt");
    }

    struct process *preempted_process = current_process;

    scheduler_save_context(
        preempted_process,
        context
    );

    preempted_process->state = PROCESS_STATE_READY;

    current_process = NULL;

    struct process *next = scheduler_find_next_ready();

    if (next == NULL) {
        kernel_panic("Preemption left scheduler without runnable process");
    }

    scheduler_switch_from_interrupt(
        context,
        next
    );
}

void scheduler_tick(struct interrupt_context *context)
{
    if (context == NULL) return;
    if (current_process == NULL) return;
    if (current_process->state != PROCESS_STATE_RUNNING) return;

    ++current_quantum_ticks;

    if (current_quantum_ticks < SCHEDULER_QUANTUM_TICKS) {
        return;
    }

    current_quantum_ticks = 0;

    scheduler_preempt_current(context);
}

static void scheduler_save_context(
    struct process *process,
    const struct interrupt_context *context)
{
    process->context.r15 = context->r15;
    process->context.r14 = context->r14;
    process->context.r13 = context->r13;
    process->context.r12 = context->r12;
    process->context.r11 = context->r11;
    process->context.r10 = context->r10;
    process->context.r9 = context->r9;
    process->context.r8 = context->r8;

    process->context.rbp = context->rbp;
    process->context.rdi = context->rdi;
    process->context.rsi = context->rsi;
    process->context.rdx = context->rdx;
    process->context.rcx = context->rcx;
    process->context.rbx = context->rbx;
    process->context.rax = context->rax;

    process->context.rip = context->rip;
    process->context.rsp = context->rsp;
    process->context.rflags = context->rflags;
}

static void scheduler_load_context(
    struct interrupt_context *context,
    const struct process *process)
{
    context->r15 = process->context.r15;
    context->r14 = process->context.r14;
    context->r13 = process->context.r13;
    context->r12 = process->context.r12;
    context->r11 = process->context.r11;
    context->r10 = process->context.r10;
    context->r9 = process->context.r9;
    context->r8 = process->context.r8;

    context->rbp = process->context.rbp;
    context->rdi = process->context.rdi;
    context->rsi = process->context.rsi;
    context->rdx = process->context.rdx;
    context->rcx = process->context.rcx;
    context->rbx = process->context.rbx;
    context->rax = process->context.rax;

    context->vector = 0x80;
    context->error_code = 0;

    context->rip = process->context.rip;
    context->cs = GDT_USER_CODE_SELECTOR;
    context->rflags = process->context.rflags;
    context->rsp = process->context.rsp;
    context->ss = GDT_USER_DATA_SELECTOR;
}

static void scheduler_switch_from_interrupt(
    struct interrupt_context *context,
    struct process *next)
{
    if (next == NULL) {
        kernel_panic("Scheduler attempted to switch to null process");
    }

    if (!paging_address_space_activate(
        &next->memory->address_space
    )) {
        kernel_panic("Unable to activate scheduled process address space");
    }

    scheduler_load_context(context, next);

    current_quantum_ticks = 0;

    current_process = next;
    next->state = PROCESS_STATE_RUNNING;

    diagnostics_printf(
        "[scheduler] Running PID %u\n",
        next->id
    );
}
