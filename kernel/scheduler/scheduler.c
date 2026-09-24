// SPDX-License-Identifier: GPL-2.0-only

#include "scheduler.h"

#include "../arch/x86_64/paging.h"
#include "../arch/x86_64/usermode.h"
#include "../arch/x86_64/gdt.h"
#include "../arch/x86_64/interrupts.h"
#include "../arch/x86_64/timer.h"
#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"

#include <stddef.h>

#define SCHEDULER_QUANTUM_TICKS 10

static struct process *processes[SCHEDULER_MAX_PROCESSES];
static size_t process_count;
static size_t next_process_index;

static scheduler_terminated_handler terminated_handler;

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

static void scheduler_detach_terminated(
    struct process *process
);

void scheduler_init(void)
{
    for (size_t index = 0;
         index < SCHEDULER_MAX_PROCESSES;
         ++index) {
        processes[index] = NULL;
    }

    process_count = 0;
    current_process = NULL;
    next_process_index = 0;
    current_quantum_ticks = 0;
    terminated_handler = NULL;
}

void scheduler_set_terminated_handler(
    scheduler_terminated_handler handler)
{
    terminated_handler = handler;
}

bool scheduler_add(struct process *process)
{
    if (process == NULL) return false;
    if (process->state != PROCESS_STATE_READY) return false;

    for (size_t index = 0;
         index < SCHEDULER_MAX_PROCESSES;
         ++index) {
        if (processes[index] == process) {
            return false;
        }
    }

    if (process_count >= SCHEDULER_MAX_PROCESSES) {
        return false;
    }

    for (size_t index = 0;
         index < SCHEDULER_MAX_PROCESSES;
         ++index) {
        if (processes[index] != NULL) {
            continue;
        }

        processes[index] = process;
        ++process_count;

        return true;
    }

    kernel_panic(
        "Scheduler process count does not match occupied slots"
    );
}

bool scheduler_unregister_terminated(struct process *process)
{
    if (process == NULL) return false;
    if (process->state != PROCESS_STATE_TERMINATED) return false;
    if (process == current_process) return false;

    for (size_t index = 0;
         index < SCHEDULER_MAX_PROCESSES;
         ++index) {
        if (processes[index] != process) {
            continue;
        }

        if (process_count == 0) {
            kernel_panic(
                "Scheduler process count is inconsistent"
            );
        }

        processes[index] = NULL;
        --process_count;

        if (process_count == 0) {
            next_process_index = 0;
        }

        return true;
    }

    return false;
}

struct process *scheduler_current(void)
{
    return current_process;
}

void scheduler_wake_sleepers(uint64_t now_ticks)
{
    if (process_count == 0) {
        return;
    }

    for (size_t index = 0;
         index < SCHEDULER_MAX_PROCESSES;
         ++index) {
        struct process *process = processes[index];

        if (
            process == NULL ||
            process->state != PROCESS_STATE_SLEEPING
        ) {
            continue;
        }

        uint64_t duration = process->sleep_duration_ticks;

        /*
         * Every SLEEPING process must have been assigned a
         * valid, bounded interval before entering this state.
         */
        if (
            duration == 0 ||
            duration > TIMER_SLEEP_MAX_TICKS
        ) {
            kernel_panic(
                "Sleeping process has invalid timer metadata"
            );
        }

        uint64_t elapsed =
            (uint64_t) (
                now_ticks - process->sleep_start_ticks
            );

        if (elapsed < duration) {
            continue;
        }

        process->sleep_start_ticks = 0;
        process->sleep_duration_ticks = 0;
        process->state = PROCESS_STATE_READY;
    }
}

bool scheduler_wake_blocked(struct process *process)
{
    if (process == NULL) {
        return false;
    }

    /*
     * On this single-CPU scheduler, callers must serialize
     * event-driven wakeups against interrupt-driven scheduling.
     */
    uint64_t rflags;

    __asm__ volatile (
        "pushfq\n\t"
        "popq %0"
        : "=r"(rflags)
        :
        : "memory"
    );

    if ((rflags & (1ULL << 9)) != 0) {
        return false;
    }

    if (process == current_process) {
        return false;
    }

    /*
     * Check scheduler registration before inspecting process
     * state. An unrelated descriptor must not be awakened.
     */
    for (size_t index = 0;
         index < SCHEDULER_MAX_PROCESSES;
         ++index) {
        if (processes[index] != process) {
            continue;
        }

        if (process->state != PROCESS_STATE_BLOCKED) {
            return false;
        }

        process->state = PROCESS_STATE_READY;

        return true;
    }

    return false;
}

static struct process *scheduler_find_next_ready(void)
{
    if (process_count == 0) {
        return NULL;
    }

    for (size_t offset = 0;
         offset < SCHEDULER_MAX_PROCESSES;
         ++offset) {
        size_t index =
            (next_process_index + offset) %
            SCHEDULER_MAX_PROCESSES;

        struct process *process =
            processes[index];

        if (process == NULL) {
            continue;
        }

        if (process->state == PROCESS_STATE_READY) {
            next_process_index =
                (index + 1) %
                SCHEDULER_MAX_PROCESSES;

            return process;
        }
    }

    return NULL;
}

static _Noreturn void scheduler_enter_process(struct process *process)
{
    if (process == NULL) {
        kernel_panic("Scheduler attempted to run null process");
    }

    if (process->state != PROCESS_STATE_READY) {
        kernel_panic("Scheduler attempted to enter non-READY process");
    }

    if (!paging_address_space_activate(
        &process->memory->address_space
    )) {
        kernel_panic("Unable to activate scheduled process address space");
    }

    /*
     * A newly initialized process already has an initial
     * user context. A previously suspended process instead
     * has the context saved when it stopped executing.
     *
     * Both cases enter user mode through the same path.
     */
    struct interrupt_context context;

    scheduler_load_context(&context, process);

    current_quantum_ticks = 0;

    current_process = process;
    process->state = PROCESS_STATE_RUNNING;

    diagnostics_printf(
        "[scheduler] Running PID %u\n",
        process->id
    );

    usermode_resume(&context);
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

    /*
     * Idle owns the no-runnable-process transition with interrupts disabled.
     *
     * interrupts_wait() enables interrupts immediately before HLT and returns
     * with them disabled again. This makes the READY check and subsequent
     * sleep race-free on the current single-CPU scheduler.
     */
    interrupts_disable();

    /*
     * No user process owns the CPU while the scheduler is idle.
     * Do not retain the address space of a process that has just
     * entered SLEEPING or BLOCKED.
     *
     * A process selected after wakeup activates its own address
     * space in scheduler_enter_process().
     */
    struct paging_address_space *kernel_space =
        paging_kernel_address_space();

    if (
        kernel_space == NULL ||
        !paging_address_space_activate(kernel_space)
    ) {
        kernel_panic(
            "Unable to activate kernel address space during idle"
        );
    }

    for (;;) {
        struct process *next =
            scheduler_find_next_ready();

        if (next != NULL) {
            scheduler_enter_process(next);
        }

        interrupts_wait();
    }
}

static void scheduler_detach_terminated(
    struct process *process)
{
    if (process == NULL) {
        kernel_panic(
            "Scheduler attempted to detach null process"
        );
    }

    if (process->state != PROCESS_STATE_TERMINATED) {
        kernel_panic(
            "Scheduler attempted to detach non-terminated process"
        );
    }

    struct paging_address_space *kernel_space =
        paging_kernel_address_space();

    if (kernel_space == NULL) {
        kernel_panic(
            "Kernel address space unavailable during process detach"
        );
    }

    if (!paging_address_space_activate(kernel_space)) {
        kernel_panic(
            "Unable to activate kernel address space during process detach"
        );
    }

    if (!scheduler_unregister_terminated(process)) {
        kernel_panic(
            "Unable to unregister terminated process"
        );
    }

    if (terminated_handler != NULL) {
        terminated_handler(process);
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
    scheduler_detach_terminated(
        terminated_process
    );

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

    scheduler_detach_terminated(
        exiting_process
    );

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

bool scheduler_sleep_current(
    struct interrupt_context *context,
    uint64_t duration_ticks)
{
    if (context == NULL || current_process == NULL) {
        return false;
    }

    if (
        current_process->state != PROCESS_STATE_RUNNING ||
        (context->cs & 0x3) != 3
    ) {
        return false;
    }

    /*
     * This operation is called from the syscall interrupt path.
     * Interrupts must remain disabled while the scheduler
     * modifies the current process and selects its successor.
     */
    uint64_t rflags;

    __asm__ volatile (
        "pushfq\n\t"
        "popq %0"
        : "=r"(rflags)
        :
        : "memory"
    );

    if ((rflags & (1ULL << 9)) != 0) {
        return false;
    }

    /*
     * A process that resumes with interrupts disabled could
     * prevent further timer-driven scheduling and wakeups.
     */
    if ((context->rflags & (1ULL << 9)) == 0) {
        return false;
    }

    if (duration_ticks > TIMER_SLEEP_MAX_TICKS) {
        return false;
    }

    if (duration_ticks == 0) {
        context->rax = 0;
        return true;
    }

    struct process *sleeping_process = current_process;

    /*
     * Preserve the interrupted user context before selecting
     * another process. The saved RAX is the eventual return
     * value of the sleep request.
     */
    scheduler_save_context(
        sleeping_process,
        context
    );

    sleeping_process->context.rax = 0;

    sleeping_process->sleep_start_ticks = timer_ticks();
    sleeping_process->sleep_duration_ticks = duration_ticks;
    sleeping_process->state = PROCESS_STATE_SLEEPING;

    current_process = NULL;

    struct process *next = scheduler_find_next_ready();

    if (next == NULL) {
        /*
         * The idle loop continues receiving timer interrupts.
         * scheduler_wake_sleepers() will eventually move this
         * process back to READY.
         */
        scheduler_idle();
    }

    /*
     * The interrupt frame now belongs to the next process.
     * The caller must not overwrite it on successful return.
     */
    scheduler_switch_from_interrupt(
        context,
        next
    );

    return true;
}

bool scheduler_block_current(struct interrupt_context *context)
{
    if (context == NULL || current_process == NULL) {
        return false;
    }

    if (
        current_process->state != PROCESS_STATE_RUNNING ||
        (context->cs & 0x3) != 3
    ) {
        return false;
    }

    /*
     * The state transition and successor selection must be
     * atomic with respect to timer interrupts on this CPU.
     */
    uint64_t rflags;

    __asm__ volatile (
        "pushfq\n\t"
        "popq %0"
        : "=r"(rflags)
        :
        : "memory"
    );

    if ((rflags & (1ULL << 9)) != 0) {
        return false;
    }

    if ((context->rflags & (1ULL << 9)) == 0) {
        return false;
    }

    struct process *blocked_process = current_process;

    /*
     * BLOCKED is an event-driven wait, not a timed wait.
     * The process must not retain timer-wait metadata.
     */
    if (
        blocked_process->sleep_start_ticks != 0 ||
        blocked_process->sleep_duration_ticks != 0
    ) {
        kernel_panic(
            "Running process retained sleep metadata"
        );
    }

    scheduler_save_context(
        blocked_process,
        context
    );

    blocked_process->context.rax = 0;
    blocked_process->state = PROCESS_STATE_BLOCKED;

    current_process = NULL;

    struct process *next = scheduler_find_next_ready();

    if (next == NULL) {
        scheduler_idle();
    }

    scheduler_switch_from_interrupt(
        context,
        next
    );

    return true;
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
