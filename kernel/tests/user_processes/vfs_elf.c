// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file vfs_elf.c
 * @brief Ring-3 execution regression for an ELF loaded from VFS.
 */

#include "vfs_elf.h"

#include "../../core/panic.h"
#include "../../diagnostics/diagnostics.h"
#include "../../memory/heap.h"
#include "../../memory/memory.h"
#include "../../process/create.h"
#include "../../process/executable.h"
#include "../../process/instance.h"
#include "../../process/namespace.h"
#include "../../process/process.h"
#include "../../scheduler/scheduler.h"
#include "../../vfs/root.h"
#include "../../vfs/vfs.h"

#include <stddef.h>
#include <stdint.h>

// Static local variables
static struct process_instance *vfs_elf_test_instance;

static uint64_t vfs_elf_test_free_frame_baseline;

static struct kernel_heap_stats vfs_elf_test_heap_baseline;

static size_t vfs_elf_test_scheduler_count_baseline;

static uint64_t vfs_elf_test_pid;

// Public functions implementations
void user_process_vfs_elf_test_prepare(
    const struct vfs_mount_table *mounts)
{
    if (mounts == NULL) {
        kernel_panic(
            "VFS ELF execution test received NULL mount topology"
        );
    }

    if (vfs_elf_test_instance != NULL) {
        kernel_panic(
            "VFS ELF execution test already has an active process"
        );
    }

    vfs_elf_test_free_frame_baseline =
        physical_free_frame_count();

    if (!kernel_heap_stats_get(
        &vfs_elf_test_heap_baseline
    )) {
        kernel_panic(
            "Unable to read heap baseline before VFS ELF execution test"
        );
    }

    vfs_elf_test_scheduler_count_baseline =
        scheduler_test_process_count();

    struct vfs_node *root =
        vfs_root_get();

    if (root == NULL) {
        kernel_panic(
            "VFS ELF execution test has no system root"
        );
    }

    size_t root_reference_baseline =
        root->reference_count;

    /*
     * The resolver instance exists only to provide namespace context while the
     * executable pathname is resolved. The created process image does not
     * borrow this instance or its namespace ownership.
     */
    struct process_instance resolver = {0};

    if (!process_namespace_root_set(
        &resolver,
        root,
        mounts
    )) {
        kernel_panic(
            "Unable to install VFS ELF execution namespace"
        );
    }

    struct process_executable executable = {0};

    enum process_executable_result executable_result =
        process_executable_open_elf64(
            &resolver,
            "/bin/elf-from-vfs",
            sizeof("/bin/elf-from-vfs") - 1U,
            &executable
        );

    if (
        executable_result !=
        PROCESS_EXECUTABLE_RESULT_SUCCESS
    ) {
        if (!process_namespace_root_release(
            &resolver
        )) {
            kernel_panic(
                "Unable to roll back VFS ELF resolver namespace"
            );
        }

        kernel_panic(
            "Unable to open Ring-3 ELF executable from VFS"
        );
    }

    const char *argv[] = {
        "elf-from-vfs",
    };

    /*
     * process_create_elf64() materializes PT_LOAD bytes synchronously. The
     * backing VFS file must therefore remain open through this call, but is no
     * longer required once the process image has been created successfully.
     */
    vfs_elf_test_instance =
        process_create_elf64(
            &executable.image,
            1,
            argv,
            0,
            NULL
        );

    if (vfs_elf_test_instance == NULL) {
        if (!process_executable_close(
            &executable
        )) {
            kernel_panic(
                "Unable to close VFS ELF executable after process creation failure"
            );
        }

        if (!process_namespace_root_release(
            &resolver
        )) {
            kernel_panic(
                "Unable to release VFS ELF resolver after process creation failure"
            );
        }

        kernel_panic(
            "Unable to create Ring-3 process from VFS ELF executable"
        );
    }

    vfs_elf_test_pid =
        vfs_elf_test_instance->process.id;

    if (!process_executable_close(
        &executable
    )) {
        kernel_panic(
            "Unable to close VFS ELF executable after image materialization"
        );
    }

    if (!process_namespace_root_release(
        &resolver
    )) {
        kernel_panic(
            "Unable to release VFS ELF resolver namespace"
        );
    }

    /*
     * The executable and temporary resolver must no longer retain VFS
     * ownership once the process image has been materialized.
     */
    if (
        root->reference_count !=
        root_reference_baseline
    ) {
        kernel_panic(
            "VFS ELF execution preparation leaked root ownership"
        );
    }

    if (
        scheduler_test_process_count() !=
        vfs_elf_test_scheduler_count_baseline + 1
    ) {
        kernel_panic(
            "VFS ELF execution process was not registered exactly once"
        );
    }
}

void user_process_vfs_elf_test_terminated(
    struct process *process)
{
    if (
        vfs_elf_test_instance == NULL ||
        process == NULL ||
        process !=
            &vfs_elf_test_instance->process
    ) {
        kernel_panic(
            "VFS ELF execution test received unexpected process"
        );
    }

    if (
        process->id !=
            vfs_elf_test_pid ||
        process->state !=
            PROCESS_STATE_TERMINATED ||
        process->termination_reason !=
            PROCESS_TERMINATION_EXITED ||
        process->exit_status != 0
    ) {
        kernel_panic(
            "VFS-backed Ring-3 ELF produced unexpected result"
        );
    }

    if (!process_release_terminated(
        vfs_elf_test_instance
    )) {
        kernel_panic(
            "Unable to release VFS-backed Ring-3 ELF process"
        );
    }

    vfs_elf_test_instance =
        NULL;

    if (
        scheduler_test_process_count() !=
        vfs_elf_test_scheduler_count_baseline
    ) {
        kernel_panic(
            "VFS-backed Ring-3 ELF leaked scheduler registration"
        );
    }

    if (
        physical_free_frame_count() !=
        vfs_elf_test_free_frame_baseline
    ) {
        kernel_panic(
            "VFS-backed Ring-3 ELF leaked physical frames"
        );
    }

    struct kernel_heap_stats heap_after;

    if (!kernel_heap_stats_get(
        &heap_after
    )) {
        kernel_panic(
            "Unable to read heap after VFS ELF execution test"
        );
    }

    if (
        heap_after.allocated_block_count !=
            vfs_elf_test_heap_baseline.allocated_block_count ||
        heap_after.allocated_bytes !=
            vfs_elf_test_heap_baseline.allocated_bytes
    ) {
        kernel_panic(
            "VFS-backed Ring-3 ELF leaked kernel heap allocations"
        );
    }

    diagnostics_printf(
        "[process] VFS-backed Ring-3 ELF execution test passed: PID %u\n",
        vfs_elf_test_pid
    );
}
