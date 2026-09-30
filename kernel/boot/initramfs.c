// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include "initramfs.h"

#include <stdbool.h>
#include <stddef.h>

// Defines
#define BOOT_INITRAMFS_COMMAND_LINE "initramfs"

// Private functions and helpers declarations
static bool boot_initramfs_text_equal(
    const char *left,
    const char *right
);

// Public functions implementations
bool boot_initramfs_find(
    const struct boot_info *boot_info,
    const struct boot_module **module)
{
    if (
        boot_info == NULL ||
        module == NULL
    ) {
        return false;
    }

    const struct boot_module *found =
        NULL;

    for (
        size_t index = 0;
        index < boot_info->module_count;
        ++index
    ) {
        const struct boot_module *candidate =
            &boot_info->modules[index];

        if (!boot_initramfs_text_equal(
            candidate->command_line,
            BOOT_INITRAMFS_COMMAND_LINE
        )) {
            continue;
        }

        if (found != NULL) {
            return false;
        }

        found =
            candidate;
    }

    if (found == NULL) {
        return false;
    }

    *module =
        found;

    return true;
}

// Private functions and helpers implementations
static bool boot_initramfs_text_equal(
    const char *left,
    const char *right)
{
    if (
        left == NULL ||
        right == NULL
    ) {
        return false;
    }

    size_t index = 0;

    while (
        left[index] != '\0' &&
        right[index] != '\0'
    ) {
        if (
            left[index] !=
            right[index]
        ) {
            return false;
        }

        ++index;
    }

    return
        left[index] ==
        right[index];
}
