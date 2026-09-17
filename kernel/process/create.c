// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file create.c
 * @brief Dynamic userspace process creation implementation.
 */

#include "create.h"

#include "../memory/heap.h"
#include "../scheduler/scheduler.h"

#include <stddef.h>

struct process_instance *process_create_elf64(
    uint64_t id,
    const struct elf64_image *elf,
    size_t argc,
    const char *const argv[],
    size_t envc,
    const char *const envp[])
{
    if (elf == NULL) return NULL;

    struct process_instance *instance =
        kmalloc(sizeof(*instance));

    if (instance == NULL) {
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
        return NULL;
    }

    if (!scheduler_add(
        &instance->process
    )) {
        if (!process_instance_discard(
            instance
        )) {
            /*
             * Failure here means the lifecycle invariants are inconsistent:
             * scheduler_add() acquired no ownership, yet the freshly prepared
             * READY instance could not be rolled back safely.
             */
            return NULL;
        }

        kfree(instance);
        return NULL;
    }

    return instance;
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

    if (!process_reclaim_resources(
        &instance->process
    )) {
        return false;
    }

    kfree(instance);

    return true;
}
