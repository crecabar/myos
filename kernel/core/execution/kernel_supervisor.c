// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include <myos/kernel/core/execution/kernel_supervisor.h>

#include "../../arch/x86_64/cpu_identity.h"
#include "../../arch/x86_64/irq_supervisor.h"
#include "../../arch/x86_64/paging.h"
#include "../../arch/x86_64/kernel_switch.h"
#include "../cpu/cpu_local.h"
#include "../panic.h"

#include <stddef.h>
#include <stdint.h>

#define KERNEL_SUPERVISOR_STACK_SIZE 16384U
#define X86_RFLAGS_IF (1ULL << 9)

struct kernel_supervisor_bsp {
    struct cpu_local cpu;
    struct kernel_switch_context context;

    bool initialized;
    bool dispatching;

    kernel_supervisor_dispatch_fn one_way_dispatch;
    kernel_supervisor_completion_fn one_way_completion;
    void *one_way_argument;
};

struct kernel_supervisor_invocation {
    kernel_supervisor_dispatch_fn dispatch;
    bool completed;
};

static struct kernel_supervisor_bsp bsp_supervisor;

static _Alignas(16)
unsigned char bsp_supervisor_stack[
    KERNEL_SUPERVISOR_STACK_SIZE
];

static uint64_t kernel_supervisor_rflags(void)
{
    uint64_t flags;

    __asm__ volatile (
        "pushfq\n\t"
        "popq %0"
        : "=r"(flags)
        :
        : "memory"
    );

    return flags;
}

static bool kernel_supervisor_kernel_cr3_active(void)
{
    struct paging_address_space *space =
        paging_kernel_address_space();

    if (space == NULL) {
        return false;
    }

    uint64_t current =
        paging_read_cr3() & PAGE_ADDRESS_MASK_4K;

    uint64_t expected =
        space->pml4_physical & PAGE_ADDRESS_MASK_4K;

    return expected != 0 && current == expected;
}

bool kernel_supervisor_bsp_initialize(void)
{
    if (bsp_supervisor.initialized) {
        return
            !bsp_supervisor.dispatching &&
            cpu_local_matches_current_cpu(
                &bsp_supervisor.cpu
            );
    }

    uint32_t apic_id;

    if (!arch_cpu_apic_id(&apic_id)) {
        return false;
    }

    if (!cpu_local_init(
        &bsp_supervisor.cpu,
        0U,
        apic_id
    )) {
        return false;
    }

    bsp_supervisor.context =
        (struct kernel_switch_context) {0};

    bsp_supervisor.dispatching = false;
    bsp_supervisor.initialized = true;

    return true;
}

struct kernel_switch_context *
kernel_supervisor_bsp_context(void)
{
    if (!bsp_supervisor.initialized) {
        return NULL;
    }

    return &bsp_supervisor.context;
}

static void kernel_supervisor_dispatch_entry(
    void *argument)
{
    struct kernel_supervisor_invocation *invocation =
        argument;

    uintptr_t rsp;

    __asm__ volatile (
        "movq %%rsp, %0"
        : "=r"(rsp)
        :
        : "memory"
    );

    uintptr_t bottom =
        (uintptr_t) bsp_supervisor_stack;

    uintptr_t top =
        bottom + sizeof(bsp_supervisor_stack);

    if (
        invocation == NULL ||
        invocation->dispatch == NULL ||
        rsp < bottom ||
        rsp >= top
    ) {
        kernel_panic(
            "BSP supervisor entered on invalid stack"
        );
    }

    if (
        !cpu_local_matches_current_cpu(
            &bsp_supervisor.cpu
        ) ||
        (kernel_supervisor_rflags() &
            X86_RFLAGS_IF) != 0
    ) {
        kernel_panic(
            "BSP supervisor execution invariant failed"
        );
    }

    invocation->completed =
        invocation->dispatch(&bsp_supervisor.cpu);
}

bool kernel_supervisor_bsp_dispatch(
    kernel_supervisor_dispatch_fn dispatch)
{
    if (
        !bsp_supervisor.initialized ||
        bsp_supervisor.dispatching ||
        dispatch == NULL ||
        !cpu_local_matches_current_cpu(
            &bsp_supervisor.cpu
        ) ||
        cpu_local_active_kernel_context(
            &bsp_supervisor.cpu
        ) != NULL ||
        (kernel_supervisor_rflags() &
            X86_RFLAGS_IF) != 0 ||
        !kernel_supervisor_kernel_cr3_active()
    ) {
        return false;
    }

    /*
     * The caller must not already execute on the
     * dedicated supervisor stack.
     */
    uintptr_t rsp;

    __asm__ volatile (
        "movq %%rsp, %0"
        : "=r"(rsp)
        :
        : "memory"
    );

    uintptr_t bottom =
        (uintptr_t) bsp_supervisor_stack;

    uintptr_t top =
        bottom + sizeof(bsp_supervisor_stack);

    if (rsp >= bottom && rsp < top) {
        return false;
    }

    struct kernel_supervisor_invocation invocation = {
        .dispatch = dispatch,
        .completed = false,
    };

    bsp_supervisor.dispatching = true;

    kernel_supervisor_stack_call(
        top,
        kernel_supervisor_dispatch_entry,
        &invocation
    );

    /*
     * The callback has returned physically to its
     * original caller stack. No worker may remain
     * active on this CPU.
     */
    if (
        cpu_local_active_kernel_context(
            &bsp_supervisor.cpu
        ) != NULL ||
        (kernel_supervisor_rflags() &
            X86_RFLAGS_IF) != 0 ||
        !kernel_supervisor_kernel_cr3_active()
    ) {
        kernel_panic(
            "BSP supervisor returned with invalid CPU state"
        );
    }

    bsp_supervisor.dispatching = false;

    return invocation.completed;
}

static _Noreturn void kernel_supervisor_bsp_one_way_entry(
    void *argument)
{
    struct kernel_supervisor_bsp *supervisor = argument;

    uintptr_t rsp;

    __asm__ volatile (
        "movq %%rsp, %0"
        : "=r"(rsp)
        :
        : "memory"
    );

    uintptr_t bottom =
        (uintptr_t) bsp_supervisor_stack;

    uintptr_t top =
        bottom + sizeof(bsp_supervisor_stack);

    if (
        supervisor != &bsp_supervisor ||
        !supervisor->initialized ||
        !supervisor->dispatching ||
        supervisor->one_way_dispatch == NULL ||
        supervisor->one_way_completion == NULL ||
        rsp < bottom ||
        rsp >= top ||
        !cpu_local_matches_current_cpu(
            &supervisor->cpu
        ) ||
        (kernel_supervisor_rflags() &
            X86_RFLAGS_IF) != 0
    ) {
        kernel_panic(
            "BSP one-way supervisor entry invariant failed"
        );
    }

    /*
     * We may have entered from a Ring 3 process's
     * address space. The independent supervisor stack
     * and kernel higher half remain mapped there.
     */
    struct paging_address_space *kernel_space =
        paging_kernel_address_space();

    if (kernel_space == NULL) {
        kernel_panic(
            "BSP one-way supervisor has no kernel CR3"
        );
    }

    if (!kernel_supervisor_kernel_cr3_active()) {
        if (!paging_address_space_activate(
            kernel_space
        )) {
            kernel_panic(
                "BSP one-way supervisor could not activate kernel CR3"
            );
        }
    }

    if (!kernel_supervisor_kernel_cr3_active()) {
        kernel_panic(
            "BSP one-way supervisor activated incorrect CR3"
        );
    }

    /*
     * Dispatch can suspend or terminate the worker.
     * In either case it returns physically to this
     * supervisor stack.
     */
    bool dispatched =
        supervisor->one_way_dispatch(&supervisor->cpu);

    if (
        cpu_local_active_kernel_context(
            &supervisor->cpu
        ) != NULL ||
        (kernel_supervisor_rflags() &
            X86_RFLAGS_IF) != 0 ||
        !kernel_supervisor_kernel_cr3_active()
    ) {
        kernel_panic(
            "BSP one-way supervisor dispatch invariant failed"
        );
    }

    kernel_supervisor_completion_fn completion =
        supervisor->one_way_completion;

    void *completion_argument =
        supervisor->one_way_argument;

    /*
     * The worker no longer owns the CPU.
     *
     * Retire this invocation before transferring
     * execution to the non-returning continuation.
     */
    supervisor->one_way_dispatch = NULL;
    supervisor->one_way_completion = NULL;
    supervisor->one_way_argument = NULL;
    supervisor->dispatching = false;

    completion(dispatched, completion_argument);

    kernel_panic(
        "BSP one-way supervisor completion returned"
    );
}

_Noreturn void kernel_supervisor_bsp_enter(
    kernel_supervisor_dispatch_fn dispatch,
    kernel_supervisor_completion_fn completion,
    void *argument)
{
    if (
        !bsp_supervisor.initialized ||
        bsp_supervisor.dispatching ||
        dispatch == NULL ||
        completion == NULL ||
        !cpu_local_matches_current_cpu(
            &bsp_supervisor.cpu
        ) ||
        cpu_local_active_kernel_context(
            &bsp_supervisor.cpu
        ) != NULL ||
        (kernel_supervisor_rflags() &
            X86_RFLAGS_IF) != 0
    ) {
        kernel_panic(
            "Invalid BSP one-way supervisor request"
        );
    }

    /*
     * All invocation metadata is stored persistently.
     * Nothing is borrowed from the abandoned caller stack
     * except the separately lifetime-guaranteed argument.
     */
    bsp_supervisor.one_way_dispatch = dispatch;
    bsp_supervisor.one_way_completion = completion;
    bsp_supervisor.one_way_argument = argument;
    bsp_supervisor.dispatching = true;

    uintptr_t stack_top =
        (uintptr_t) bsp_supervisor_stack +
        sizeof(bsp_supervisor_stack);

    irq_supervisor_enter(
        stack_top,
        kernel_supervisor_bsp_one_way_entry,
        &bsp_supervisor
    );
}
