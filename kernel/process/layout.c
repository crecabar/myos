// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include "layout.h"

#include "../core/panic.h"
#include "../memory/heap.h"

#include <stddef.h>

static void process_layout_clear(
    struct process_layout *layout
);

static bool process_layout_clone_range(
    struct process_memory *destination_memory,
    const struct process_memory *source_memory,
    uint64_t virtual_address,
    size_t page_count
);

static bool process_layout_release_range(
    struct process_memory *memory,
    uint64_t virtual_address,
    size_t page_count
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

static bool process_layout_clone_range(
    struct process_memory *destination_memory,
    const struct process_memory *source_memory,
    uint64_t virtual_address,
    size_t page_count)
{
    size_t cloned_pages = 0;

    for (
        size_t index = 0;
        index < page_count;
        ++index
    ) {
        uint64_t page_address =
            virtual_address +
            (uint64_t) index * 4096ULL;

        if (!process_memory_clone_page(
            destination_memory,
            source_memory,
            page_address
        )) {
            break;
        }

        ++cloned_pages;
    }

    if (cloned_pages == page_count) {
        return true;
    }

    for (
        size_t remaining = cloned_pages;
        remaining > 0;
        --remaining
    ) {
        uint64_t page_address =
            virtual_address +
            (uint64_t) (remaining - 1) *
            4096ULL;

        if (!process_memory_release_page(
            destination_memory,
            page_address
        )) {
            return false;
        }
    }

    return false;
}

static bool process_layout_release_range(
    struct process_memory *memory,
    uint64_t virtual_address,
    size_t page_count)
{
    bool released_all = true;

    for (
        size_t remaining = page_count;
        remaining > 0;
        --remaining
    ) {
        uint64_t page_address =
            virtual_address +
            (uint64_t) (remaining - 1) *
            4096ULL;

        if (!process_memory_release_page(
            memory,
            page_address
        )) {
            released_all = false;
        }
    }

    return released_all;
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

bool process_layout_clone(
    struct process_memory *destination_memory,
    const struct process_memory *source_memory,
    const struct process_layout *source_layout,
    struct process_layout *destination_layout)
{
    if (
        destination_memory == NULL ||
        source_memory == NULL ||
        source_layout == NULL ||
        destination_layout == NULL ||
        destination_memory == source_memory
    ) {
        return false;
    }

    process_layout_clear(
        destination_layout
    );

    /*
     * fork initially supports only the real ELF64 process model.
     * Legacy test layouts are deliberately outside this ownership path.
     */
    if (
        source_layout->kind !=
        PROCESS_LAYOUT_KIND_ELF64
    ) {
        return false;
    }

    if (
        source_layout->loaded_image.segments == NULL ||
        source_layout->loaded_image.segment_count == 0 ||
        source_layout->stack.page_count == 0
    ) {
        return false;
    }

    size_t segment_count =
        source_layout->loaded_image.segment_count;

    if (
        segment_count >
        SIZE_MAX /
            sizeof(struct elf64_load_segment)
    ) {
        return false;
    }

    size_t metadata_size =
        segment_count *
        sizeof(struct elf64_load_segment);

    struct elf64_load_segment *segments =
        kmalloc(metadata_size);

    if (segments == NULL) {
        return false;
    }

    for (
        size_t index = 0;
        index < segment_count;
        ++index
    ) {
        segments[index] =
            source_layout
                ->loaded_image
                .segments[index];
    }

    size_t cloned_segments = 0;

    for (
        size_t index = 0;
        index < segment_count;
        ++index
    ) {
        const struct elf64_load_segment *segment =
            &segments[index];

        if (!process_layout_clone_range(
            destination_memory,
            source_memory,
            segment->mapping_start,
            segment->page_count
        )) {
            break;
        }

        ++cloned_segments;
    }

    if (cloned_segments != segment_count) {
        for (
            size_t remaining = cloned_segments;
            remaining > 0;
            --remaining
        ) {
            const struct elf64_load_segment *segment =
                &segments[remaining - 1];

            if (!process_layout_release_range(
                destination_memory,
                segment->mapping_start,
                segment->page_count
            )) {
                kernel_panic(
                    "Unable to roll back cloned ELF segment"
                );
            }
        }

        kfree(segments);

        return false;
    }

    if (!process_layout_clone_range(
        destination_memory,
        source_memory,
        source_layout->stack.base_address,
        source_layout->stack.page_count
    )) {
        for (
            size_t remaining = segment_count;
            remaining > 0;
            --remaining
        ) {
            const struct elf64_load_segment *segment =
                &segments[remaining - 1];

            if (!process_layout_release_range(
                destination_memory,
                segment->mapping_start,
                segment->page_count
            )) {
                kernel_panic(
                    "Unable to roll back cloned ELF layout"
                );
            }
        }

        kfree(segments);

        return false;
    }

    destination_layout->kind =
        source_layout->kind;

    destination_layout->code_base =
        source_layout->code_base;

    destination_layout->entry_point =
        source_layout->entry_point;

    destination_layout->initial_rsp =
        source_layout->initial_rsp;

    destination_layout->stack =
        source_layout->stack;

    destination_layout->loaded_image.segments =
        segments;

    destination_layout->loaded_image.segment_count =
        segment_count;

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
