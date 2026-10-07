// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file create.c
 * @brief Dynamic userspace process creation implementation.
 */

#include "create.h"

#include "cwd.h"
#include "fd_table.h"
#include "namespace.h"
#include "pid.h"

#include "../core/panic.h"
#include "../memory/heap.h"
#include "../scheduler/scheduler.h"

#include <stddef.h>

static struct process_instance *process_create_elf64_internal(
    struct process_instance *parent,
    const struct process_initial_vfs *initial_vfs,
    const struct elf64_image *elf,
    size_t argc,
    const char *const argv[],
    size_t envc,
    const char *const envp[]
);

static void process_create_rollback_prepublication(
    struct process_instance *instance,
    uint64_t id
);

struct process_instance *process_create_elf64(
    const struct elf64_image *elf,
    size_t argc,
    const char *const argv[],
    size_t envc,
    const char *const envp[])
{
    return process_create_elf64_internal(
        NULL,
        NULL,
        elf,
        argc,
        argv,
        envc,
        envp
    );
}

struct process_instance *process_create_elf64_with_vfs(
    const struct elf64_image *elf,
    const struct process_initial_vfs *initial_vfs,
    size_t argc,
    const char *const argv[],
    size_t envc,
    const char *const envp[])
{
    if (
        initial_vfs == NULL ||
        initial_vfs->namespace_root == NULL
    ) {
        return NULL;
    }

    return process_create_elf64_internal(
        NULL,
        initial_vfs,
        elf,
        argc,
        argv,
        envc,
        envp
    );
}

struct process_instance *process_create_child_elf64(
    struct process_instance *parent,
    const struct elf64_image *elf,
    size_t argc,
    const char *const argv[],
    size_t envc,
    const char *const envp[])
{
    if (parent == NULL) return NULL;

    if (
        parent->process.instance != parent ||
        parent->process.state ==
            PROCESS_STATE_TERMINATED
    ) {
        return NULL;
    }

    return process_create_elf64_internal(
        parent,
        NULL,
        elf,
        argc,
        argv,
        envc,
        envp
    );
}

bool process_release_terminated(
    struct process_instance *instance)
{
    if (instance == NULL) return false;

    if (
        instance->process.state !=
        PROCESS_STATE_TERMINATED
    ) {
        return false;
    }

    if (
        instance->process.instance !=
        instance
    ) {
        return false;
    }

    if (
        instance->parent != NULL ||
        instance->first_child != NULL ||
        instance->next_sibling != NULL ||
        instance->wait_active ||
        instance->wait_child_pid != 0
    ) {
        return false;
    }

    /*
     * Normal process termination releases descriptor, current-directory and
     * namespace-root ownership during lifecycle notification. These releases are
     * idempotent so direct teardown and synthetic test paths remain deterministic
     * when no notification was required.
     */
    if (!process_fd_table_release_all(
        &instance->file_descriptors
    )) {
        return false;
    }

    if (!process_cwd_release(
        instance
    )) {
        return false;
    }

    if (!process_namespace_root_release(
        instance
    )) {
        return false;
    }

    if (!process_reclaim_resources(&instance->process)) {
        return false;
    }

    instance->process.instance = NULL;

    kfree(instance);

    return true;
}

static struct process_instance *process_create_elf64_internal(
    struct process_instance *parent,
    const struct process_initial_vfs *initial_vfs,
    const struct elf64_image *elf,
    size_t argc,
    const char *const argv[],
    size_t envc,
    const char *const envp[])
{
    if (elf == NULL) return NULL;

    uint64_t id;

    if (!process_pid_allocate(&id)) {
        return NULL;
    }

    struct process_instance *instance =
        kmalloc(sizeof(*instance));

    if (instance == NULL) {
        if (!process_pid_release(id)) {
            kernel_panic(
                "Unable to roll back process identifier allocation"
            );
        }

        return NULL;
    }

    if (!process_instance_prepare_elf64(
        instance,
        id,
        elf,
        argc,
        argv,
        envc,
        envp
    )) {
        kfree(instance);

        if (!process_pid_release(id)) {
            kernel_panic(
                "Unable to roll back process identifier allocation"
            );
        }

        return NULL;
    }

    if (initial_vfs != NULL) {
        if (!process_namespace_root_set(
            instance,
            initial_vfs->namespace_root,
            initial_vfs->namespace_mounts
        )) {
            process_create_rollback_prepublication(
                instance,
                id
            );

            return NULL;
        }

        /*
         * The initial working directory is deliberately the namespace root.
         * This guarantees containment without accepting an arbitrary VFS node
         * whose membership in the namespace has not been established.
         */
        if (!process_cwd_set(
            instance,
            initial_vfs->namespace_root
        )) {
            process_create_rollback_prepublication(
                instance,
                id
            );

            return NULL;
        }
    }

    if (!scheduler_add(
        &instance->process
    )) {
        process_create_rollback_prepublication(
            instance,
            id
        );

        return NULL;
    }

    if (parent != NULL) {
        instance->parent =
            parent;

        instance->next_sibling =
            parent->first_child;

        parent->first_child =
            instance;
    }

    return instance;
}

static void process_create_rollback_prepublication(
    struct process_instance *instance,
    uint64_t id)
{
    if (instance == NULL) {
        kernel_panic(
            "Process creation rollback received null instance"
        );
    }

    if (!process_instance_discard(
        instance
    )) {
        kernel_panic(
            "Unable to roll back unpublished process instance"
        );
    }

    kfree(instance);

    if (!process_pid_release(
        id
    )) {
        kernel_panic(
            "Unable to roll back unpublished process identifier"
        );
    }
}
