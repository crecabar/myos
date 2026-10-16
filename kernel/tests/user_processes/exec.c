// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file exec.c
 * @brief Ring-3 exec image-replacement regression fixture.
 */

#include "exec.h"

#include "../../arch/x86_64/interrupts.h"
#include "../../arch/x86_64/paging.h"
#include "../../core/panic.h"
#include "../../diagnostics/diagnostics.h"
#include "../../elf/elf64.h"
#include "../../memory/heap.h"
#include "../../memory/memory.h"
#include "../../process/create.h"
#include "../../process/exec.h"
#include "../../scheduler/scheduler.h"
#include "../../syscall/syscall.h"

#include <stddef.h>
#include <stdint.h>

// Static local variables
static struct process_instance *exec_test_instance;

static struct elf64_image exec_test_target_image;

static uint64_t exec_test_free_frame_baseline;
static struct kernel_heap_stats exec_test_heap_baseline;

static uint64_t exec_test_pid;
static uint64_t exec_test_replacement_cr3;

static bool exec_test_replacement_committed;

extern const uint8_t process_exec_caller_fixture_start[];
extern const uint8_t process_exec_caller_fixture_end[];

extern const uint8_t process_exec_target_fixture_start[];
extern const uint8_t process_exec_target_fixture_end[];

// Public functions implementations
void user_process_exec_test_prepare(void)
{
    uintptr_t caller_start =
        (uintptr_t)
        process_exec_caller_fixture_start;

    uintptr_t caller_end =
        (uintptr_t)
        process_exec_caller_fixture_end;

    if (caller_end <= caller_start) {
        kernel_panic(
            "Exec caller fixture has invalid bounds"
        );
    }

    uintptr_t caller_size_value =
        caller_end -
        caller_start;

    if (caller_size_value > SIZE_MAX) {
        kernel_panic(
            "Exec caller fixture is too large"
        );
    }

    struct elf64_image caller_image;

    if (!elf64_parse(
        process_exec_caller_fixture_start,
        (size_t) caller_size_value,
        &caller_image
    )) {
        kernel_panic(
            "Unable to parse exec caller fixture"
        );
    }

    uintptr_t target_start =
        (uintptr_t)
        process_exec_target_fixture_start;

    uintptr_t target_end =
        (uintptr_t)
        process_exec_target_fixture_end;

    if (target_end <= target_start) {
        kernel_panic(
            "Exec target fixture has invalid bounds"
        );
    }

    uintptr_t target_size_value =
        target_end -
        target_start;

    if (target_size_value > SIZE_MAX) {
        kernel_panic(
            "Exec target fixture is too large"
        );
    }

    if (!elf64_parse(
        process_exec_target_fixture_start,
        (size_t) target_size_value,
        &exec_test_target_image
    )) {
        kernel_panic(
            "Unable to parse exec target fixture"
        );
    }

    exec_test_free_frame_baseline =
        physical_free_frame_count();

    if (!kernel_heap_stats_get(
        &exec_test_heap_baseline
    )) {
        kernel_panic(
            "Unable to read heap baseline before exec regression"
        );
    }

    exec_test_replacement_committed = false;
    exec_test_replacement_cr3 = 0;

    const char *argv[] = {
        "exec-caller",
    };

    exec_test_instance =
        process_create_elf64(
            &caller_image,
            1,
            argv,
            0,
            NULL
        );

    if (exec_test_instance == NULL) {
        kernel_panic(
            "Unable to create exec regression process"
        );
    }

    exec_test_pid =
        exec_test_instance->process.id;
}

void user_process_exec_test_syscall(
    struct interrupt_context *context,
    uint64_t operation)
{
    if (context == NULL) {
        kernel_panic(
            "Exec regression received null syscall context"
        );
    }

    if (exec_test_instance == NULL) {
        kernel_panic(
            "Exec regression has no active process"
        );
    }

    struct process *process =
        scheduler_current();

    if (
        process == NULL ||
        process !=
            &exec_test_instance->process ||
        process->id !=
            exec_test_pid
    ) {
        kernel_panic(
            "Exec regression observed incorrect current process"
        );
    }

    uint64_t original_cr3 =
        process
            ->memory
            ->address_space
            .pml4_physical &
        PAGE_ADDRESS_MASK_4K;

    if (operation == 0) {
        uint64_t original_entry_point =
            exec_test_target_image.entry_point;

        exec_test_target_image.entry_point = 0;

        struct process_exec_candidate candidate;

        candidate.prepared = false;

        const char *argv[] = {
            "exec-target",
            "replacement",
        };

        const char *envp[] = {
            "EXEC=1",
        };

        bool prepared =
            process_exec_candidate_prepare_elf64(
                &candidate,
                &exec_test_target_image,
                2,
                argv,
                1,
                envp
            );

        exec_test_target_image.entry_point =
            original_entry_point;

        if (
            prepared ||
            candidate.prepared
        ) {
            kernel_panic(
                "Invalid exec regression target unexpectedly prepared"
            );
        }

        if (
            process->id !=
                exec_test_pid ||
            (
                paging_read_cr3() &
                PAGE_ADDRESS_MASK_4K
            ) != original_cr3
        ) {
            kernel_panic(
                "Failed exec modified the original process"
            );
        }

        context->rax =
            (uint64_t)
            syscall_result_error(
                SYSCALL_ERROR_INVALID_ARGUMENT
            );

        return;
    }

    if (operation != 1) {
        context->rax =
            (uint64_t)
            syscall_result_error(
                SYSCALL_ERROR_INVALID_ARGUMENT
            );

        return;
    }

    struct process_exec_candidate candidate;

    candidate.prepared = false;

    const char *argv[] = {
        "exec-target",
        "replacement",
    };

    const char *envp[] = {
        "EXEC=1",
    };

    if (!process_exec_candidate_prepare_elf64(
        &candidate,
        &exec_test_target_image,
        2,
        argv,
        1,
        envp
    )) {
        kernel_panic(
            "Unable to prepare valid exec regression target"
        );
    }

    exec_test_replacement_cr3 =
        candidate.image
            .memory
            .address_space
            .pml4_physical &
        PAGE_ADDRESS_MASK_4K;

    if (
        exec_test_replacement_cr3 == 0 ||
        exec_test_replacement_cr3 ==
            original_cr3
    ) {
        kernel_panic(
            "Exec regression replacement address space is invalid"
        );
    }

    if (!process_exec_candidate_commit_current(
        process,
        &candidate
    )) {
        kernel_panic(
            "Unable to commit exec regression replacement"
        );
    }

    if (candidate.prepared) {
        kernel_panic(
            "Committed exec regression candidate remained prepared"
        );
    }

    if (
        process->id !=
            exec_test_pid ||
        process->state !=
            PROCESS_STATE_RUNNING
    ) {
        kernel_panic(
            "Exec regression changed process identity"
        );
    }

    exec_test_replacement_committed = true;

    if (!scheduler_load_current_context(
        context
    )) {
        kernel_panic(
            "Unable to load replacement exec context"
        );
    }
}

void user_process_exec_test_terminated(
    struct process *process)
{
    if (
        exec_test_instance == NULL ||
        process !=
            &exec_test_instance->process
    ) {
        kernel_panic(
            "Exec regression received unexpected process"
        );
    }

    if (
        !exec_test_replacement_committed ||
        process->id !=
            exec_test_pid ||
        process->state !=
            PROCESS_STATE_TERMINATED ||
        process->termination_reason !=
            PROCESS_TERMINATION_EXITED ||
        process->exit_status != 0
    ) {
        kernel_panic(
            "Ring-3 exec regression produced unexpected result"
        );
    }

    if (
        process->image == NULL ||
        (
            process->image
                ->memory
                .address_space
                .pml4_physical &
            PAGE_ADDRESS_MASK_4K
        ) != exec_test_replacement_cr3
    ) {
        kernel_panic(
            "Exec regression terminated with incorrect replacement image"
        );
    }

    if (!process_release_terminated(
        exec_test_instance
    )) {
        kernel_panic(
            "Unable to release exec regression process"
        );
    }

    exec_test_instance = NULL;

    if (
        physical_free_frame_count() !=
        exec_test_free_frame_baseline
    ) {
        kernel_panic(
            "Ring-3 exec regression leaked physical frames"
        );
    }

    struct kernel_heap_stats heap_after;

    if (!kernel_heap_stats_get(
        &heap_after
    )) {
        kernel_panic(
            "Unable to read heap state after exec regression"
        );
    }

    if (
        heap_after.allocated_block_count !=
            exec_test_heap_baseline.allocated_block_count ||
        heap_after.allocated_bytes !=
            exec_test_heap_baseline.allocated_bytes
    ) {
        kernel_panic(
            "Ring-3 exec regression leaked kernel heap allocations"
        );
    }

    diagnostics_printf(
        "[process] Ring-3 exec image replacement test passed: PID %u\n",
        exec_test_pid
    );
}
