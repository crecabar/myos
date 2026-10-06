// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file instance.h
 * @brief Owned lifecycle storage for one MyOS userspace process.
 *
 * A process instance owns the schedulable process descriptor, the executable
 * image referenced by that descriptor, the process file descriptor table, and
 * an optional namespace-root reference and an optional reference to its current
 * working directory.
 *
 * Scheduler registration remains separate. The scheduler may borrow the
 * embedded process descriptor but never owns the instance, its image, or its
 * file descriptor references.
 */

#ifndef MYOS_PROCESS_INSTANCE_H
#define MYOS_PROCESS_INSTANCE_H

#include "../elf/elf64.h"
#include "fd_table.h"
#include "image.h"
#include "process.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct vfs_mount_table;

/**
 * Owns the lifecycle storage of one userspace process.
 *
 * The embedded process descriptor borrows the memory and layout contained in
 * image.
 *
 * The instance may also own one reference to a VFS directory representing its
 * namespace root and one independent reference to a VFS directory representing
 * its current working directory.
 *
 * namespace_mounts is a borrowed pointer to the VFS mount topology associated
 * with namespace_root. The process does not own that table, so it must remain
 * alive while the namespace context references it.
 *
 * Therefore the instance must remain alive while the process may be referenced
 * by the scheduler.
 */
 struct process_instance {
    struct process process;
    struct process_image image;
    struct process_fd_table file_descriptors;

    struct vfs_node *namespace_root;

    const struct vfs_mount_table *namespace_mounts;

    struct vfs_node *current_directory;

    /*
     * Intrusive process-family links.
     *
     * These links describe lifecycle ownership only. The scheduler does not
     * inspect or own them.
     */
    struct process_instance *parent;
    struct process_instance *first_child;
    struct process_instance *next_sibling;

    /*
     * Active parent wait request.
     *
     * child_pid == 0 means any direct child.
     *
     * The request remains active while the parent is BLOCKED and is cleared
     * when the matching terminated child is reaped or the request is
     * explicitly cancelled.
     */
    bool wait_active;
    uint64_t wait_child_pid;
};

/**
 * Prepares an ELF-backed process instance.
 *
 * A complete executable image is created first. The process descriptor is then
 * initialized to borrow that image's memory and layout.
 *
 * This function does not register the process with the scheduler and does not
 * allocate the supplied process identifier.
 *
 * On failure, all resources acquired during preparation are released before
 * returning.
 *
 * @param instance Instance storage to initialize.
 * @param id Process identifier supplied by the lifecycle layer.
 * @param elf Parsed ELF64 executable.
 * @param argc Number of argv strings.
 * @param argv Argument strings, or NULL when argc is zero.
 * @param envc Number of environment strings.
 * @param envp Environment strings, or NULL when envc is zero.
 *
 * @return true when the complete process instance was prepared; false
 * otherwise.
 */
bool process_instance_prepare_elf64(
    struct process_instance *instance,
    uint64_t id,
    const struct elf64_image *elf,
    size_t argc,
    const char *const argv[],
    size_t envc,
    const char *const envp[]
);

/**
 * Prepares a process instance as an independent clone of another instance.
 *
 * The source executable image is deep-copied into a new address space. The
 * supplied execution context becomes the initial resumable context of the
 * clone.
 *
 * Occupied file descriptors are inherited with the same descriptor numbers.
 * Parent and child retain the same vfs_file objects and therefore share
 * per-open state such as the current file offset.
 *
 * The namespace root and current working directory are inherited as the same
 * VFS directory nodes referenced by the source, with the clone owning one
 * independent reference to each non-NULL node.
 *
 * Process-family links and wait state are initialized empty. Scheduler
 * registration and publication as a child remain the caller's responsibility.
 *
 * This function does not allocate the supplied process identifier.
 *
 * On failure, every resource acquired for the clone is released before
 * returning.
 *
 * @param instance Destination instance storage.
 * @param id Process identifier supplied by the lifecycle layer.
 * @param source Existing process instance whose image is cloned.
 * @param context Execution context to install in the clone.
 *
 * @return true when the complete instance was prepared; false otherwise.
 */
bool process_instance_prepare_clone(
    struct process_instance *instance,
    uint64_t id,
    const struct process_instance *source,
    const struct process_context *context
);

/**
 * Releases an unregistered prepared process instance.
 *
 * This function is intended for rollback before scheduler ownership has been
 * established. A process that has run and terminated must instead be reclaimed
 * through the normal terminated-process lifecycle.
 *
 * @param instance Prepared instance that is not registered with the scheduler.
 *
 * @return true when all descriptor, namespace, current-directory and image
 * resources were released; false otherwise.
 */
bool process_instance_discard(
    struct process_instance *instance
);

#endif
