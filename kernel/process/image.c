// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file image.c
 * @brief Owned executable image lifecycle implementation.
 */

#include "image.h"

#include <stddef.h>

bool process_image_create_elf64(
    struct process_image *image,
    const struct elf64_image *elf,
    size_t argc,
    const char *const argv[],
    size_t envc,
    const char *const envp[])
{
    if (image == NULL) return false;
    if (elf == NULL) return false;

    if (!process_memory_create(&image->memory)) {
        return false;
    }

    if (!process_layout_create_elf64(
        &image->memory,
        elf,
        argc,
        argv,
        envc,
        envp,
        &image->layout
    )) {
        if (!process_memory_destroy(&image->memory)) {
            return false;
        }

        return false;
    }

    return true;
}

bool process_image_destroy(struct process_image *image)
{
    if (image == NULL) return false;

    if (!process_layout_destroy(
        &image->memory,
        &image->layout
    )) {
        return false;
    }

    if (!process_memory_destroy(
        &image->memory
    )) {
        return false;
    }

    return true;
}
