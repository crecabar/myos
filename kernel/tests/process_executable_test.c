// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file process_executable_test.c
 * @brief VFS-backed executable source regression tests.
 */

#include "process_executable_test.h"

#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../elf/elf64.h"
#include "../process/executable.h"
#include "../process/namespace.h"
#include "../vfs/root.h"
#include "../vfs/vfs.h"

#include <stddef.h>
#include <stdint.h>

void process_executable_test_run(
    const struct vfs_mount_table *mounts)
{
    if (mounts == NULL) {
        kernel_panic(
            "Executable source test received NULL mount topology"
        );
    }

    struct vfs_node *root =
        vfs_root_get();

    if (root == NULL) {
        kernel_panic(
            "Executable source test has no system root"
        );
    }

    size_t root_reference_baseline =
        root->reference_count;

    /*
     * The system bootstrap must be able to acquire /init before any userspace
     * process exists. Exercise the explicit-namespace acquisition path without
     * manufacturing a process instance solely for pathname resolution.
     */
    struct process_executable init_executable = {0};

    if (
        process_executable_open_elf64_from_namespace(
            root,
            mounts,
            "/init",
            sizeof("/init") - 1U,
            &init_executable
        ) !=
            PROCESS_EXECUTABLE_RESULT_SUCCESS
    ) {
        kernel_panic(
            "Unable to open bootstrap /init executable"
        );
    }

    if (
        init_executable.file == NULL ||
        init_executable.file->node == NULL ||
        init_executable.file->node->type !=
            VFS_NODE_TYPE_REGULAR_FILE ||
        init_executable.image.source_read == NULL ||
        init_executable.image.source_context !=
            &init_executable.file ||
        init_executable.image.load_segment_count == 0
    ) {
        kernel_panic(
            "Bootstrap /init executable state is invalid"
        );
    }

    if (!process_executable_close(
        &init_executable
    )) {
        kernel_panic(
            "Unable to close bootstrap /init executable"
        );
    }

    if (
        init_executable.file != NULL ||
        init_executable.image.source_context != NULL ||
        init_executable.image.source_read != NULL ||
        init_executable.image.size != 0
    ) {
        kernel_panic(
            "Closed bootstrap /init retained backing state"
        );
    }

    if (
        root->reference_count !=
            root_reference_baseline
    ) {
        kernel_panic(
            "Bootstrap /init executable acquisition leaked root ownership"
        );
    }

    struct process_instance instance = {0};

    if (!process_namespace_root_set(
        &instance,
        root,
        mounts
    )) {
        kernel_panic(
            "Unable to install executable test namespace"
        );
    }

    if (
        root->reference_count !=
        root_reference_baseline + 1
    ) {
        kernel_panic(
            "Executable test namespace ownership is incorrect"
        );
    }

    struct process_executable executable = {0};

    if (
        process_executable_open_elf64(
            &instance,
            "/bin/elf-from-vfs",
            sizeof("/bin/elf-from-vfs") - 1U,
            &executable
        ) !=
            PROCESS_EXECUTABLE_RESULT_SUCCESS
    ) {
        kernel_panic(
            "Unable to open VFS-backed ELF executable"
        );
    }

    if (
        executable.file == NULL ||
        executable.file->node == NULL ||
        executable.file->node->type !=
            VFS_NODE_TYPE_REGULAR_FILE ||
        executable.image.source_read == NULL ||
        executable.image.source_context !=
            &executable.file ||
        executable.image.size == 0 ||
        executable.image.load_segment_count == 0
    ) {
        kernel_panic(
            "VFS-backed ELF executable has invalid state"
        );
    }

    uint64_t original_offset =
        executable.file->offset;

    uint8_t magic[4] = {0};

    if (
        !elf64_image_read(
            &executable.image,
            0,
            magic,
            sizeof(magic)
        ) ||
        magic[0] != ELF64_MAGIC_0 ||
        magic[1] != ELF64_MAGIC_1 ||
        magic[2] != ELF64_MAGIC_2 ||
        magic[3] != ELF64_MAGIC_3 ||
        executable.file->offset !=
            original_offset
    ) {
        kernel_panic(
            "VFS-backed ELF source read contract failed"
        );
    }

    if (!process_executable_close(
        &executable
    )) {
        kernel_panic(
            "Unable to close VFS-backed ELF executable"
        );
    }

    if (
        executable.file != NULL ||
        executable.image.source_context != NULL ||
        executable.image.source_read != NULL ||
        executable.image.size != 0
    ) {
        kernel_panic(
            "Closed executable retained backing state"
        );
    }

    /*
     * Directories are resolvable filesystem objects but are never valid
     * executable backing files.
     */
    struct process_executable directory = {0};

    if (
        process_executable_open_elf64(
            &instance,
            "/bin",
            sizeof("/bin") - 1U,
            &directory
        ) !=
            PROCESS_EXECUTABLE_RESULT_NOT_REGULAR_FILE ||
        directory.file != NULL ||
        directory.image.source_read != NULL
    ) {
        kernel_panic(
            "Executable source accepted directory backing"
        );
    }

    if (!process_namespace_root_release(
        &instance
    )) {
        kernel_panic(
            "Unable to release executable test namespace"
        );
    }

    if (
        root->reference_count !=
        root_reference_baseline
    ) {
        kernel_panic(
            "Executable source test leaked namespace ownership"
        );
    }

    diagnostics_write(
        "[process] VFS-backed ELF executable source test passed\n"
    );
}
