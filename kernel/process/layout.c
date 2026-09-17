// SPDX-License-Identifier: GPL-2.0-only

#include "layout.h"

#include <stddef.h>

static void process_layout_clear(
    struct process_layout *layout
);

static void process_layout_clear(
    struct process_layout *layout)
{
    layout->kind = PROCESS_LAYOUT_KIND_NONE;

    layout->code_base = 0;
    layout->entry_point = 0;
    layout->initial_rsp = 0;

    layout->stack.guard_address = 0;
    layout->stack.base_address = 0;
    layout->stack.stack_top = 0;
    layout->stack.page_count = 0;

    layout->loaded_image.segments = NULL;
    layout->loaded_image.segment_count = 0;
}

bool process_layout_create(
    struct process_memory *memory,
    struct process_layout *layout)
{
    if (memory == NULL) return false;
    if (layout == NULL) return false;

    process_layout_clear(layout);

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
        (void) process_memory_release_page(
            memory,
            PROCESS_LAYOUT_CODE_BASE
        );

        process_layout_clear(layout);

        return false;
    }

    layout->kind = PROCESS_LAYOUT_KIND_LEGACY;
    layout->code_base = PROCESS_LAYOUT_CODE_BASE;
    layout->entry_point = PROCESS_LAYOUT_CODE_BASE;
    layout->initial_rsp = layout->stack.stack_top;

    return true;
}

bool process_layout_create_elf64(
    struct process_memory *memory,
    const struct elf64_image *image,
    size_t argc,
    const char *const argv[],
    size_t envc,
    const char *const envp[],
    struct process_layout *layout)
{
    if (memory == NULL) return false;
    if (image == NULL) return false;
    if (layout == NULL) return false;

    process_layout_clear(layout);

    if (!elf64_entry_point_validate(image)) {
        return false;
    }

    if (!elf64_load_image(
        image,
        memory,
        &layout->loaded_image
    )) {
        return false;
    }

    if (!process_stack_create(
        memory,
        &layout->stack,
        PROCESS_LAYOUT_STACK_TOP,
        PROCESS_LAYOUT_STACK_PAGES
    )) {
        (void) elf64_unload_image(
            memory,
            &layout->loaded_image
        );

        process_layout_clear(layout);

        return false;
    }

    uint64_t initial_rsp;

    if (!process_stack_build_initial(
        memory,
        &layout->stack,
        argc,
        argv,
        envc,
        envp,
        &initial_rsp
    )) {
        (void) process_stack_destroy(
            memory,
            &layout->stack
        );

        (void) elf64_unload_image(
            memory,
            &layout->loaded_image
        );

        process_layout_clear(layout);

        return false;
    }

    layout->kind = PROCESS_LAYOUT_KIND_ELF64;
    layout->entry_point = image->entry_point;
    layout->initial_rsp = initial_rsp;

    return true;
}

bool process_layout_destroy(
    struct process_memory *memory,
    struct process_layout *layout)
{
    if (memory == NULL) return false;
    if (layout == NULL) return false;

    if (
        layout->kind != PROCESS_LAYOUT_KIND_LEGACY &&
        layout->kind != PROCESS_LAYOUT_KIND_ELF64
    ) {
        return false;
    }

    if (layout->stack.page_count != 0) {
        if (!process_stack_destroy(
            memory,
            &layout->stack
        )) {
            return false;
        }
    }

    if (
        layout->kind ==
        PROCESS_LAYOUT_KIND_LEGACY
    ) {
        if (layout->code_base != 0) {
            if (!process_memory_release_page(
                memory,
                layout->code_base
            )) {
                return false;
            }

            layout->code_base = 0;
        }
    } else {
        if (
            layout->loaded_image.segments != NULL ||
            layout->loaded_image.segment_count != 0
        ) {
            if (!elf64_unload_image(
                memory,
                &layout->loaded_image
            )) {
                return false;
            }
        }
    }

    process_layout_clear(layout);

    return true;
}
