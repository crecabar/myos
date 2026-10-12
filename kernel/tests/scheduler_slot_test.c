// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file scheduler_slot_test.c
 * @brief Scheduler registration-slot recycling regression tests.
 */

#include "scheduler_slot_test.h"

#include "../arch/x86_64/cpu_identity.h"
#include "../arch/x86_64/gdt.h"
#include "../arch/x86_64/interrupts.h"
#include "../arch/x86_64/kernel_switch.h"
#include "../arch/x86_64/timer.h"
#include "../core/cpu/cpu_local.h"
#include "../core/execution/kernel_supervisor.h"
#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../process/process.h"
#include "../scheduler/scheduler.h"

#include <stddef.h>
#include <stdint.h>

#define SCHEDULER_SLOT_TEST_PROCESS_COUNT \
    (SCHEDULER_MAX_PROCESSES + 1)

#define SCHEDULER_SLOT_TEST_RECYCLED_INDEX 3

#define SCHEDULER_SLOT_TEST_ENTRY_BASE \
    0x0000000000400000ULL

#define SCHEDULER_SLOT_TEST_STACK_TOP \
    0x0000000070000000ULL

#define SCHEDULER_SLOT_TEST_OLD_RAX \
    0x1111222233334444ULL

#define SCHEDULER_SLOT_TEST_OLD_RBX \
    0x5555666677778888ULL

#define SCHEDULER_SLOT_TEST_NEW_RAX \
    0xAAAABBBBCCCCDDDDULL

#define SCHEDULER_SLOT_TEST_NEW_RBX \
    0xEEEEFFFF11112222ULL

#define SCHEDULER_WORKER_TEST_STACK_SIZE 8192U

static _Alignas(KERNEL_CONTEXT_STACK_ALIGNMENT)
unsigned char scheduler_worker_test_stack[
    SCHEDULER_WORKER_TEST_STACK_SIZE
];

#define SCHEDULER_SUPERVISOR_TEST_STACK_SIZE 8192U

static _Alignas(16)
unsigned char scheduler_supervisor_test_stack[
    SCHEDULER_SUPERVISOR_TEST_STACK_SIZE
];

static _Alignas(16)
unsigned char scheduler_nested_worker_stack[
    SCHEDULER_WORKER_TEST_STACK_SIZE
];

static _Alignas(KERNEL_CONTEXT_STACK_ALIGNMENT)
unsigned char scheduler_bsp_worker_stack[
    SCHEDULER_WORKER_TEST_STACK_SIZE
];

struct scheduler_nested_test_probe {
    struct cpu_local *cpu;
    struct kernel_context *worker;

    uintptr_t supervisor_bottom;
    uintptr_t supervisor_top;
    uintptr_t worker_bottom;
    uintptr_t worker_top;

    unsigned int supervisor_calls;
    unsigned int worker_entries;
    unsigned int stage;
    bool dispatch_succeeded;
};

struct scheduler_worker_test_probe {
    struct kernel_context *worker;
    unsigned int entries;
    unsigned int stage;
};

#define SCHEDULER_ONE_WAY_ENTRY_STACK_SIZE 8192U

static _Alignas(16)
unsigned char scheduler_one_way_entry_stack[
    SCHEDULER_ONE_WAY_ENTRY_STACK_SIZE
];

static _Alignas(KERNEL_CONTEXT_STACK_ALIGNMENT)
unsigned char scheduler_one_way_worker_stack[
    SCHEDULER_WORKER_TEST_STACK_SIZE
];

struct scheduler_one_way_test_probe {
    struct kernel_switch_context caller;
    struct kernel_switch_context entry;
    struct kernel_switch_context completion_context;

    struct kernel_context worker;
    struct scheduler_worker_test_probe worker_probe;
    struct process_memory memory;

    uint64_t kernel_cr3;

    unsigned int completions;
    bool enter_from_user_cr3;
    bool last_dispatched;
};

static struct scheduler_one_way_test_probe
    scheduler_one_way_probe;

static void scheduler_worker_test_entry(
    void *argument)
{
    struct scheduler_worker_test_probe *probe =
        argument;

    if (
        probe == NULL ||
        probe->worker == NULL ||
        probe->stage != 0U
    ) {
        kernel_panic(
            "Scheduler worker test entry invalid"
        );
    }

    ++probe->entries;
    probe->stage = 1U;

    if (!kernel_context_yield_wait(probe->worker)) {
        kernel_panic(
            "Scheduler worker failed to yield"
        );
    }

    if (
        probe->stage != 2U ||
        probe->entries != 1U
    ) {
        kernel_panic(
            "Scheduler worker did not resume"
        );
    }

    probe->stage = 3U;
}

static struct process_memory
    test_memories[SCHEDULER_SLOT_TEST_PROCESS_COUNT];

static struct process_layout
    test_layouts[SCHEDULER_SLOT_TEST_PROCESS_COUNT];

static struct process
    test_processes[SCHEDULER_SLOT_TEST_PROCESS_COUNT];

static unsigned int idle_work_test_calls;

static bool scheduler_slot_test_idle_work(void)
{
    uint64_t rflags;

    __asm__ volatile (
        "pushfq\n\t"
        "popq %0"
        : "=r"(rflags)
        :
        : "memory"
    );

    if ((rflags & (1ULL << 9)) != 0) {
        kernel_panic(
            "Scheduler idle work ran with interrupts enabled"
        );
    }

    ++idle_work_test_calls;

    return idle_work_test_calls == 1U;
}

static void scheduler_slot_test_initialize_processes(void);
static void scheduler_slot_test_kernel_worker_dispatch(void);

static uintptr_t scheduler_nested_test_rsp(void)
{
    uintptr_t rsp;

    __asm__ volatile (
        "movq %%rsp, %0"
        : "=r"(rsp)
        :
        : "memory"
    );

    return rsp;
}

static void scheduler_nested_worker_entry(void *argument)
{
    struct scheduler_nested_test_probe *probe = argument;

    if (
        probe == NULL ||
        probe->worker == NULL ||
        probe->stage != 0U
    ) {
        kernel_panic("Nested worker entry invalid");
    }

    uintptr_t rsp = scheduler_nested_test_rsp();

    if (
        rsp < probe->worker_bottom ||
        rsp >= probe->worker_top ||
        (rsp >= probe->supervisor_bottom &&
         rsp < probe->supervisor_top)
    ) {
        kernel_panic("Nested worker used incorrect stack");
    }

    ++probe->worker_entries;
    probe->stage = 1U;

    if (!kernel_context_yield_wait(probe->worker)) {
        kernel_panic("Nested worker yield failed");
    }

    /*
     * Continuation after the second supervisor dispatch.
     */
    if (
        probe->stage != 2U ||
        probe->worker_entries != 1U
    ) {
        kernel_panic("Nested worker continuation invalid");
    }

    probe->stage = 3U;
}

static void scheduler_nested_supervisor_entry(void *argument)
{
    struct scheduler_nested_test_probe *probe = argument;

    if (
        probe == NULL ||
        probe->cpu == NULL ||
        probe->worker == NULL
    ) {
        kernel_panic("Nested supervisor arguments invalid");
    }

    uintptr_t rsp = scheduler_nested_test_rsp();

    if (
        rsp < probe->supervisor_bottom ||
        rsp >= probe->supervisor_top ||
        (rsp >= probe->worker_bottom &&
         rsp < probe->worker_top)
    ) {
        kernel_panic("Nested supervisor used incorrect stack");
    }

    uint64_t rflags;

    __asm__ volatile (
        "pushfq\n\t"
        "popq %0"
        : "=r"(rflags)
        :
        : "memory"
    );

    if ((rflags & (1ULL << 9)) != 0) {
        kernel_panic("Nested supervisor enabled interrupts");
    }

    ++probe->supervisor_calls;

    probe->dispatch_succeeded =
        scheduler_dispatch_kernel_worker(probe->cpu);
}

static void scheduler_slot_test_nested_supervisor(void)
{
    struct cpu_local cpu = {0};
    struct kernel_context worker;
    struct kernel_switch_context supervisor = {0};

    struct scheduler_nested_test_probe probe = {
        .cpu = &cpu,
        .worker = &worker,
        .supervisor_bottom =
            (uintptr_t) scheduler_supervisor_test_stack,
        .supervisor_top =
            (uintptr_t) scheduler_supervisor_test_stack +
            sizeof(scheduler_supervisor_test_stack),
        .worker_bottom =
            (uintptr_t) scheduler_nested_worker_stack,
        .worker_top =
            (uintptr_t) scheduler_nested_worker_stack +
            sizeof(scheduler_nested_worker_stack),
    };

    uint32_t apic_id;

    if (
        !arch_cpu_apic_id(&apic_id) ||
        !cpu_local_init(&cpu, 0U, apic_id) ||
        !kernel_context_init(
            &worker,
            scheduler_nested_worker_stack,
            sizeof(scheduler_nested_worker_stack),
            scheduler_nested_worker_entry,
            &probe
        ) ||
        !kernel_context_prepare(
            &worker,
            kernel_context_bootstrap
        ) ||
        !kernel_context_bind_supervisor(
            &worker,
            &supervisor
        ) ||
        !scheduler_register_kernel_worker(&worker)
    ) {
        kernel_panic("Nested supervisor setup failed");
    }

    /*
     * First invocation:
     * caller -> supervisor stack -> worker stack ->
     * supervisor stack -> caller.
     */
    uintptr_t caller_rsp_before =
        scheduler_nested_test_rsp();

    interrupts_disable();

    kernel_supervisor_stack_call(
        probe.supervisor_top,
        scheduler_nested_supervisor_entry,
        &probe
    );

    uintptr_t caller_rsp_after =
        scheduler_nested_test_rsp();

    if (
        caller_rsp_after != caller_rsp_before ||
        !probe.dispatch_succeeded ||
        probe.supervisor_calls != 1U ||
        probe.worker_entries != 1U ||
        probe.stage != 1U ||
        worker.state != KERNEL_CONTEXT_WAITING ||
        kernel_context_owner(&worker) !=
            KERNEL_CONTEXT_NO_CPU ||
        cpu_local_active_kernel_context(&cpu) != NULL
    ) {
        kernel_panic("Nested supervisor first dispatch failed");
    }

    probe.stage = 2U;
    probe.dispatch_succeeded = false;

    if (!kernel_context_wake(&worker)) {
        kernel_panic("Nested worker wake failed");
    }

    /*
     * Second invocation resumes the saved worker continuation.
     */
    kernel_supervisor_stack_call(
        probe.supervisor_top,
        scheduler_nested_supervisor_entry,
        &probe
    );

    if (
        scheduler_nested_test_rsp() != caller_rsp_before ||
        !probe.dispatch_succeeded ||
        probe.supervisor_calls != 2U ||
        probe.worker_entries != 1U ||
        probe.stage != 3U ||
        worker.state != KERNEL_CONTEXT_TERMINATED ||
        kernel_context_owner(&worker) !=
            KERNEL_CONTEXT_NO_CPU ||
        cpu_local_active_kernel_context(&cpu) != NULL ||
        !scheduler_unregister_kernel_worker(&worker)
    ) {
        kernel_panic("Nested supervisor resume failed");
    }

    interrupts_enable();
}

static bool scheduler_slot_test_frames_equal(
    const struct interrupt_context *left,
    const struct interrupt_context *right)
{
    const unsigned char *a =
        (const unsigned char *) left;

    const unsigned char *b =
        (const unsigned char *) right;

    for (size_t index = 0;
         index < sizeof(*left);
         ++index) {
        if (a[index] != b[index]) {
            return false;
        }
    }

    return true;
}

static void scheduler_slot_test_handoff_capture(void)
{
    struct interrupt_context original = {
        .r15 = 0x1111222233334444ULL,
        .r12 = 0x5555666677778888ULL,
        .rax = 0xAAAABBBBCCCCDDDDULL,
        .rbx = 0xEEEEFFFF11112222ULL,
        .vector = 0x20U,
        .error_code = 0U,
        .rip = 0x400000ULL,
        .cs = 0x1BU,
        .rflags = 0x202ULL,
        .rsp = 0x70000000ULL,
        .ss = 0x23U,
    };

    struct interrupt_context captured = {0};

    if (!scheduler_capture_user_interrupt(
        &original,
        &captured
    )) {
        kernel_panic(
            "Scheduler rejected valid Ring 3 frame"
        );
    }

    if (!scheduler_slot_test_frames_equal(
        &original,
        &captured
    )) {
        kernel_panic(
            "Scheduler corrupted captured user frame"
        );
    }

    /*
     * Prove that the capture does not borrow storage
     * from the original interrupt frame.
     */
    original.rax = 0;
    original.rip = 0;

    if (
        captured.rax !=
            0xAAAABBBBCCCCDDDDULL ||
        captured.rip != 0x400000ULL
    ) {
        kernel_panic(
            "Scheduler retained borrowed interrupt frame"
        );
    }

    struct interrupt_context before = captured;

    struct interrupt_context kernel_frame = {
        .cs = 0x08U,
        .ss = 0x10U,
    };

    if (
        scheduler_capture_user_interrupt(
            &kernel_frame,
            &captured
        ) ||
        scheduler_capture_user_interrupt(
            NULL,
            &captured
        ) ||
        scheduler_capture_user_interrupt(
            &original,
            NULL
        ) ||
        scheduler_capture_user_interrupt(
            &original,
            &original
        )
    ) {
        kernel_panic(
            "Scheduler accepted invalid handoff capture"
        );
    }

    if (!scheduler_slot_test_frames_equal(
        &before,
        &captured
    )) {
        kernel_panic(
            "Rejected capture modified destination"
        );
    }
}

static void scheduler_slot_test_handoff_ownership(void)
{
    struct scheduler_kernel_handoff handoff;
    scheduler_kernel_handoff_init(&handoff);

    struct process owner = {
        .id = 100U,
        .state = PROCESS_STATE_RUNNING,
    };

    struct process other = {
        .id = 101U,
        .state = PROCESS_STATE_RUNNING,
    };

    struct interrupt_context frame = {
        .vector = 0x20U,
        .rip = 0x400000ULL,
        .cs = 0x1BU,
        .rflags = 0x202ULL,
        .rsp = 0x70000000ULL,
        .ss = 0x23U,
        .rax = 0x1122334455667788ULL,
    };

    if (
        handoff.pending ||
        handoff.owner != NULL
    ) {
        kernel_panic(
            "New kernel handoff not empty"
        );
    }

    /*
     * A valid RUNNING process can publish one request.
     */
    if (!scheduler_kernel_handoff_prepare(
        &handoff,
        &owner,
        &frame
    )) {
        kernel_panic(
            "Scheduler rejected valid handoff"
        );
    }

    if (
        !handoff.pending ||
        handoff.owner != &owner ||
        !scheduler_slot_test_frames_equal(
            &handoff.frame,
            &frame
        )
    ) {
        kernel_panic(
            "Scheduler corrupted pending handoff"
        );
    }

    /*
     * Changing the source after publication must not
     * modify the stored frame.
     */
    frame.rax = 0U;

    if (
        handoff.frame.rax !=
        0x1122334455667788ULL
    ) {
        kernel_panic(
            "Pending handoff borrowed IRQ frame"
        );
    }

    /*
     * A second process cannot overwrite an existing request.
     */
    if (scheduler_kernel_handoff_prepare(
        &handoff,
        &other,
        &frame
    )) {
        kernel_panic(
            "Scheduler overwrote pending handoff"
        );
    }

    if (
        handoff.owner != &owner ||
        handoff.frame.rax !=
            0x1122334455667788ULL
    ) {
        kernel_panic(
            "Rejected handoff changed ownership"
        );
    }

    /*
     * Only the owning process can release the request.
     */
    if (scheduler_kernel_handoff_release(
        &handoff,
        &other
    )) {
        kernel_panic(
            "Non-owner released kernel handoff"
        );
    }

    if (
        !handoff.pending ||
        handoff.owner != &owner
    ) {
        kernel_panic(
            "Rejected release modified handoff"
        );
    }

    if (!scheduler_kernel_handoff_release(
        &handoff,
        &owner
    )) {
        kernel_panic(
            "Owner could not release handoff"
        );
    }

    if (
        handoff.pending ||
        handoff.owner != NULL
    ) {
        kernel_panic(
            "Released handoff retained ownership"
        );
    }

    if (scheduler_kernel_handoff_release(
        &handoff,
        &owner
    )) {
        kernel_panic(
            "Scheduler accepted duplicate release"
        );
    }

    /*
     * A READY process cannot prepare a new request.
     */
    owner.state = PROCESS_STATE_READY;

    if (scheduler_kernel_handoff_prepare(
        &handoff,
        &owner,
        &frame
    )) {
        kernel_panic(
            "READY process prepared kernel handoff"
        );
    }

    /*
     * A malformed kernel frame must not publish a request.
     */
    owner.state = PROCESS_STATE_RUNNING;
    frame.cs = 0x08U;

    if (scheduler_kernel_handoff_prepare(
        &handoff,
        &owner,
        &frame
    )) {
        kernel_panic(
            "Kernel frame prepared user handoff"
        );
    }

    if (
        handoff.pending ||
        handoff.owner != NULL
    ) {
        kernel_panic(
            "Rejected preparation left pending handoff"
        );
    }

    diagnostics_write(
        "[scheduler] Kernel handoff ownership tests passed\n"
    );
}

static void scheduler_slot_test_handoff_completion(void)
{
    struct scheduler_kernel_handoff handoff;

    scheduler_kernel_handoff_init(&handoff);

    struct process owner = {
        .id = 200U,
        .state = PROCESS_STATE_RUNNING,
    };

    struct process other = {
        .id = 201U,
        .state = PROCESS_STATE_RUNNING,
    };

    struct interrupt_context original = {
        .vector = 0x20U,
        .error_code = 0U,
        .rip = 0x400000ULL,
        .cs = 0x1BU,
        .rflags = 0x202ULL,
        .rsp = 0x70000000ULL,
        .ss = 0x23U,
        .rax = 0x1122334455667788ULL,
        .r12 = 0x8877665544332211ULL,
    };

    struct interrupt_context source = original;

    struct interrupt_context resumed = {
        .rax = 0xDEADBEEFULL,
    };

    struct interrupt_context sentinel = resumed;

    /*
     * Completion without a pending request is invalid.
     */
    if (scheduler_kernel_handoff_complete(
        &handoff,
        &owner,
        &resumed
    )) {
        kernel_panic(
            "Scheduler completed absent handoff"
        );
    }

    if (!scheduler_kernel_handoff_prepare(
        &handoff,
        &owner,
        &source
    )) {
        kernel_panic(
            "Scheduler handoff completion setup failed"
        );
    }

    /*
     * The source IRQ frame must no longer be required.
     */
    source.rax = 0U;
    source.rip = 0U;

    /*
     * Reject another owner, invalid destinations and
     * attempts to overwrite the owned frame itself.
     */
    if (
        scheduler_kernel_handoff_complete(
            &handoff,
            &other,
            &resumed
        ) ||
        scheduler_kernel_handoff_complete(
            &handoff,
            &owner,
            NULL
        ) ||
        scheduler_kernel_handoff_complete(
            &handoff,
            &owner,
            &handoff.frame
        )
    ) {
        kernel_panic(
            "Scheduler accepted invalid handoff completion"
        );
    }

    /*
     * A process that stopped being RUNNING cannot
     * complete the request.
     */
    owner.state = PROCESS_STATE_READY;

    if (scheduler_kernel_handoff_complete(
        &handoff,
        &owner,
        &resumed
    )) {
        kernel_panic(
            "Scheduler completed handoff for READY owner"
        );
    }

    owner.state = PROCESS_STATE_RUNNING;

    if (
        !handoff.pending ||
        handoff.owner != &owner ||
        !scheduler_slot_test_frames_equal(
            &handoff.frame,
            &original
        ) ||
        !scheduler_slot_test_frames_equal(
            &resumed,
            &sentinel
        )
    ) {
        kernel_panic(
            "Rejected completion mutated handoff state"
        );
    }

    /*
     * Only the original owner can extract the frame
     * and retire the request.
     */
    if (!scheduler_kernel_handoff_complete(
        &handoff,
        &owner,
        &resumed
    )) {
        kernel_panic(
            "Scheduler could not complete owned handoff"
        );
    }

    if (
        handoff.pending ||
        handoff.owner != NULL ||
        !scheduler_slot_test_frames_equal(
            &resumed,
            &original
        )
    ) {
        kernel_panic(
            "Scheduler handoff completion lost user frame"
        );
    }

    /*
     * Completion is one-shot.
     */
    if (scheduler_kernel_handoff_complete(
        &handoff,
        &owner,
        &resumed
    )) {
        kernel_panic(
            "Scheduler accepted duplicate handoff completion"
        );
    }

    /*
     * The scheduler-owned wrapper must reject a request
     * when there is no current process.
     */
    if (scheduler_finish_kernel_worker_handoff(
        &owner,
        &resumed
    )) {
        kernel_panic(
            "Scheduler completed handoff without current process"
        );
    }

    diagnostics_write(
        "[scheduler] Kernel handoff completion tests passed\n"
    );
}

static void scheduler_slot_test_handoff_resume_preflight(void)
{
    /*
     * Synthetic page-table roots are used only as
     * metadata. They must never be activated as CR3.
     */
    uint64_t kernel_root = 0U;
    uint64_t user_root = 0U;

    struct paging_address_space kernel_space = {
        .pml4_physical = 0x1000U,
        .pml4_virtual = &kernel_root,
    };

    struct process_memory memory = {
        .address_space = {
            .pml4_physical = 0x2000U,
            .pml4_virtual = &user_root,
            .kernel_half_shared = true,
        },
    };

    struct process owner = {
        .id = 300U,
        .state = PROCESS_STATE_RUNNING,
        .memory = &memory,
    };

    struct process other = {
        .id = 301U,
        .state = PROCESS_STATE_RUNNING,
        .memory = &memory,
    };

    struct interrupt_context frame = {
        .vector = 0x20U,
        .rip = 0x400000ULL,
        .cs = GDT_USER_CODE_SELECTOR,
        .rflags = 0x202ULL,
        .rsp = 0x70000000ULL,
        .ss = GDT_USER_DATA_SELECTOR,
        .rax = 0x1122334455667788ULL,
    };

    struct scheduler_kernel_handoff handoff;
    scheduler_kernel_handoff_init(&handoff);

    struct scheduler_handoff_resume_plan plan = {
        .owner = &other,
        .address_space = &kernel_space,
        .frame = {
            .rax = 0xDEADBEEFULL,
        },
    };

    /*
     * No request, no plan.
     */
    if (scheduler_kernel_handoff_resume_preflight(
        &handoff,
        &owner,
        &kernel_space,
        0x1000U,
        &plan
    )) {
        kernel_panic(
            "Resume preflight accepted absent handoff"
        );
    }

    if (!scheduler_kernel_handoff_prepare(
        &handoff,
        &owner,
        &frame
    )) {
        kernel_panic(
            "Resume preflight setup failed"
        );
    }

    /*
     * A valid request produces a snapshot without
     * releasing the original.
     */
    if (!scheduler_kernel_handoff_resume_preflight(
        &handoff,
        &owner,
        &kernel_space,
        0x1000U,
        &plan
    )) {
        kernel_panic(
            "Resume preflight rejected valid owner"
        );
    }

    if (
        plan.owner != &owner ||
        plan.address_space !=
            &memory.address_space ||
        !scheduler_slot_test_frames_equal(
            &plan.frame,
            &frame
        ) ||
        !handoff.pending ||
        handoff.owner != &owner
    ) {
        kernel_panic(
            "Resume preflight corrupted ownership or frame"
        );
    }

    /*
     * The pending frame and the prepared snapshot
     * must be independent.
     */
    frame.rax = 0U;

    if (
        plan.frame.rax !=
            0x1122334455667788ULL ||
        handoff.frame.rax !=
            0x1122334455667788ULL
    ) {
        kernel_panic(
            "Resume preflight retained borrowed IRQ frame"
        );
    }

    /*
     * Neither another owner nor a wrong active CR3
     * may produce a new plan.
     */
    if (
        scheduler_kernel_handoff_resume_preflight(
            &handoff,
            &other,
            &kernel_space,
            0x1000U,
            &plan
        ) ||
        scheduler_kernel_handoff_resume_preflight(
            &handoff,
            &owner,
            &kernel_space,
            0x2000U,
            &plan
        )
    ) {
        kernel_panic(
            "Resume preflight accepted invalid CPU ownership"
        );
    }

    owner.state = PROCESS_STATE_READY;

    if (scheduler_kernel_handoff_resume_preflight(
        &handoff,
        &owner,
        &kernel_space,
        0x1000U,
        &plan
    )) {
        kernel_panic(
            "Resume preflight accepted non-running owner"
        );
    }

    owner.state = PROCESS_STATE_RUNNING;

    /*
     * A private user PML4 must remain distinct from
     * the kernel root and share the kernel higher half.
     */
    memory.address_space.kernel_half_shared = false;

    if (scheduler_kernel_handoff_resume_preflight(
        &handoff,
        &owner,
        &kernel_space,
        0x1000U,
        &plan
    )) {
        kernel_panic(
            "Resume preflight accepted non-shared kernel mapping"
        );
    }

    memory.address_space.kernel_half_shared = true;
    memory.address_space.pml4_physical = 0x2001U;

    if (scheduler_kernel_handoff_resume_preflight(
        &handoff,
        &owner,
        &kernel_space,
        0x1000U,
        &plan
    )) {
        kernel_panic(
            "Resume preflight accepted unaligned PML4"
        );
    }

    memory.address_space.pml4_physical = 0x2000U;

    /*
     * Reject invalid privilege selectors.
     */
    handoff.frame.cs = GDT_KERNEL_CODE_SELECTOR;

    if (scheduler_kernel_handoff_resume_preflight(
        &handoff,
        &owner,
        &kernel_space,
        0x1000U,
        &plan
    )) {
        kernel_panic(
            "Resume preflight accepted kernel CS"
        );
    }

    handoff.frame.cs = GDT_USER_CODE_SELECTOR;

    /*
     * A kernel address is not a valid user RIP.
     */
    handoff.frame.rip =
        0xFFFF800000000000ULL;

    if (scheduler_kernel_handoff_resume_preflight(
        &handoff,
        &owner,
        &kernel_space,
        0x1000U,
        &plan
    )) {
        kernel_panic(
            "Resume preflight accepted kernel RIP"
        );
    }

    handoff.frame.rip = 0x400000ULL;

    /*
     * Failed attempts must not change the previously
     * prepared plan or release the original request.
     */
    if (
        plan.owner != &owner ||
        plan.address_space !=
            &memory.address_space ||
        plan.frame.rax !=
            0x1122334455667788ULL ||
        !handoff.pending ||
        handoff.owner != &owner
    ) {
        kernel_panic(
            "Rejected resume preflight mutated state"
        );
    }

    /*
     * The scheduler-owned wrapper has no current
     * RUNNING process during this regression.
     */
    if (scheduler_kernel_worker_resume_preflight(
        &plan
    )) {
        kernel_panic(
            "Resume preflight ignored scheduler selection"
        );
    }

    if (!scheduler_kernel_handoff_release(
        &handoff,
        &owner
    )) {
        kernel_panic(
            "Resume preflight cleanup failed"
        );
    }

    diagnostics_write(
        "[scheduler] Kernel handoff resume preflight tests passed\n"
    );
}

static void scheduler_slot_test_handoff_resume_commit(void)
{
    struct paging_address_space *kernel_space =
        paging_kernel_address_space();

    if (kernel_space == NULL) {
        kernel_panic("Resume commit missing kernel space");
    }

    uint64_t kernel_cr3 =
        paging_read_cr3() & PAGE_ADDRESS_MASK_4K;

    if (kernel_cr3 != kernel_space->pml4_physical) {
        kernel_panic("Resume commit wrong initial CR3");
    }

    struct process_memory memory = {0};

    if (!process_memory_create(&memory)) {
        kernel_panic("Resume commit memory setup failed");
    }

    struct process owner = {
        .id = 500U,
        .state = PROCESS_STATE_RUNNING,
        .memory = &memory,
    };

    struct process other = {
        .id = 501U,
        .state = PROCESS_STATE_RUNNING,
        .memory = &memory,
    };

    struct interrupt_context frame = {
        .vector = 0x20U,
        .rip = 0x400000ULL,
        .cs = GDT_USER_CODE_SELECTOR,
        .rflags = 0x202ULL,
        .rsp = 0x70000000ULL,
        .ss = GDT_USER_DATA_SELECTOR,
        .rax = 0x1122334455667788ULL,
        .r12 = 0x8877665544332211ULL,
    };

    struct scheduler_kernel_handoff handoff;
    scheduler_kernel_handoff_init(&handoff);

    uint64_t original_rflags;

    __asm__ volatile (
        "pushfq\n\t"
        "popq %0"
        : "=r"(original_rflags)
        :
        : "memory"
    );

    interrupts_disable();

    if (!scheduler_kernel_handoff_prepare(
        &handoff,
        &owner,
        &frame
    )) {
        kernel_panic("Resume commit preparation failed");
    }

    struct scheduler_handoff_resume_plan plan = {0};

    if (!scheduler_kernel_handoff_resume_preflight(
        &handoff,
        &owner,
        kernel_space,
        paging_read_cr3(),
        &plan
    )) {
        kernel_panic("Resume commit preflight failed");
    }

    struct interrupt_context resumed = {
        .rax = 0xDEADBEEFULL,
    };

    struct interrupt_context sentinel = resumed;

    /*
     * The kernel CR3 is still active. Completion is
     * forbidden before activating the owner space.
     */
    if (scheduler_kernel_handoff_commit_resume(
        &handoff,
        &owner,
        &plan,
        &resumed
    )) {
        kernel_panic(
            "Resume commit accepted kernel CR3"
        );
    }

    if (
        !scheduler_slot_test_frames_equal(
            &resumed,
            &sentinel
        ) ||
        !handoff.pending
    ) {
        kernel_panic(
            "Rejected resume commit changed state"
        );
    }

    if (!scheduler_kernel_handoff_activate_resume_space(
        &handoff,
        &owner,
        &plan
    )) {
        kernel_panic(
            "Resume commit could not activate owner CR3"
        );
    }

    /*
     * No other process may commit the request.
     */
    if (scheduler_kernel_handoff_commit_resume(
        &handoff,
        &other,
        &plan,
        &resumed
    )) {
        kernel_panic(
            "Resume commit accepted wrong owner"
        );
    }

    /*
     * Rejected modifications must preserve the
     * original handoff and output.
     */
    struct scheduler_handoff_resume_plan stale = plan;
    stale.frame.rax ^= 1U;

    if (scheduler_kernel_handoff_commit_resume(
        &handoff,
        &owner,
        &stale,
        &resumed
    )) {
        kernel_panic(
            "Resume commit accepted modified plan"
        );
    }

    handoff.frame.r12 ^= 1U;

    if (scheduler_kernel_handoff_commit_resume(
        &handoff,
        &owner,
        &plan,
        &resumed
    )) {
        kernel_panic(
            "Resume commit accepted modified request"
        );
    }

    handoff.frame.r12 ^= 1U;

    if (
        !handoff.pending ||
        handoff.owner != &owner ||
        !scheduler_slot_test_frames_equal(
            &resumed,
            &sentinel
        )
    ) {
        kernel_panic(
            "Rejected resume commit corrupted state"
        );
    }

    /*
     * Current owner, correct CR3 and unchanged plan:
     * consume the request exactly once.
     */
    if (!scheduler_kernel_handoff_commit_resume(
        &handoff,
        &owner,
        &plan,
        &resumed
    )) {
        kernel_panic(
            "Resume commit rejected valid request"
        );
    }

    if (
        handoff.pending ||
        handoff.owner != NULL ||
        !scheduler_slot_test_frames_equal(
            &resumed,
            &frame
        )
    ) {
        kernel_panic(
            "Resume commit lost saved user frame"
        );
    }

    if (scheduler_kernel_handoff_commit_resume(
        &handoff,
        &owner,
        &plan,
        &resumed
    )) {
        kernel_panic(
            "Resume commit accepted duplicate completion"
        );
    }

    /*
     * The synthetic test never enters Ring 3.
     * Restore kernel CR3 before destroying memory.
     */
    if (
        !paging_address_space_activate(kernel_space) ||
        (paging_read_cr3() & PAGE_ADDRESS_MASK_4K) !=
            kernel_cr3 ||
        !process_memory_destroy(&memory)
    ) {
        kernel_panic(
            "Resume commit cleanup failed"
        );
    }

    if ((original_rflags & (1ULL << 9)) != 0U) {
        interrupts_enable();
    }

    diagnostics_write(
        "[scheduler] Kernel handoff resume commit tests passed\n"
    );
}

static void scheduler_slot_test_timer_decision(void)
{
    /*
     * Normal user execution with available quantum.
     */
    if (
        scheduler_timer_decide(
            true, false, false, 1U, 10U
        ) != SCHEDULER_TIMER_CONTINUE
    ) {
        kernel_panic(
            "Timer decision rejected remaining quantum"
        );
    }

    /*
     * Exhausted quantum without kernel work.
     */
    if (
        scheduler_timer_decide(
            true, false, false, 10U, 10U
        ) != SCHEDULER_TIMER_PREEMPT
    ) {
        kernel_panic(
            "Timer decision missed preemption"
        );
    }

    /*
     * Kernel handoff has priority over preemption.
     */
    if (
        scheduler_timer_decide(
            true, true, false, 10U, 10U
        ) != SCHEDULER_TIMER_HANDOFF
    ) {
        kernel_panic(
            "Timer decision missed handoff priority"
        );
    }

    /*
     * Handoff may run before the quantum expires.
     */
    if (
        scheduler_timer_decide(
            true, true, false, 1U, 10U
        ) != SCHEDULER_TIMER_HANDOFF
    ) {
        kernel_panic(
            "Timer decision delayed eligible handoff"
        );
    }

    /*
     * A pending request excludes both a new handoff
     * and ordinary preemption, even at the quantum
     * boundary.
     */
    if (
        scheduler_timer_decide(
            true, true, true, 10U, 10U
        ) != SCHEDULER_TIMER_CONTINUE ||
        scheduler_timer_decide(
            true, false, true, 10U, 10U
        ) != SCHEDULER_TIMER_CONTINUE ||
        scheduler_timer_decide(
            true, true, true, 1U, 10U
        ) != SCHEDULER_TIMER_CONTINUE
    ) {
        kernel_panic(
            "Timer decision failed to protect pending handoff"
        );
    }

    /*
     * With no pending request, normal scheduling
     * decisions must remain available.
     */
    if (
        scheduler_timer_decide(
            true, true, false, 10U, 10U
        ) != SCHEDULER_TIMER_HANDOFF ||
        scheduler_timer_decide(
            true, false, false, 10U, 10U
        ) != SCHEDULER_TIMER_PREEMPT
    ) {
        kernel_panic(
            "Timer decision did not restore normal policy"
        );
    }

    /*
     * Without a running user process, no user scheduling
     * action should be selected.
     */
    if (
        scheduler_timer_decide(
            false, true, false, 10U, 10U
        ) != SCHEDULER_TIMER_CONTINUE
    ) {
        kernel_panic(
            "Timer decision scheduled absent user process"
        );
    }

    /*
     * Invalid policy input must be conservative.
     */
    if (
        scheduler_timer_decide(
            true, true, false, 10U, 0U
        ) != SCHEDULER_TIMER_CONTINUE
    ) {
        kernel_panic(
            "Timer decision accepted zero quantum"
        );
    }

    /*
     * No current process exists while scheduler
     * registration tests are running.
     */
    struct interrupt_context frame = {
        .cs = 0x1BU,
        .ss = 0x23U,
    };

    if (
        scheduler_timer_decision_for(
            &frame
        ) != SCHEDULER_TIMER_CONTINUE ||
        scheduler_timer_decision_for(
            NULL
        ) != SCHEDULER_TIMER_CONTINUE
    ) {
        kernel_panic(
            "Timer decision selected action without current process"
        );
    }

    diagnostics_write(
        "[scheduler] Timer decision policy tests passed\n"
    );
}

static void scheduler_slot_test_handoff_cr3_activation(void)
{
    struct paging_address_space *kernel_space =
        paging_kernel_address_space();

    if (kernel_space == NULL) {
        kernel_panic(
            "Handoff CR3 test has no kernel address space"
        );
    }

    uint64_t kernel_cr3 =
        paging_read_cr3() & PAGE_ADDRESS_MASK_4K;

    if (
        kernel_cr3 == 0U ||
        kernel_cr3 != kernel_space->pml4_physical
    ) {
        kernel_panic(
            "Handoff CR3 test started outside kernel space"
        );
    }

    /*
     * Unlike synthetic preflight tests, this address
     * space owns real paging structures.
     */
    struct process_memory memory = {0};

    if (!process_memory_create(&memory)) {
        kernel_panic(
            "Unable to create handoff CR3 test space"
        );
    }

    struct process owner = {
        .id = 400U,
        .state = PROCESS_STATE_RUNNING,
        .memory = &memory,
    };

    struct process other = {
        .id = 401U,
        .state = PROCESS_STATE_RUNNING,
        .memory = &memory,
    };

    struct interrupt_context frame = {
        .vector = 0x20U,
        .rip = 0x400000ULL,
        .cs = GDT_USER_CODE_SELECTOR,
        .rflags = 0x202ULL,
        .rsp = 0x70000000ULL,
        .ss = GDT_USER_DATA_SELECTOR,
        .rax = 0x1122334455667788ULL,
    };

    struct scheduler_kernel_handoff handoff;
    scheduler_kernel_handoff_init(&handoff);

    if (!scheduler_kernel_handoff_prepare(
        &handoff,
        &owner,
        &frame
    )) {
        kernel_panic(
            "Unable to prepare handoff CR3 test"
        );
    }

    struct scheduler_handoff_resume_plan plan = {0};

    if (!scheduler_kernel_handoff_resume_preflight(
        &handoff,
        &owner,
        kernel_space,
        paging_read_cr3(),
        &plan
    )) {
        kernel_panic(
            "Handoff CR3 preflight failed"
        );
    }

    uint64_t original_rflags;

    __asm__ volatile (
        "pushfq\n\t"
        "popq %0"
        : "=r"(original_rflags)
        :
        : "memory"
    );

    /*
     * The transition must reject enabled interrupts.
     */
    if (
        (original_rflags & (1ULL << 9)) != 0U &&
        scheduler_kernel_handoff_activate_resume_space(
            &handoff,
            &owner,
            &plan
        )
    ) {
        kernel_panic(
            "Handoff CR3 activation accepted IF=1"
        );
    }

    interrupts_disable();

    /*
     * A different process cannot consume this plan.
     */
    if (scheduler_kernel_handoff_activate_resume_space(
        &handoff,
        &other,
        &plan
    )) {
        kernel_panic(
            "Handoff CR3 activation accepted wrong owner"
        );
    }

    /*
     * Reject stale or modified plans.
     */
    struct scheduler_handoff_resume_plan stale = plan;
    stale.frame.rax ^= 1U;

    if (scheduler_kernel_handoff_activate_resume_space(
        &handoff,
        &owner,
        &stale
    )) {
        kernel_panic(
            "Handoff CR3 activation accepted stale plan"
        );
    }

    /*
     * The pending request itself may have changed.
     * The old plan must then be rejected as well.
     */
    handoff.frame.rax ^= 1U;

    if (scheduler_kernel_handoff_activate_resume_space(
        &handoff,
        &owner,
        &plan
    )) {
        kernel_panic(
            "Handoff CR3 activation accepted changed request"
        );
    }

    handoff.frame.rax ^= 1U;

    if (
        (paging_read_cr3() & PAGE_ADDRESS_MASK_4K) !=
            kernel_cr3 ||
        !handoff.pending ||
        handoff.owner != &owner
    ) {
        kernel_panic(
            "Rejected handoff CR3 activation changed state"
        );
    }

    /*
     * The production wrapper must reject the test
     * owner, which is not scheduler_current().
     */
    if (scheduler_activate_kernel_worker_resume_space(
        &plan
    )) {
        kernel_panic(
            "Handoff CR3 wrapper ignored current process"
        );
    }

    /*
     * Perform the actual Ring 0 CR3 transition.
     */
    if (!scheduler_kernel_handoff_activate_resume_space(
        &handoff,
        &owner,
        &plan
    )) {
        kernel_panic(
            "Handoff owner CR3 activation failed"
        );
    }

    uint64_t user_cr3 =
        paging_read_cr3() & PAGE_ADDRESS_MASK_4K;

    if (
        user_cr3 !=
            memory.address_space.pml4_physical ||
        user_cr3 == kernel_cr3 ||
        !handoff.pending ||
        handoff.owner != &owner
    ) {
        kernel_panic(
            "Handoff CR3 activation violated contract"
        );
    }

    /*
     * Once the user's CR3 is active, another attempt
     * requiring kernel CR3 must be rejected.
     */
    if (scheduler_kernel_handoff_activate_resume_space(
        &handoff,
        &owner,
        &plan
    )) {
        kernel_panic(
            "Handoff CR3 activation accepted wrong source space"
        );
    }

    /*
     * Test-only rollback. We have not performed IRETQ,
     * so execution must return to kernel CR3.
     */
    if (!paging_address_space_activate(kernel_space)) {
        kernel_panic(
            "Handoff CR3 test could not restore kernel space"
        );
    }

    if (
        (paging_read_cr3() & PAGE_ADDRESS_MASK_4K) !=
        kernel_cr3
    ) {
        kernel_panic(
            "Handoff CR3 test restored incorrect space"
        );
    }

    /*
     * Activation does not consume the request.
     * The existing completion primitive still owns
     * that responsibility.
     */
    struct interrupt_context resumed = {0};

    if (
        !scheduler_kernel_handoff_complete(
            &handoff,
            &owner,
            &resumed
        ) ||
        !scheduler_slot_test_frames_equal(
            &resumed,
            &frame
        ) ||
        handoff.pending
    ) {
        kernel_panic(
            "Handoff CR3 test lost saved user context"
        );
    }

    if (!process_memory_destroy(&memory)) {
        kernel_panic(
            "Handoff CR3 test leaked address space"
        );
    }

    if ((original_rflags & (1ULL << 9)) != 0U) {
        interrupts_enable();
    }

    diagnostics_write(
        "[scheduler] Kernel handoff CR3 activation tests passed\n"
    );
}

static void scheduler_slot_test_bsp_supervisor(void)
{
    struct kernel_context worker;

    struct scheduler_worker_test_probe probe = {
        .worker = &worker,
    };

    /*
     * The persistent BSP supervisor must support
     * repeated initialization without resetting
     * its execution context.
     */
    if (
        !kernel_supervisor_bsp_initialize() ||
        !kernel_supervisor_bsp_initialize() ||
        kernel_supervisor_bsp_context() == NULL
    ) {
        kernel_panic(
            "BSP supervisor initialization failed"
        );
    }

    if (
        !kernel_context_init(
            &worker,
            scheduler_bsp_worker_stack,
            sizeof(scheduler_bsp_worker_stack),
            scheduler_worker_test_entry,
            &probe
        ) ||
        !kernel_context_prepare(
            &worker,
            kernel_context_bootstrap
        ) ||
        !kernel_context_bind_supervisor(
            &worker,
            kernel_supervisor_bsp_context()
        )
    ) {
        kernel_panic(
            "BSP supervisor worker setup failed"
        );
    }

    uint64_t original_rflags;

    __asm__ volatile (
        "pushfq\n\t"
        "popq %0"
        : "=r"(original_rflags)
        :
        : "memory"
    );

    interrupts_disable();

    uintptr_t caller_rsp =
        scheduler_nested_test_rsp();

    /*
     * With no registered worker, idle must not
     * attempt to dispatch anything.
     */
    if (scheduler_test_dispatch_idle_kernel_worker()) {
        kernel_panic(
            "Idle dispatched unregistered kernel worker"
        );
    }

    if (
        !scheduler_register_kernel_worker(&worker) ||
        !scheduler_test_dispatch_idle_kernel_worker()
    ) {
        kernel_panic(
            "BSP supervisor first dispatch failed"
        );
    }

    if (
        scheduler_nested_test_rsp() != caller_rsp ||
        worker.state != KERNEL_CONTEXT_WAITING ||
        probe.stage != 1U ||
        probe.entries != 1U ||
        kernel_context_owner(&worker) !=
            KERNEL_CONTEXT_NO_CPU
    ) {
        kernel_panic(
            "BSP supervisor first dispatch contract failed"
        );
    }

    /*
     * A suspended worker with no new request must
     * not be dispatched again.
     */
    if (scheduler_test_dispatch_idle_kernel_worker()) {
        kernel_panic(
            "Idle dispatched waiting kernel worker"
        );
    }

    /*
     * Resume the worker from its saved continuation.
     */
    probe.stage = 2U;

    if (!kernel_context_wake(&worker)) {
        kernel_panic(
            "BSP supervisor worker wake failed"
        );
    }

    if (!scheduler_test_dispatch_idle_kernel_worker()) {
        kernel_panic(
            "BSP supervisor second dispatch failed"
        );
    }

    if (
        scheduler_nested_test_rsp() != caller_rsp ||
        worker.state != KERNEL_CONTEXT_TERMINATED ||
        probe.stage != 3U ||
        probe.entries != 1U ||
        kernel_context_owner(&worker) !=
            KERNEL_CONTEXT_NO_CPU ||
        !scheduler_unregister_kernel_worker(&worker)
    ) {
        kernel_panic(
            "BSP supervisor completion contract failed"
        );
    }

    /*
     * No worker remains registered.
     */
    if (
        scheduler_test_dispatch_idle_kernel_worker() ||
        kernel_supervisor_bsp_dispatch(
            scheduler_dispatch_kernel_worker
        )
    ) {
        kernel_panic(
            "BSP dispatched absent kernel worker"
        );
    }

    diagnostics_write(
    "[scheduler] BSP idle worker dispatch tests passed\n"
    );

    if ((original_rflags & (1ULL << 9)) != 0) {
        interrupts_enable();
    }

    diagnostics_write(
        "[scheduler] BSP supervisor dispatch tests passed\n"
    );
}

static void scheduler_one_way_completion(
    bool dispatched,
    void *argument)
{
    struct scheduler_one_way_test_probe *probe =
        argument;

    if (
        probe != &scheduler_one_way_probe ||
        !dispatched ||
        (paging_read_cr3() & PAGE_ADDRESS_MASK_4K) !=
            probe->kernel_cr3
    ) {
        kernel_panic(
            "One-way BSP supervisor completion invalid"
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

    if ((rflags & (1ULL << 9)) != 0) {
        kernel_panic(
            "One-way BSP supervisor completion enabled interrupts"
        );
    }

    ++probe->completions;
    probe->last_dispatched = dispatched;

    /*
     * Synthetic test exit:
     * transfer control back to the original test runner.
     *
     * The original entry stack is not resumed.
     */
    kernel_context_switch(
        &probe->completion_context,
        &probe->caller
    );

    kernel_panic(
        "One-way BSP supervisor test continuation resumed"
    );
}

static _Noreturn void scheduler_one_way_test_entry(void)
{
    struct scheduler_one_way_test_probe *probe =
        &scheduler_one_way_probe;

    if (probe->enter_from_user_cr3) {
        if (!paging_address_space_activate(
            &probe->memory.address_space
        )) {
            kernel_panic(
                "One-way BSP test could not activate user CR3"
            );
        }

        if (
            (paging_read_cr3() & PAGE_ADDRESS_MASK_4K) !=
            probe->memory.address_space.pml4_physical
        ) {
            kernel_panic(
                "One-way BSP test activated wrong source CR3"
            );
        }
    }

    kernel_supervisor_bsp_enter(
        scheduler_dispatch_kernel_worker,
        scheduler_one_way_completion,
        probe
    );
}

static void scheduler_one_way_test_enter(void)
{
    struct scheduler_one_way_test_probe *probe =
        &scheduler_one_way_probe;

    uintptr_t bottom =
        (uintptr_t) scheduler_one_way_entry_stack;

    uintptr_t top =
        bottom + sizeof(scheduler_one_way_entry_stack);

    if (!kernel_switch_prepare(
        &probe->entry,
        bottom,
        top,
        scheduler_one_way_test_entry
    )) {
        kernel_panic(
            "Unable to prepare one-way BSP test entry"
        );
    }

    /*
     * The synthetic entry abandons its stack.
     * Only the supervisor's explicit continuation
     * can resume this caller.
     */
    kernel_context_switch(
        &probe->caller,
        &probe->entry
    );
}

static void scheduler_slot_test_bsp_one_way(void)
{
    struct scheduler_one_way_test_probe *probe =
        &scheduler_one_way_probe;

    *probe = (struct scheduler_one_way_test_probe) {0};

    if (!kernel_supervisor_bsp_initialize()) {
        kernel_panic(
            "One-way BSP supervisor initialization failed"
        );
    }

    struct paging_address_space *kernel_space =
        paging_kernel_address_space();

    if (kernel_space == NULL) {
        kernel_panic(
            "One-way BSP test has no kernel address space"
        );
    }

    probe->kernel_cr3 =
        kernel_space->pml4_physical;

    if (
        (paging_read_cr3() & PAGE_ADDRESS_MASK_4K) !=
        probe->kernel_cr3
    ) {
        kernel_panic(
            "One-way BSP test started outside kernel CR3"
        );
    }

    if (
        !process_memory_create(&probe->memory) ||
        !kernel_context_init(
            &probe->worker,
            scheduler_one_way_worker_stack,
            sizeof(scheduler_one_way_worker_stack),
            scheduler_worker_test_entry,
            &probe->worker_probe
        ) ||
        !kernel_context_prepare(
            &probe->worker,
            kernel_context_bootstrap
        ) ||
        !kernel_context_bind_supervisor(
            &probe->worker,
            kernel_supervisor_bsp_context()
        )
    ) {
        kernel_panic(
            "One-way BSP worker setup failed"
        );
    }

    probe->worker_probe.worker = &probe->worker;

    uint64_t original_rflags;

    __asm__ volatile (
        "pushfq\n\t"
        "popq %0"
        : "=r"(original_rflags)
        :
        : "memory"
    );

    interrupts_disable();

    if (!scheduler_register_kernel_worker(
        &probe->worker
    )) {
        kernel_panic(
            "One-way BSP worker registration failed"
        );
    }

    /*
     * First pass:
     * kernel CR3 -> supervisor -> worker WAITING.
     */
    probe->enter_from_user_cr3 = false;

    scheduler_one_way_test_enter();

    if (
        probe->completions != 1U ||
        !probe->last_dispatched ||
        probe->worker.state != KERNEL_CONTEXT_WAITING ||
        probe->worker_probe.stage != 1U ||
        kernel_context_owner(&probe->worker) !=
            KERNEL_CONTEXT_NO_CPU ||
        (paging_read_cr3() & PAGE_ADDRESS_MASK_4K) !=
            probe->kernel_cr3
    ) {
        kernel_panic(
            "One-way BSP supervisor first pass failed"
        );
    }

    /*
     * Resume the same worker from its saved context.
     */
    probe->worker_probe.stage = 2U;

    if (!kernel_context_wake(&probe->worker)) {
        kernel_panic(
            "One-way BSP worker wake failed"
        );
    }

    /*
     * Second pass:
     * user CR3 -> supervisor -> kernel CR3 ->
     * worker TERMINATED.
     */
    probe->enter_from_user_cr3 = true;

    scheduler_one_way_test_enter();

    if (
        probe->completions != 2U ||
        !probe->last_dispatched ||
        probe->worker.state !=
            KERNEL_CONTEXT_TERMINATED ||
        probe->worker_probe.stage != 3U ||
        probe->worker_probe.entries != 1U ||
        kernel_context_owner(&probe->worker) !=
            KERNEL_CONTEXT_NO_CPU ||
        (paging_read_cr3() & PAGE_ADDRESS_MASK_4K) !=
            probe->kernel_cr3
    ) {
        kernel_panic(
            "One-way BSP supervisor second pass failed"
        );
    }

    if (
        !scheduler_unregister_kernel_worker(
            &probe->worker
        ) ||
        !process_memory_destroy(&probe->memory)
    ) {
        kernel_panic(
            "One-way BSP supervisor cleanup failed"
        );
    }

    if ((original_rflags & (1ULL << 9)) != 0) {
        interrupts_enable();
    }

    diagnostics_write(
        "[scheduler] BSP one-way supervisor tests passed\n"
    );
}

void scheduler_slot_test_run(void)
{
    if (scheduler_current() != NULL) {
        kernel_panic(
            "Scheduler slot test started with active process"
        );
    }

    /*
     * No real processes have been registered yet when this regression runs.
     */
    scheduler_init();

    scheduler_slot_test_kernel_worker_dispatch();
    scheduler_slot_test_nested_supervisor();
    scheduler_slot_test_bsp_supervisor();
    scheduler_slot_test_bsp_one_way();
    scheduler_slot_test_handoff_capture();
    scheduler_slot_test_handoff_ownership();
    scheduler_slot_test_handoff_completion();
    scheduler_slot_test_handoff_resume_preflight();
    scheduler_slot_test_handoff_cr3_activation();
    scheduler_slot_test_handoff_resume_commit();
    scheduler_slot_test_timer_decision();

        /*
     * Exercise the production idle-work dispatch helper
     * without entering the non-returning idle loop.
     */
    idle_work_test_calls = 0;

    interrupts_disable();

    if (scheduler_test_dispatch_idle_work()) {
        kernel_panic(
            "Scheduler idle work ran without a handler"
        );
    }

    scheduler_set_idle_work_handler(
        scheduler_slot_test_idle_work
    );

    if (
        !scheduler_test_dispatch_idle_work() ||
        idle_work_test_calls != 1U
    ) {
        kernel_panic(
            "Scheduler idle work dispatch failed"
        );
    }

    if (
        scheduler_test_dispatch_idle_work() ||
        idle_work_test_calls != 2U
    ) {
        kernel_panic(
            "Scheduler idle work completion contract failed"
        );
    }

    scheduler_set_idle_work_handler(NULL);

    if (
        scheduler_test_dispatch_idle_work() ||
        idle_work_test_calls != 2U
    ) {
        kernel_panic(
            "Scheduler idle work unregister failed"
        );
    }

    interrupts_enable();

    /*
     * Kernel worker registration owns no execution resources
     * and must not interfere with userspace process slots.
     */
    struct kernel_context worker = {
        .state = KERNEL_CONTEXT_READY,
        .prepared = true,
        .supervisor = NULL,
        .owner_cpu_id = KERNEL_CONTEXT_NO_CPU,
    };

    struct kernel_switch_context supervisor = {0};

    if (scheduler_register_kernel_worker(&worker)) {
        kernel_panic(
            "Scheduler accepted worker without supervisor"
        );
    }

    worker.supervisor = &supervisor;

    if (
        !scheduler_register_kernel_worker(&worker) ||
        scheduler_register_kernel_worker(&worker)
    ) {
        kernel_panic(
            "Scheduler kernel worker registration failed"
        );
    }

    struct kernel_context other_worker = {
        .state = KERNEL_CONTEXT_READY,
        .prepared = true,
        .supervisor = &supervisor,
        .owner_cpu_id = KERNEL_CONTEXT_NO_CPU,
    };

    if (
        scheduler_register_kernel_worker(&other_worker) ||
        scheduler_unregister_kernel_worker(&other_worker)
    ) {
        kernel_panic(
            "Scheduler accepted unrelated kernel worker"
        );
    }

    worker.state = KERNEL_CONTEXT_RUNNING;

    if (scheduler_unregister_kernel_worker(&worker)) {
        kernel_panic(
            "Scheduler unregistered running kernel worker"
        );
    }

    worker.state = KERNEL_CONTEXT_WAITING;

    if (
        !scheduler_unregister_kernel_worker(&worker) ||
        scheduler_unregister_kernel_worker(&worker)
    ) {
        kernel_panic(
            "Scheduler kernel worker unregister failed"
        );
    }

    scheduler_slot_test_initialize_processes();

    /*
     * Only READY processes may enter the scheduler.
     * Test this while registration capacity is still available,
     * so a rejection cannot be attributed to a full slot table.
     */
    test_processes[0].state = PROCESS_STATE_BLOCKED;

    if (scheduler_add(&test_processes[0])) {
        kernel_panic(
            "Scheduler registered a BLOCKED process"
        );
    }

    test_processes[0].state = PROCESS_STATE_SLEEPING;

    if (scheduler_add(&test_processes[0])) {
        kernel_panic(
            "Scheduler registered a SLEEPING process"
        );
    }

    test_processes[0].state = PROCESS_STATE_READY;

    if (!scheduler_add(&test_processes[0])) {
        kernel_panic(
            "Unable to register first scheduler slot test process"
        );
    }

    if (scheduler_add(&test_processes[0])) {
        kernel_panic(
            "Scheduler accepted duplicate process registration"
        );
    }

    for (size_t index = 1;
        index < SCHEDULER_MAX_PROCESSES;
        ++index) {
        if (!scheduler_add(&test_processes[index])) {
            kernel_panic(
                "Unable to fill scheduler slot table"
            );
        }
    }

    /*
     * Exercise wakeup accounting on registered test processes.
     *
     * Interrupts remain disabled while synthetic timer values
     * are supplied so real timer interrupts cannot interfere
     * with the test metadata.
     *
     * The direct state assignments below are test-only setup.
     * Production transitions will be owned by the scheduler.
     */
    struct process *sleeper = &test_processes[0];
    struct process *blocked = &test_processes[1];

    interrupts_disable();

    sleeper->sleep_start_ticks = UINT64_MAX - 1U;
    sleeper->sleep_duration_ticks = 3;
    sleeper->state = PROCESS_STATE_SLEEPING;

    blocked->state = PROCESS_STATE_BLOCKED;

    /*
     * The interval begins two ticks before the uint64_t
     * counter wraps. Tick zero must not wake this process.
     */
    scheduler_wake_sleepers(0);

    if (
        sleeper->state != PROCESS_STATE_SLEEPING ||
        blocked->state != PROCESS_STATE_BLOCKED
    ) {
        kernel_panic(
            "Scheduler woke a process before timer expiration"
        );
    }

    /*
     * At tick one, three ticks have elapsed across wraparound.
     */
    scheduler_wake_sleepers(1);

    if (
        sleeper->state != PROCESS_STATE_READY ||
        sleeper->sleep_start_ticks != 0 ||
        sleeper->sleep_duration_ticks != 0
    ) {
        kernel_panic(
            "Scheduler failed to wake expired sleeping process"
        );
    }

    if (blocked->state != PROCESS_STATE_BLOCKED) {
        kernel_panic(
            "Timer incorrectly woke a BLOCKED process"
        );
    }

    /*
     * Verify integration with actual timer interrupts.
     *
     * This test executes in kernel mode, so periodic timer
     * interrupts may update process states without invoking
     * user-mode scheduling.
     */
    uint64_t live_start = timer_ticks();

    sleeper->sleep_start_ticks = live_start;
    sleeper->sleep_duration_ticks = 2;
    sleeper->state = PROCESS_STATE_SLEEPING;

    while (
        sleeper->state == PROCESS_STATE_SLEEPING &&
        (uint64_t) (timer_ticks() - live_start) < 8
    ) {
        interrupts_wait();
    }

    if (
        sleeper->state != PROCESS_STATE_READY ||
        sleeper->sleep_start_ticks != 0 ||
        sleeper->sleep_duration_ticks != 0
    ) {
        kernel_panic(
            "Timer interrupt failed to wake sleeping process"
        );
    }

    if (
        (uint64_t) (timer_ticks() - live_start) < 2
    ) {
        kernel_panic(
            "Timer woke sleeping process before requested interval"
        );
    }

    if (blocked->state != PROCESS_STATE_BLOCKED) {
        kernel_panic(
            "Timer interrupt incorrectly woke BLOCKED process"
        );
    }

    /*
     * Reject an unregistered descriptor, even when its
     * state field says BLOCKED.
     */
    struct process *unregistered =
        &test_processes[SCHEDULER_MAX_PROCESSES];

    unregistered->state = PROCESS_STATE_BLOCKED;

    if (
        scheduler_wake_blocked(unregistered) ||
        unregistered->state != PROCESS_STATE_BLOCKED
    ) {
        kernel_panic(
            "Scheduler woke an unregistered BLOCKED process"
        );
    }

    unregistered->state = PROCESS_STATE_READY;

    /*
     * A READY process cannot be woken again.
     */
    if (
        scheduler_wake_blocked(sleeper) ||
        sleeper->state != PROCESS_STATE_READY
    ) {
        kernel_panic(
            "Scheduler accepted wakeup of a READY process"
        );
    }

    /*
     * Explicitly wake the registered BLOCKED process.
     */
    if (
        !scheduler_wake_blocked(blocked) ||
        blocked->state != PROCESS_STATE_READY
    ) {
        kernel_panic(
            "Scheduler failed to wake a BLOCKED process"
        );
    }

    /*
     * The transition must be one-shot.
     */
    if (scheduler_wake_blocked(blocked)) {
        kernel_panic(
            "Scheduler accepted duplicate BLOCKED wakeup"
        );
    }

    interrupts_enable();

    diagnostics_write(
        "[scheduler] Timer-driven wakeup tests passed\n"
    );

    diagnostics_write(
        "[scheduler] Explicit BLOCKED wakeup tests passed\n"
    );

    struct process *replacement =
        &test_processes[SCHEDULER_MAX_PROCESSES];

    if (scheduler_add(replacement)) {
        kernel_panic(
            "Scheduler accepted process beyond slot capacity"
        );
    }

    struct process *recycled =
        &test_processes[
            SCHEDULER_SLOT_TEST_RECYCLED_INDEX
        ];

    /*
     * READY registrations cannot be removed through the terminated-process
     * lifecycle path.
     */
    if (scheduler_unregister_terminated(recycled)) {
        kernel_panic(
            "Scheduler unregistered non-terminated process"
        );
    }

    /*
     * The rejected unregister must not have released capacity.
     */
    if (scheduler_add(replacement)) {
        kernel_panic(
            "Rejected scheduler unregister released a slot"
        );
    }

    recycled->state =
        PROCESS_STATE_TERMINATED;

    recycled->termination_reason =
        PROCESS_TERMINATION_EXITED;

    recycled->exit_status = 77;

    recycled->context.rax =
        SCHEDULER_SLOT_TEST_OLD_RAX;

    recycled->context.rbx =
        SCHEDULER_SLOT_TEST_OLD_RBX;

    uint64_t recycled_id =
        recycled->id;

    if (!scheduler_unregister_terminated(recycled)) {
        kernel_panic(
            "Unable to release terminated scheduler slot"
        );
    }

    if (
        recycled->id != recycled_id ||
        recycled->state != PROCESS_STATE_TERMINATED ||
        recycled->termination_reason !=
            PROCESS_TERMINATION_EXITED ||
        recycled->exit_status != 77 ||
        recycled->context.rax !=
            SCHEDULER_SLOT_TEST_OLD_RAX ||
        recycled->context.rbx !=
            SCHEDULER_SLOT_TEST_OLD_RBX
    ) {
        kernel_panic(
            "Scheduler unregister mutated process metadata"
        );
    }

    if (scheduler_unregister_terminated(recycled)) {
        kernel_panic(
            "Scheduler unregistered process more than once"
        );
    }

    replacement->context.rax =
        SCHEDULER_SLOT_TEST_NEW_RAX;

    replacement->context.rbx =
        SCHEDULER_SLOT_TEST_NEW_RBX;

    uint64_t replacement_id =
        replacement->id;

    if (!scheduler_add(replacement)) {
        kernel_panic(
            "Scheduler did not reuse released slot capacity"
        );
    }

    if (
        replacement->id != replacement_id ||
        replacement->state != PROCESS_STATE_READY ||
        replacement->termination_reason !=
            PROCESS_TERMINATION_NONE ||
        replacement->exit_status != 0 ||
        replacement->context.rax !=
            SCHEDULER_SLOT_TEST_NEW_RAX ||
        replacement->context.rbx !=
            SCHEDULER_SLOT_TEST_NEW_RBX
    ) {
        kernel_panic(
            "Scheduler slot reuse leaked stale process state"
        );
    }

    if (scheduler_add(replacement)) {
        kernel_panic(
            "Scheduler accepted duplicate replacement process"
        );
    }

    if (scheduler_current() != NULL) {
        kernel_panic(
            "Scheduler registration unexpectedly selected a process"
        );
    }

    /*
     * Remove every test registration so the real user-process fixtures begin
     * with a completely empty scheduler.
     */
    for (size_t index = 0;
         index < SCHEDULER_MAX_PROCESSES;
         ++index) {
        if (
            index ==
            SCHEDULER_SLOT_TEST_RECYCLED_INDEX
        ) {
            continue;
        }

        test_processes[index].state =
            PROCESS_STATE_TERMINATED;

        if (!scheduler_unregister_terminated(
            &test_processes[index]
        )) {
            kernel_panic(
                "Unable to clean scheduler slot test registration"
            );
        }
    }

    replacement->state =
        PROCESS_STATE_TERMINATED;

    if (!scheduler_unregister_terminated(replacement)) {
        kernel_panic(
            "Unable to clean reused scheduler slot"
        );
    }

    diagnostics_write(
        "[scheduler] Scheduler slot recycling test passed\n"
    );
}

static void scheduler_slot_test_initialize_processes(void)
{
    for (size_t index = 0;
         index < SCHEDULER_SLOT_TEST_PROCESS_COUNT;
         ++index) {
        test_layouts[index].entry_point =
            SCHEDULER_SLOT_TEST_ENTRY_BASE +
            ((uint64_t) index * 0x1000ULL);

            test_layouts[index].stack.stack_top =
            SCHEDULER_SLOT_TEST_STACK_TOP -
            ((uint64_t) index * 0x1000ULL);

        /*
         * Simulate stale metadata in reused process storage.
         * process_init() must reset both timer-wait fields.
         */
        test_processes[index].sleep_start_ticks = UINT64_MAX;
        test_processes[index].sleep_duration_ticks = UINT64_MAX;

        if (!process_init(
            &test_processes[index],
            100ULL + (uint64_t) index,
            &test_memories[index],
            &test_layouts[index]
        )) {
            kernel_panic(
                "Unable to initialize scheduler slot test process"
            );
        }

        if (
            test_processes[index].state != PROCESS_STATE_READY ||
            test_processes[index].sleep_start_ticks != 0 ||
            test_processes[index].sleep_duration_ticks != 0
        ) {
            kernel_panic(
                "Process initialization retained stale sleep metadata"
            );
        }
    }
}

static void scheduler_slot_test_kernel_worker_dispatch(void)
{
    struct cpu_local cpu = {0};
    struct kernel_context worker;
    struct kernel_switch_context supervisor = {0};

    struct scheduler_worker_test_probe probe = {
        .worker = &worker,
    };

    uint32_t apic_id;

    if (
        !arch_cpu_apic_id(&apic_id) ||
        !cpu_local_init(&cpu, 0U, apic_id) ||
        !kernel_context_init(
            &worker,
            scheduler_worker_test_stack,
            sizeof(scheduler_worker_test_stack),
            scheduler_worker_test_entry,
            &probe
        ) ||
        !kernel_context_prepare(
            &worker,
            kernel_context_bootstrap
        ) ||
        !kernel_context_bind_supervisor(
            &worker,
            &supervisor
        )
    ) {
        kernel_panic(
            "Scheduler worker dispatch setup failed"
        );
    }

    interrupts_disable();

    if (scheduler_dispatch_kernel_worker(&cpu)) {
        kernel_panic(
            "Scheduler dispatched unregistered worker"
        );
    }

    if (scheduler_kernel_worker_ready()) {
        kernel_panic(
            "Unregistered kernel worker reported READY"
        );
    }

    if (!scheduler_register_kernel_worker(&worker)) {
        kernel_panic(
            "Scheduler could not register executable worker"
        );
    }

    if (!scheduler_kernel_worker_ready()) {
        kernel_panic(
            "Registered kernel worker not READY"
        );
    }

    struct interrupt_context user_context = {
        .cs = 0x1BU,
    };

    struct interrupt_context kernel_context = {
        .cs = 0x08U,
    };

    /*
     * A READY worker alone is insufficient.
     * The scheduler must also have a running user process.
     */
    if (
        scheduler_kernel_worker_handoff_needed(
            &user_context
        ) ||
        scheduler_kernel_worker_handoff_needed(
            &kernel_context
        ) ||
        scheduler_kernel_worker_handoff_needed(NULL)
    ) {
        kernel_panic(
            "Scheduler accepted kernel handoff without running user process"
        );
    }

    /*
     * A registered READY worker must not be sufficient
     * to publish a scheduler-owned request.
     *
     * There is no current process in this test.
     */
    if (
        scheduler_kernel_worker_handoff_pending() ||
        scheduler_prepare_kernel_worker_handoff(
            &user_context
        ) ||
        scheduler_prepare_kernel_worker_handoff(
            &kernel_context
        ) ||
        scheduler_prepare_kernel_worker_handoff(NULL)
    ) {
        kernel_panic(
            "Scheduler prepared handoff without current process"
        );
    }

    if (scheduler_kernel_worker_handoff_pending()) {
        kernel_panic(
            "Rejected handoff left pending request"
        );
    }

    if (scheduler_release_kernel_worker_handoff(NULL)) {
        kernel_panic(
            "Scheduler released nonexistent handoff"
        );
    }

    if (!scheduler_dispatch_kernel_worker(&cpu)) {
        kernel_panic(
            "Scheduler first worker dispatch failed"
        );
    }

    if (
        worker.state != KERNEL_CONTEXT_WAITING ||
        probe.entries != 1U ||
        probe.stage != 1U ||
        kernel_context_owner(&worker) !=
            KERNEL_CONTEXT_NO_CPU ||
        cpu_local_active_kernel_context(&cpu) != NULL
    ) {
        kernel_panic(
            "Scheduler worker waiting state invalid"
        );
    }

    if (scheduler_kernel_worker_ready()) {
        kernel_panic(
            "Waiting kernel worker reported READY"
        );
    }

    if (scheduler_dispatch_kernel_worker(&cpu)) {
        kernel_panic(
            "Scheduler dispatched waiting worker"
        );
    }

    probe.stage = 2U;

    if (!kernel_context_wake(&worker)) {
        kernel_panic(
            "Scheduler worker wake failed"
        );
    }

    if (!scheduler_kernel_worker_ready()) {
        kernel_panic(
            "Woken kernel worker not READY"
        );
    }

    if (!scheduler_dispatch_kernel_worker(&cpu)) {
        kernel_panic(
            "Scheduler worker resume dispatch failed"
        );
    }

    if (
        worker.state != KERNEL_CONTEXT_TERMINATED ||
        probe.entries != 1U ||
        probe.stage != 3U ||
        kernel_context_owner(&worker) !=
            KERNEL_CONTEXT_NO_CPU ||
        cpu_local_active_kernel_context(&cpu) != NULL
    ) {
        kernel_panic(
            "Scheduler worker completion state invalid"
        );
    }

    if (scheduler_kernel_worker_ready()) {
        kernel_panic(
            "Terminated kernel worker reported READY"
        );
    }

    if (
        scheduler_dispatch_kernel_worker(&cpu) ||
        !scheduler_unregister_kernel_worker(&worker)
    ) {
        kernel_panic(
            "Scheduler worker cleanup failed"
        );
    }

    interrupts_enable();
}
