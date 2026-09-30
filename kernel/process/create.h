// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file create.h
 * @brief Dynamic userspace process creation.
 *
 * Provides the lifecycle orchestration required to allocate a process instance,
 * materialize an executable image, initialize its process descriptor, and
 * register the resulting READY process with the scheduler.
 */

#ifndef MYOS_PROCESS_CREATE_H
#define MYOS_PROCESS_CREATE_H

#include "../elf/elf64.h"
#include "instance.h"

#include <stddef.h>
#include <stdint.h>

/**
 * Dynamically creates and schedules an ELF64 userspace process.
 *
 * Lifecycle storage is allocated from the kernel heap. A complete ELF-backed
 * process instance is prepared and then registered with the scheduler.
 *
 * A process identifier is allocated internally through the central process PID
 * allocator before lifecycle storage and executable resources are created.
 *
 * Process identifiers are monotonic once a process has been published to the
 * scheduler. An identifier allocated during a failed creation attempt is
 * rolled back before returning.
 *
 * On failure, all resources acquired during this operation are released before
 * returning.
 *
 * On success, the returned process instance remains owned by the process
 * lifecycle layer. The scheduler stores only a borrowed pointer to its embedded
 * process descriptor.
 *
 * @param elf Parsed ELF64 executable.
 * @param argc Number of argv strings.
 * @param argv Argument strings, or NULL when argc is zero.
 * @param envc Number of environment strings.
 * @param envp Environment strings, or NULL when envc is zero.
 *
 * @return Owned process instance when creation and scheduler registration
 * succeed; NULL otherwise.
 */
struct process_instance *process_create_elf64(
    const struct elf64_image *elf,
    size_t argc,
    const char *const argv[],
    size_t envc,
    const char *const envp[]
);

/**
 * Dynamically creates and schedules an ELF64 child process.
 *
 * The new process becomes a direct child of parent only after its complete
 * image has been prepared and scheduler registration has succeeded.
 *
 * Failed creation therefore never publishes a partial parent/child
 * relationship.
 *
 * @param parent Existing dynamic parent process.
 * @param elf Parsed ELF64 executable.
 * @param argc Number of argv strings.
 * @param argv Argument strings, or NULL when argc is zero.
 * @param envc Number of environment strings.
 * @param envp Environment strings, or NULL when envc is zero.
 *
 * @return Owned child instance on success; NULL otherwise.
 */
struct process_instance *process_create_child_elf64(
    struct process_instance *parent,
    const struct elf64_image *elf,
    size_t argc,
    const char *const argv[],
    size_t envc,
    const char *const envp[]
);

/**
 * Releases a dynamically allocated terminated process instance.
 *
 * The scheduler must already have detached its borrowed registration before
 * this function is called.
 *
 * Any remaining descriptor-owned open-file references and current-directory
 * node reference are released before the process executable resources are
 * reclaimed. Normal scheduler-driven termination already releases these VFS
 * resources during lifecycle notification, so this also covers direct teardown
 * and synthetic lifecycle paths safely.
 *
 * The lifecycle storage allocated by process_create_elf64() is then returned
 * to the kernel heap.
 *
 * @param instance Detached terminated dynamic process instance.
 *
 * @return true when all process resources and lifecycle storage were released;
 * false when the instance is invalid or cannot be reclaimed safely.
 */
bool process_release_terminated(
    struct process_instance *instance
);

#endif
