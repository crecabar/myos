// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file exec.c
 * @brief Transactional executable-image replacement preparation.
 */

#include "exec.h"

#include "../core/panic.h"

#include <stddef.h>

bool process_exec_candidate_prepare_elf64(
    struct process_exec_candidate *candidate,
    const struct elf64_image *elf,
    size_t argc,
    const char *const argv[],
    size_t envc,
    const char *const envp[])
{
    if (candidate == NULL) return false;
    if (elf == NULL) return false;
    if (candidate->prepared) return false;

    if (!process_image_create_elf64(
        &candidate->image,
        elf,
        argc,
        argv,
        envc,
        envp
    )) {
        return false;
    }

    if (!process_context_initialize(
        &candidate->context,
        &candidate->image.layout
    )) {
        if (!process_image_destroy(
            &candidate->image
        )) {
            kernel_panic(
                "Unable to roll back failed exec candidate"
            );
        }

        return false;
    }

    candidate->prepared = true;

    return true;
}

bool process_exec_candidate_discard(
    struct process_exec_candidate *candidate)
{
    if (candidate == NULL) return false;
    if (!candidate->prepared) return false;

    if (!process_image_destroy(
        &candidate->image
    )) {
        return false;
    }

    candidate->prepared = false;

    return true;
}
