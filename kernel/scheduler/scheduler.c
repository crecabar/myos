// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include "scheduler.h"

#include "../arch/x86_64/paging.h"
#include "../arch/x86_64/usermode.h"
#include "../arch/x86_64/gdt.h"
#include "../arch/x86_64/interrupts.h"
#include "../arch/x86_64/timer.h"
#include "../core/execution/kernel_dispatch.h"
#include "../core/execution/kernel_supervisor.h"
#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../process/lifecycle.h"

#include <stddef.h>

#define SCHEDULER_QUANTUM_TICKS 10

static struct process *processes[SCHEDULER_MAX_PROCESSES];
static size_t process_count;
static size_t next_process_index;

static scheduler_terminated_handler terminated_handler;
static scheduler_idle_work_handler idle_work_handler;

static struct kernel_context *kernel_worker;
static struct scheduler_kernel_handoff kernel_handoff;

static struct process *current_process;
static uint64_t current_quantum_ticks;

#if MYOS_KERNEL_TESTS
static uint64_t scheduler_test_preemption_count_value;
static uint64_t scheduler_test_cross_address_space_preemption_count_value;
#endif

static struct process *scheduler_find_next_ready(void);
static _Noreturn void scheduler_enter_process(struct process *process);
static _Noreturn void scheduler_idle(void);
static bool scheduler_dispatch_idle_work(void);
static bool scheduler_dispatch_idle_kernel_worker(void);

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

static bool scheduler_block_current_internal(
    struct interrupt_context *context,
    bool preserve_context
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
    idle_work_handler = NULL;
    kernel_worker = NULL;
    scheduler_kernel_handoff_init(&kernel_handoff);

#if MYOS_KERNEL_TESTS
    scheduler_test_preemption_count_value = 0;
    scheduler_test_cross_address_space_preemption_count_value = 0;
#endif
}

bool scheduler_register_kernel_worker(
    struct kernel_context *context)
{
    if (
        context == NULL ||
        context->state != KERNEL_CONTEXT_READY ||
        !context->prepared ||
        context->supervisor == NULL ||
        kernel_worker != NULL
    ) {
        return false;
    }

    kernel_worker = context;

    return true;
}

bool scheduler_unregister_kernel_worker(
    struct kernel_context *context)
{
    if (
        context == NULL ||
        kernel_worker != context ||
        context->state == KERNEL_CONTEXT_RUNNING ||
        kernel_context_owner(context) != KERNEL_CONTEXT_NO_CPU
    ) {
        return false;
    }

    kernel_worker = NULL;

    return true;
}

bool scheduler_dispatch_kernel_worker(
    struct cpu_local *cpu)
{
    if (
        cpu == NULL ||
        kernel_worker == NULL ||
        kernel_worker->state != KERNEL_CONTEXT_READY
    ) {
        return false;
    }

    return kernel_context_dispatch(
        cpu,
        kernel_worker
    );
}

bool scheduler_kernel_worker_ready(void)
{
    return
        kernel_worker != NULL &&
        kernel_worker->state == KERNEL_CONTEXT_READY &&
        kernel_worker->prepared &&
        kernel_worker->supervisor != NULL &&
        kernel_context_owner(kernel_worker) ==
            KERNEL_CONTEXT_NO_CPU;
}

bool scheduler_kernel_worker_handoff_needed(
    const struct interrupt_context *context)
{
    if (
        context == NULL ||
        (context->cs & 0x3U) != 3U ||
        current_process == NULL ||
        current_process->state != PROCESS_STATE_RUNNING
    ) {
        return false;
    }

    return scheduler_kernel_worker_ready();
}

bool scheduler_capture_user_interrupt(
    const struct interrupt_context *source,
    struct interrupt_context *destination)
{
    if (
        source == NULL ||
        destination == NULL ||
        source == destination ||
        (source->cs & 0x3U) != 3U ||
        (source->ss & 0x3U) != 3U
    ) {
        return false;
    }

    /*
     * Store the complete architectural interrupt frame.
     *
     * The destination must have independent storage
     * whose lifetime covers the future handoff.
     */
    *destination = *source;

    return true;
}

void scheduler_kernel_handoff_init(
    struct scheduler_kernel_handoff *handoff)
{
    if (handoff == NULL) {
        return;
    }

    *handoff = (struct scheduler_kernel_handoff) {0};
}

bool scheduler_kernel_handoff_prepare(
    struct scheduler_kernel_handoff *handoff,
    struct process *owner,
    const struct interrupt_context *frame)
{
    if (
        handoff == NULL ||
        owner == NULL ||
        frame == NULL ||
        handoff->pending ||
        owner->state != PROCESS_STATE_RUNNING
    ) {
        return false;
    }

    struct interrupt_context captured;

    if (!scheduler_capture_user_interrupt(
        frame,
        &captured
    )) {
        return false;
    }

    /*
     * Publication occurs last.
     *
     * On the current single-CPU scheduler, callers must
     * serialize this operation with interrupts disabled.
     */
    handoff->frame = captured;
    handoff->owner = owner;
    handoff->pending = true;

    return true;
}

bool scheduler_kernel_handoff_release(
    struct scheduler_kernel_handoff *handoff,
    const struct process *owner)
{
    if (
        handoff == NULL ||
        owner == NULL ||
        !handoff->pending ||
        handoff->owner != owner
    ) {
        return false;
    }

    /*
     * Clearing the request releases its borrowed
     * owner reference and invalidates the saved frame.
     */
    scheduler_kernel_handoff_init(handoff);

    return true;
}

bool scheduler_kernel_handoff_complete(
    struct scheduler_kernel_handoff *handoff,
    const struct process *owner,
    struct interrupt_context *resume_frame)
{
    if (
        handoff == NULL ||
        owner == NULL ||
        resume_frame == NULL ||
        resume_frame == &handoff->frame ||
        !handoff->pending ||
        handoff->owner != owner ||
        owner->state != PROCESS_STATE_RUNNING
    ) {
        return false;
    }

    /*
     * The saved interrupt frame must remain a valid
     * Ring 3 frame throughout the handoff lifecycle.
     *
     * First capture into independent local storage.
     */
    struct interrupt_context captured;

    if (!scheduler_capture_user_interrupt(
        &handoff->frame,
        &captured
    )) {
        return false;
    }

    /*
     * Release ownership only after successful capture.
     *
     * The single-CPU caller must keep interrupts disabled
     * throughout this operation.
     */
    if (!scheduler_kernel_handoff_release(
        handoff,
        owner
    )) {
        return false;
    }

    *resume_frame = captured;

    return true;
}

bool scheduler_kernel_handoff_resume_preflight(
    const struct scheduler_kernel_handoff *handoff,
    const struct process *selected,
    const struct paging_address_space *kernel_space,
    uint64_t active_cr3,
    struct scheduler_handoff_resume_plan *plan)
{
    if (
        handoff == NULL ||
        selected == NULL ||
        kernel_space == NULL ||
        plan == NULL ||
        !handoff->pending ||
        handoff->owner != selected ||
        selected->state != PROCESS_STATE_RUNNING ||
        selected->memory == NULL
    ) {
        return false;
    }

    const struct paging_address_space *user_space =
        &selected->memory->address_space;

    /*
     * Both address spaces must have valid PML4 roots.
     * User processes must inherit the kernel higher half.
     */
    if (
        kernel_space->pml4_virtual == NULL ||
        user_space->pml4_virtual == NULL ||
        !user_space->kernel_half_shared ||
        kernel_space->pml4_physical == 0U ||
        user_space->pml4_physical == 0U ||
        (kernel_space->pml4_physical &
            ~PAGE_ADDRESS_MASK_4K) != 0U ||
        (user_space->pml4_physical &
            ~PAGE_ADDRESS_MASK_4K) != 0U ||
        kernel_space->pml4_physical ==
            user_space->pml4_physical
    ) {
        return false;
    }

    /*
     * The supervisor must be running under the kernel
     * address space before preparing the return.
     */
    if (
        (active_cr3 & PAGE_ADDRESS_MASK_4K) !=
        kernel_space->pml4_physical
    ) {
        return false;
    }

    const struct interrupt_context *frame =
        &handoff->frame;

    /*
     * Unlike the generic Ring 3 capture contract,
     * actual IRETQ resumption requires the exact
     * MyOS GDT selectors, not only RPL == 3.
     */
    if (
        frame->cs != GDT_USER_CODE_SELECTOR ||
        frame->ss != GDT_USER_DATA_SELECTOR ||
        (frame->rflags & 0x2U) == 0U ||
        !process_memory_user_range_valid(
            frame->rip,
            1U
        ) ||
        !process_memory_user_range_valid(
            frame->rsp,
            0U
        )
    ) {
        return false;
    }

    /*
     * Publish the independent resume plan last.
     *
     * The original handoff remains pending and owned.
     */
    struct scheduler_handoff_resume_plan prepared = {
        .owner = selected,
        .address_space = user_space,
        .frame = *frame,
    };

    *plan = prepared;

    return true;
}

bool scheduler_kernel_handoff_activate_resume_space(
    const struct scheduler_kernel_handoff *handoff,
    const struct process *selected,
    const struct scheduler_handoff_resume_plan *plan)
{
    if (plan == NULL) {
        return false;
    }

    uint64_t rflags;

    __asm__ volatile (
        "pushfq\n\t"
        "popq %0"
        : "=r"(rflags)
        :
        : "memory"
    );

    /*
     * CR3 must not be changed while maskable interrupts
     * can interleave with this transition.
     */
    if ((rflags & (1ULL << 9)) != 0U) {
        return false;
    }

    struct scheduler_handoff_resume_plan current = {0};

    if (!scheduler_kernel_handoff_resume_preflight(
        handoff,
        selected,
        paging_kernel_address_space(),
        paging_read_cr3(),
        &current
    )) {
        return false;
    }

    /*
     * A previously prepared plan must still describe
     * the exact current handoff.
     */
    if (
        plan->owner != current.owner ||
        plan->address_space != current.address_space
    ) {
        return false;
    }

    const unsigned char *expected =
        (const unsigned char *) &current.frame;

    const unsigned char *actual =
        (const unsigned char *) &plan->frame;

    for (size_t index = 0;
         index < sizeof(current.frame);
         ++index) {
        if (expected[index] != actual[index]) {
            return false;
        }
    }

    /*
     * All validations have succeeded.
     * This is the only operation that changes CR3.
     */
    if (!paging_address_space_activate(
        current.address_space
    )) {
        return false;
    }

    /*
     * Once CR3 is modified, a failed verification
     * cannot be reported as an ordinary rejection.
     */
    uint64_t active_cr3 =
        paging_read_cr3() & PAGE_ADDRESS_MASK_4K;

    if (
        active_cr3 !=
        current.address_space->pml4_physical
    ) {
        kernel_panic(
            "Handoff resume activated unexpected CR3"
        );
    }

    return true;
}

bool scheduler_kernel_handoff_commit_resume(
    struct scheduler_kernel_handoff *handoff,
    const struct process *selected,
    const struct scheduler_handoff_resume_plan *plan,
    struct interrupt_context *resume_frame)
{
    if (
        handoff == NULL ||
        selected == NULL ||
        plan == NULL ||
        resume_frame == NULL ||
        resume_frame == &handoff->frame ||
        resume_frame == &plan->frame ||
        !handoff->pending ||
        handoff->owner != selected ||
        selected->state != PROCESS_STATE_RUNNING ||
        selected->memory == NULL ||
        plan->owner != selected ||
        plan->address_space !=
            &selected->memory->address_space
    ) {
        return false;
    }

    uint64_t rflags;

    __asm__ volatile (
        "pushfq\n\t"
        "popq %0"
        : "=r"(rflags)
        :
        : "memory"
    );

    if ((rflags & (1ULL << 9)) != 0U) {
        return false;
    }

    const struct paging_address_space *space =
        plan->address_space;

    if (
        space->pml4_virtual == NULL ||
        !space->kernel_half_shared ||
        space->pml4_physical == 0U ||
        (space->pml4_physical &
            ~PAGE_ADDRESS_MASK_4K) != 0U ||
        (paging_read_cr3() & PAGE_ADDRESS_MASK_4K) !=
            space->pml4_physical
    ) {
        return false;
    }

    /*
     * The plan must still describe the exact frame
     * published by the original handoff.
     */
    const unsigned char *saved =
        (const unsigned char *) &handoff->frame;

    const unsigned char *planned =
        (const unsigned char *) &plan->frame;

    for (size_t index = 0;
         index < sizeof(handoff->frame);
         ++index) {
        if (saved[index] != planned[index]) {
            return false;
        }
    }

    /*
     * Every validation has succeeded.
     * Completion copies the frame and retires the request.
     */
    return scheduler_kernel_handoff_complete(
        handoff,
        selected,
        resume_frame
    );
}

bool scheduler_activate_kernel_worker_resume_space(
    const struct scheduler_handoff_resume_plan *plan)
{
    if (
        current_process == NULL ||
        plan == NULL ||
        plan->owner != current_process
    ) {
        return false;
    }

    return scheduler_kernel_handoff_activate_resume_space(
        &kernel_handoff,
        current_process,
        plan
    );
}

bool scheduler_commit_kernel_worker_resume(
    const struct scheduler_handoff_resume_plan *plan,
    struct interrupt_context *resume_frame)
{
    if (
        current_process == NULL ||
        plan == NULL ||
        plan->owner != current_process
    ) {
        return false;
    }

    return scheduler_kernel_handoff_commit_resume(
        &kernel_handoff,
        current_process,
        plan,
        resume_frame
    );
}

_Noreturn void scheduler_kernel_worker_resume_completion(
    bool dispatched,
    void *argument)
{
    /*
     * The timer selected a handoff because an eligible
     * worker existed. That worker must have run before
     * returning control to this completion.
     *
     * WAITING and TERMINATED are both valid outcomes.
     */
    if (
        !dispatched ||
        argument != NULL ||
        !scheduler_kernel_worker_handoff_pending()
    ) {
        kernel_panic(
            "Kernel handoff completion entered without dispatched work"
        );
    }

    uint64_t rflags;

    __asm__ volatile (
        "pushfq\n\t"
        "popq %0"
        : "=r"(rflags)
        :
        : "memory"
    );

    /*
     * No maskable interrupt may interleave with
     * the restoration of the userspace execution state.
     */
    if ((rflags & (1ULL << 9)) != 0U) {
        kernel_panic(
            "Kernel handoff completion enabled interrupts"
        );
    }

    /*
     * The BSP supervisor has already activated the
     * kernel CR3. Validate the selected process and
     * capture the independent resume plan.
     */
    struct scheduler_handoff_resume_plan plan = {0};

    if (!scheduler_kernel_worker_resume_preflight(
        &plan
    )) {
        kernel_panic(
            "Kernel handoff resume preflight failed"
        );
    }

    /*
     * This operation revalidates the pending handoff
     * before switching to the owner's address space.
     *
     * The request remains pending after activation.
     */
    if (!scheduler_activate_kernel_worker_resume_space(
        &plan
    )) {
        kernel_panic(
            "Kernel handoff resume CR3 activation failed"
        );
    }

    /*
     * The supervisor stack lives in the shared kernel
     * higher half, so it remains accessible under the
     * process address space.
     *
     * The saved frame must not reside on the abandoned
     * IRQ stack.
     */
    struct interrupt_context resume_frame = {0};

    if (!scheduler_commit_kernel_worker_resume(
        &plan,
        &resume_frame
    )) {
        kernel_panic(
            "Kernel handoff resume commit failed"
        );
    }

    /*
     * The request has been retired exactly once.
     * No kernel worker may still own this execution.
     */
    if (
        scheduler_kernel_worker_handoff_pending() ||
        scheduler_current() != plan.owner ||
        plan.owner->state != PROCESS_STATE_RUNNING
    ) {
        kernel_panic(
            "Kernel handoff resume ownership invariant failed"
        );
    }

    uint64_t active_cr3 =
        paging_read_cr3() & PAGE_ADDRESS_MASK_4K;

    if (
        active_cr3 !=
        plan.address_space->pml4_physical
    ) {
        kernel_panic(
            "Kernel handoff resume has incorrect process CR3"
        );
    }

    /*
     * IRETQ consumes resume_frame directly from the
     * shared supervisor stack.
     *
     * This transition does not return to the supervisor
     * or to the abandoned interrupt stack.
     */
    usermode_resume(&resume_frame);
}

bool scheduler_prepare_kernel_worker_handoff(
    const struct interrupt_context *frame)
{
    if (
        kernel_handoff.pending ||
        !scheduler_kernel_worker_handoff_needed(frame)
    ) {
        return false;
    }

    /*
     * The owner is the scheduler's current process,
     * never a process supplied by the caller.
     *
     * This operation must execute with interrupts
     * disabled on the current bootstrap BSP.
     */
    return scheduler_kernel_handoff_prepare(
        &kernel_handoff,
        current_process,
        frame
    );
}

bool scheduler_finish_kernel_worker_handoff(
    const struct process *owner,
    struct interrupt_context *resume_frame)
{
    if (
        owner == NULL ||
        current_process != owner ||
        owner->state != PROCESS_STATE_RUNNING
    ) {
        return false;
    }

    return scheduler_kernel_handoff_complete(
        &kernel_handoff,
        owner,
        resume_frame
    );
}

bool scheduler_kernel_worker_resume_preflight(
    struct scheduler_handoff_resume_plan *plan)
{
    if (
        current_process == NULL ||
        plan == NULL
    ) {
        return false;
    }

    return scheduler_kernel_handoff_resume_preflight(
        &kernel_handoff,
        current_process,
        paging_kernel_address_space(),
        paging_read_cr3(),
        plan
    );
}

bool scheduler_release_kernel_worker_handoff(
    const struct process *owner)
{
    return scheduler_kernel_handoff_release(
        &kernel_handoff,
        owner
    );
}

bool scheduler_kernel_worker_handoff_pending(void)
{
    return kernel_handoff.pending;
}

void scheduler_set_terminated_handler(
    scheduler_terminated_handler handler)
{
    terminated_handler = handler;
}

void scheduler_set_idle_work_handler(
    scheduler_idle_work_handler handler)
{
    idle_work_handler = handler;
}

static bool scheduler_dispatch_idle_work(void)
{
    if (idle_work_handler == NULL) {
        return false;
    }

    return idle_work_handler();
}

static bool scheduler_dispatch_idle_kernel_worker(void)
{
    /*
     * Nothing is dispatched unless the registered
     * worker is executable and has no CPU owner.
     */
    if (!scheduler_kernel_worker_ready()) {
        return false;
    }

    /*
     * Idle already owns the kernel address space
     * and executes with interrupts disabled.
     *
     * Unlike the Ring 3 timer handoff, this call
     * must return to the original idle stack.
     */
    if (!kernel_supervisor_bsp_dispatch(
        scheduler_dispatch_kernel_worker
    )) {
        kernel_panic(
            "Unable to dispatch idle kernel worker"
        );
    }

    return true;
}

#if MYOS_KERNEL_TESTS
bool scheduler_test_dispatch_idle_kernel_worker(void)
{
    return scheduler_dispatch_idle_kernel_worker();
}
#endif

#if MYOS_KERNEL_TESTS
bool scheduler_test_dispatch_idle_work(void)
{
    return scheduler_dispatch_idle_work();
}
#endif

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

bool scheduler_load_current_context(
    struct interrupt_context *context)
{
    if (context == NULL) return false;
    if (current_process == NULL) return false;

    if (
        current_process->state !=
        PROCESS_STATE_RUNNING
    ) {
        return false;
    }

    if (current_process->memory == NULL) {
        return false;
    }

    uint64_t active_cr3 =
        paging_read_cr3() &
        PAGE_ADDRESS_MASK_4K;

    uint64_t expected_cr3 =
        current_process
            ->memory
            ->address_space
            .pml4_physical &
        PAGE_ADDRESS_MASK_4K;

    if (
        expected_cr3 == 0 ||
        active_cr3 != expected_cr3
    ) {
        return false;
    }

    scheduler_load_context(
        context,
        current_process
    );

    return true;
}

#if MYOS_KERNEL_TESTS
size_t scheduler_test_process_count(void)
{
    return process_count;
}

uint64_t scheduler_test_preemption_count(void)
{
    return scheduler_test_preemption_count_value;
}

uint64_t scheduler_test_cross_address_space_preemption_count(void)
{
    return
        scheduler_test_cross_address_space_preemption_count_value;
}
#endif

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
    /*
     * The BSP supervisor must exist before the first
     * transition into userspace.
     *
     * Kernel tests may have initialized it earlier.
     */
    if (!kernel_supervisor_bsp_initialize()) {
        kernel_panic(
            "Unable to initialize BSP kernel supervisor"
        );
    }

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

        /*
         * Run a READY kernel worker even when no userspace
         * process can currently own the CPU.
         *
         * The supervisor returns to this idle stack after
         * the worker suspends or terminates.
         */
        if (scheduler_dispatch_idle_kernel_worker()) {
            /*
             * The worker may have changed process readiness,
             * or may have been rearmed by pending work.
             *
             * Reevaluate scheduling before entering HLT.
             */
            continue;
        }

        /*
         * Dispatch bounded kernel work before entering HLT.
         *
         * Interrupts remain disabled while checking for work.
         * A producer running on this BSP cannot interleave
         * between this check and interrupts_wait().
         */
        if (scheduler_dispatch_idle_work()) {
            /*
             * Work may have changed process readiness or
             * generated another bounded work request.
             * Reevaluate before sleeping.
             */
            continue;
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

    process_lifecycle_notify_terminated(
        process
    );

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

bool scheduler_block_current(
    struct interrupt_context *context)
{
    return scheduler_block_current_internal(
        context,
        false
    );
}

bool scheduler_block_current_preserve_context(
    struct interrupt_context *context)
{
    return scheduler_block_current_internal(
        context,
        true
    );
}

static bool scheduler_block_current_internal(
    struct interrupt_context *context,
    bool preserve_context)
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

    if (!preserve_context) {
        blocked_process->context.rax = 0;
    }

    blocked_process->state =
        PROCESS_STATE_BLOCKED;

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

    /*
     * A pending supervisor handoff owns the interrupted
     * userspace context.
     *
     * Ordinary preemption must never replace its owner.
     */
    if (kernel_handoff.pending) {
        kernel_panic(
            "Scheduler preemption during pending kernel handoff"
        );
    }

    struct process *preempted_process = current_process;

#if MYOS_KERNEL_TESTS
    if (preempted_process->memory == NULL) {
        kernel_panic(
            "Preempted process has no address space"
        );
    }

    uint64_t active_cr3 =
        paging_read_cr3() &
        PAGE_ADDRESS_MASK_4K;

    uint64_t expected_cr3 =
        preempted_process
            ->memory
            ->address_space
            .pml4_physical &
        PAGE_ADDRESS_MASK_4K;

    if (active_cr3 != expected_cr3) {
        kernel_panic(
            "Scheduler preemption observed wrong active CR3"
        );
    }
#endif

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

#if MYOS_KERNEL_TESTS
    ++scheduler_test_preemption_count_value;

    if (next->memory == NULL) {
        kernel_panic(
            "Preemption selected process without address space"
        );
    }

    uint64_t next_cr3 =
        next->memory
            ->address_space
            .pml4_physical &
        PAGE_ADDRESS_MASK_4K;

    if (next_cr3 != expected_cr3) {
        ++scheduler_test_cross_address_space_preemption_count_value;
    }
#endif

    scheduler_switch_from_interrupt(
        context,
        next
    );
}

enum scheduler_timer_decision scheduler_timer_decide(
    bool user_running,
    bool worker_ready,
    bool handoff_pending,
    uint64_t elapsed_ticks,
    uint64_t quantum_ticks)
{
    if (!user_running || quantum_ticks == 0U) {
        return SCHEDULER_TIMER_CONTINUE;
    }

    /*
     * An outstanding handoff owns the interrupted
     * process context.
     *
     * No new handoff or ordinary preemption may
     * be selected until that request is resolved.
     */
    if (handoff_pending) {
        return SCHEDULER_TIMER_CONTINUE;
    }

    if (worker_ready) {
        return SCHEDULER_TIMER_HANDOFF;
    }

    if (elapsed_ticks >= quantum_ticks) {
        return SCHEDULER_TIMER_PREEMPT;
    }

    return SCHEDULER_TIMER_CONTINUE;
}

enum scheduler_timer_decision scheduler_timer_decision_for(
    const struct interrupt_context *context)
{
    if (
        context == NULL ||
        (context->cs & 0x3U) != 3U ||
        (context->ss & 0x3U) != 3U ||
        current_process == NULL ||
        current_process->state != PROCESS_STATE_RUNNING
    ) {
        return SCHEDULER_TIMER_CONTINUE;
    }

    return scheduler_timer_decide(
        true,
        scheduler_kernel_worker_ready(),
        kernel_handoff.pending,
        current_quantum_ticks + 1U,
        SCHEDULER_QUANTUM_TICKS
    );
}

void scheduler_tick(struct interrupt_context *context)
{
    if (
        context == NULL ||
        current_process == NULL ||
        current_process->state != PROCESS_STATE_RUNNING
    ) {
        return;
    }

    /*
     * The pending handoff owns the interrupted user
     * context. Returning through the ordinary timer
     * path while it remains pending is forbidden.
     */
    if (kernel_handoff.pending) {
        kernel_panic(
            "Timer reached pending kernel handoff"
        );
    }

    enum scheduler_timer_decision decision =
        scheduler_timer_decision_for(context);

    /*
     * Account for exactly one interrupted user tick.
     */
    ++current_quantum_ticks;

    if (decision == SCHEDULER_TIMER_CONTINUE) {
        return;
    }

    if (decision == SCHEDULER_TIMER_HANDOFF) {
        /*
         * timer_handle_interrupt() has already sent
         * the LAPIC EOI.
         *
         * Capture the entire Ring 3 interrupt frame
         * before abandoning the original IRQ stack.
         */
        if (!scheduler_prepare_kernel_worker_handoff(
            context
        )) {
            kernel_panic(
                "Unable to prepare timer kernel handoff"
            );
        }

        /*
         * This entry never returns to scheduler_tick().
         *
         * The BSP supervisor dispatches the worker,
         * restores the owning process address space,
         * commits the handoff and resumes Ring 3.
         */
        kernel_supervisor_bsp_enter(
            scheduler_dispatch_kernel_worker,
            scheduler_kernel_worker_resume_completion,
            NULL
        );
    }

    if (decision != SCHEDULER_TIMER_PREEMPT) {
        kernel_panic(
            "Scheduler received invalid timer decision"
        );
    }

    /*
     * Ordinary quantum preemption continues using
     * the existing interrupt-return path.
     */
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

#if MYOS_KERNEL_TESTS
    uint64_t active_cr3 =
        paging_read_cr3() &
        PAGE_ADDRESS_MASK_4K;

    uint64_t expected_cr3 =
        next->memory
            ->address_space
            .pml4_physical &
        PAGE_ADDRESS_MASK_4K;

    if (active_cr3 != expected_cr3) {
        kernel_panic(
            "Scheduler switch activated wrong CR3"
        );
    }
#endif

    scheduler_load_context(context, next);

    current_quantum_ticks = 0;

    current_process = next;
    next->state = PROCESS_STATE_RUNNING;

    diagnostics_printf(
        "[scheduler] Running PID %u\n",
        next->id
    );
}
