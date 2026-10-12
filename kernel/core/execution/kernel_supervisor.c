// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include "kernel_supervisor.h"

#include "../../arch/x86_64/cpu_identity.h"
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
