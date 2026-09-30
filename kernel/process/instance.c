// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file instance.c
 * @brief Owned process-instance lifecycle implementation.
 */

#include "instance.h"

#include "cwd.h"

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

    if (!process_fd_table_initialize(
        &instance->file_descriptors
    )) {
        return false;
    }

    instance->current_directory = NULL;

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

bool process_instance_prepare_clone(
    struct process_instance *instance,
    uint64_t id,
    const struct process_instance *source,
    const struct process_context *context)
{
    if (
        instance == NULL ||
        source == NULL ||
        context == NULL ||
        instance == source
    ) {
        return false;
    }

    if (
        source->process.instance != source ||
        source->process.image !=
            &source->image ||
        source->process.memory !=
            &source->image.memory ||
        source->process.layout !=
            &source->image.layout ||
        source->process.state ==
            PROCESS_STATE_TERMINATED
    ) {
        return false;
    }

    if (!process_fd_table_initialize(
        &instance->file_descriptors
    )) {
        return false;
    }

    instance->current_directory = NULL;

    if (!process_image_clone(
        &instance->image,
        &source->image
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
                "Unable to roll back failed cloned process instance"
            );
        }

        return false;
    }

    /*
     * process_init() validates the cloned layout and initializes the schedulable
     * descriptor. Fork resumes from the caller-supplied execution point rather
     * than from the ELF entry point.
     */
    instance->process.context =
        *context;

    if (!process_fd_table_clone(
        &instance->file_descriptors,
        &source->file_descriptors
    )) {
        if (!process_image_destroy(
            &instance->image
        )) {
            kernel_panic(
                "Unable to roll back failed cloned descriptor table"
            );
        }

        instance->process.memory = NULL;
        instance->process.layout = NULL;

        return false;
    }

    if (!process_cwd_inherit(
        instance,
        source
    )) {
        if (!process_fd_table_release_all(
            &instance->file_descriptors
        )) {
            kernel_panic(
                "Unable to roll back cloned descriptor table after CWD failure"
            );
        }

        if (!process_image_destroy(
            &instance->image
        )) {
            kernel_panic(
                "Unable to roll back cloned image after CWD failure"
            );
        }

        instance->process.memory = NULL;
        instance->process.layout = NULL;

        return false;
    }

    instance->process.instance =
        instance;

    instance->process.image =
        &instance->image;

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
