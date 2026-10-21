// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include "cpu_local_test.h"

#include <myos/kernel/arch/x86_64/cpu_identity.h>
#include <myos/kernel/core/cpu/cpu_local.h>
#include <myos/kernel/core/execution/kernel_context.h>
#include <myos/kernel/core/panic.h>
#include <myos/kernel/diagnostics/diagnostics.h>

#include <stddef.h>
#include <stdint.h>

static void cpu_local_test_initialization(void)
{
    struct cpu_local cpu = {0};

    if (!cpu_local_init(&cpu, 0U, 4U)) {
        kernel_panic("CPU local initialization failed");
    }

    if (
        !cpu.initialized ||
        cpu.logical_id != 0U ||
        cpu.apic_id != 4U ||
        cpu.active_kernel_context != NULL ||
        cpu_local_active_kernel_context(&cpu) != NULL
    ) {
        kernel_panic("CPU local initialization contract failed");
    }
}

static void cpu_local_test_independent_instances(void)
{
    struct cpu_local cpu0 = {0};
    struct cpu_local cpu1 = {0};

    if (
        !cpu_local_init(&cpu0, 0U, 2U) ||
        !cpu_local_init(&cpu1, 1U, 8U)
    ) {
        kernel_panic("CPU local independent setup failed");
    }

    if (
        &cpu0 == &cpu1 ||
        cpu0.logical_id != 0U ||
        cpu1.logical_id != 1U ||
        cpu0.apic_id != 2U ||
        cpu1.apic_id != 8U ||
        cpu_local_active_kernel_context(&cpu0) != NULL ||
        cpu_local_active_kernel_context(&cpu1) != NULL
    ) {
        kernel_panic("CPU local instances are not independent");
    }
}

static void cpu_local_test_invalid(void)
{
    struct cpu_local cpu = {0};

    if (
        cpu_local_init(NULL, 0U, 0U) ||
        cpu_local_active_kernel_context(NULL) != NULL ||
        cpu_local_active_kernel_context(&cpu) != NULL
    ) {
        kernel_panic("CPU local accepted invalid descriptor");
    }
}

static void cpu_local_test_activation(void)
{
    struct cpu_local cpu = {0};

    struct kernel_switch_context supervisor = {0};

    struct kernel_context worker_a = {
        .prepared = true,
        .state = KERNEL_CONTEXT_RUNNING,
        .supervisor = &supervisor,
        .cpu = {
            .rsp = 0x1000U,
        },
    };

    struct kernel_context worker_b = {
        .prepared = true,
        .state = KERNEL_CONTEXT_RUNNING,
        .supervisor = &supervisor,
        .cpu = {
            .rsp = 0x2000U,
        },
    };

    if (!cpu_local_init(&cpu, 0U, 4U)) {
        kernel_panic("CPU local activation setup failed");
    }

    if (
        !cpu_local_activate_kernel_context(
            &cpu, &worker_a
        ) ||
        cpu_local_active_kernel_context(&cpu) != &worker_a
    ) {
        kernel_panic("CPU local activation failed");
    }

    if (
        cpu_local_activate_kernel_context(
            &cpu, &worker_a
        ) ||
        cpu_local_activate_kernel_context(
            &cpu, &worker_b
        ) ||
        cpu_local_active_kernel_context(&cpu) != &worker_a
    ) {
        kernel_panic("CPU local accepted duplicate activation");
    }

    if (
        cpu_local_deactivate_kernel_context(
            &cpu, &worker_b
        ) ||
        cpu_local_active_kernel_context(&cpu) != &worker_a
    ) {
        kernel_panic("CPU local accepted foreign deactivation");
    }

    worker_a.state = KERNEL_CONTEXT_WAITING;

    if (
        !cpu_local_deactivate_kernel_context(
            &cpu, &worker_a
        ) ||
        cpu_local_active_kernel_context(&cpu) != NULL
    ) {
        kernel_panic("CPU local deactivation failed");
    }

    if (
        !cpu_local_activate_kernel_context(
            &cpu, &worker_b
        ) ||
        cpu_local_active_kernel_context(&cpu) != &worker_b
    ) {
        kernel_panic("CPU local replacement after release failed");
    }
}

static void cpu_local_test_activation_invalid(void)
{
    struct cpu_local cpu = {0};
    struct cpu_local uninitialized = {0};

    struct kernel_switch_context supervisor = {0};

    struct kernel_context worker = {
        .state = KERNEL_CONTEXT_READY,
        .prepared = true,
        .supervisor = &supervisor,
        .cpu = {
            .rsp = 0x1000U,
        },
    };

    if (!cpu_local_init(&cpu, 0U, 4U)) {
        kernel_panic("CPU local invalid activation setup failed");
    }

    if (
        cpu_local_activate_kernel_context(NULL, &worker) ||
        cpu_local_activate_kernel_context(&cpu, NULL) ||
        cpu_local_activate_kernel_context(
            &uninitialized, &worker
        ) ||
        cpu_local_activate_kernel_context(&cpu, &worker)
    ) {
        kernel_panic("CPU local accepted invalid activation");
    }

    worker.state = KERNEL_CONTEXT_RUNNING;
    worker.prepared = false;

    if (cpu_local_activate_kernel_context(&cpu, &worker)) {
        kernel_panic("CPU local accepted unprepared worker");
    }

    worker.prepared = true;
    worker.supervisor = NULL;

    if (cpu_local_activate_kernel_context(&cpu, &worker)) {
        kernel_panic("CPU local accepted unsupervised worker");
    }

    worker.supervisor = &supervisor;
    worker.cpu.rsp = 0;

    if (
        cpu_local_activate_kernel_context(&cpu, &worker) ||
        cpu_local_deactivate_kernel_context(&cpu, &worker) ||
        cpu_local_deactivate_kernel_context(NULL, &worker) ||
        cpu_local_deactivate_kernel_context(&cpu, NULL) ||
        cpu_local_active_kernel_context(&cpu) != NULL
    ) {
        kernel_panic("CPU local invalid operation mutated state");
    }
}

static void cpu_local_test_physical_identity(void)
{
    struct cpu_local cpu = {0};
    uint32_t current_apic_id;

    if (
        !arch_cpu_apic_id(&current_apic_id) ||
        !cpu_local_init(&cpu, 0U, current_apic_id)
    ) {
        kernel_panic("CPU physical identity setup failed");
    }

    if (!cpu_local_matches_current_cpu(&cpu)) {
        kernel_panic("CPU local rejected executing CPU");
    }

    /*
     * Use a different xAPIC ID without assuming the
     * actual processor's ID is zero.
     */
    cpu.apic_id = current_apic_id ^ 1U;

    if (cpu_local_matches_current_cpu(&cpu)) {
        kernel_panic("CPU local accepted foreign CPU identity");
    }

    if (
        cpu_local_matches_current_cpu(NULL) ||
        arch_cpu_apic_id(NULL)
    ) {
        kernel_panic("CPU physical identity accepted NULL");
    }
}

void cpu_local_test_run(void)
{
    cpu_local_test_initialization();
    cpu_local_test_independent_instances();
    cpu_local_test_invalid();

    cpu_local_test_activation();
    cpu_local_test_activation_invalid();

    cpu_local_test_physical_identity();

    diagnostics_write(
        "[cpu-local] Descriptor tests passed\n"
    );
}
