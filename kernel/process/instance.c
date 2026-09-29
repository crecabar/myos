// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file instance.c
 * @brief Owned process-instance lifecycle implementation.
 */

#include "instance.h"

#include "../core/panic.h"

#include <stddef.h>

bool process_instance_prepare_elf64(
    struct process_instance *instance,
    uint64_t id,
    const struct elf64_image *elf,
    size_t argc,
    const char *const argv[],
    size_t envc,
    const char *const envp[])
{
    if (instance == NULL) return false;
    if (elf == NULL) return false;

    if (!process_image_create_elf64(
        &instance->image,
        elf,
        argc,
        argv,
        envc,
        envp
    )) {
        return false;
    }

    instance->parent = NULL;
    instance->first_child = NULL;
    instance->next_sibling = NULL;

    instance->wait_active = false;
    instance->wait_child_pid = 0;

    if (!process_init(
        &instance->process,
        id,
        &instance->image.memory,
        &instance->image.layout
    )) {
        if (!process_image_destroy(
            &instance->image
        )) {
            kernel_panic(
                "Unable to roll back failed process instance preparation"
            );
        }

        return false;
    }

    instance->process.instance = instance;
    instance->process.image = &instance->image;

    return true;
}

bool process_instance_discard(
    struct process_instance *instance)
{
    if (instance == NULL) return false;

    if (
        instance->process.state !=
        PROCESS_STATE_READY
    ) {
        return false;
    }

    if (
        instance->process.image !=
            &instance->image ||
        instance->process.memory !=
            &instance->image.memory ||
        instance->process.layout !=
            &instance->image.layout
    ) {
        return false;
    }

    if (
        instance->process.instance != instance ||
        instance->parent != NULL ||
        instance->first_child != NULL ||
        instance->next_sibling != NULL ||
        instance->wait_active ||
        instance->wait_child_pid != 0
    ) {
        return false;
    }

    if (!process_image_destroy(
        &instance->image
    )) {
        return false;
    }

    instance->process.instance = NULL;
    instance->process.image = NULL;
    instance->process.memory = NULL;
    instance->process.layout = NULL;

    return true;
}
