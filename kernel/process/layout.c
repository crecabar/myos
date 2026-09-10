// SPDX-License-Identifier: GPL-2.0-only

#include "layout.h"

#include <stddef.h>

bool process_layout_create(
    struct process_memory *memory,
    struct process_layout *layout)
{
    if (memory == NULL) return false;
    if (layout == NULL) return false;

    if (!process_memory_allocate_executable_page(
        memory,
        PROCESS_LAYOUT_CODE_BASE
    )) {
        return false;
    }

    if (!process_stack_create(
        memory,
        &layout->stack,
        PROCESS_LAYOUT_STACK_TOP,
        PROCESS_LAYOUT_STACK_PAGES
    )) {
        process_memory_release_page(
            memory,
            PROCESS_LAYOUT_CODE_BASE
        );

        return false;
    }

    layout->code_base = PROCESS_LAYOUT_CODE_BASE;
    layout->entry_point = PROCESS_LAYOUT_CODE_BASE;

    return true;
}

bool process_layout_destroy(
    struct process_memory *memory,
    struct process_layout *layout)
{
    if (memory == NULL) return false;
    if (layout == NULL) return false;

    if (!process_stack_destroy(
        memory,
        &layout->stack
    )) {
        return false;
    }

    if (!process_memory_release_page(
        memory,
        layout->code_base
    )) {
        return false;
    }

    layout->code_base = 0;
    layout->entry_point = 0;

    return true;
}
