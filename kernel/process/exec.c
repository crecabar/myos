// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file exec.c
 * @brief Transactional executable-image replacement preparation.
 */

#include "exec.h"

#include "../arch/x86_64/paging.h"
#include "../core/panic.h"

#include <stddef.h>

static void process_exec_swap_images(
    struct process_image *left,
    struct process_image *right
);

static void process_exec_copy_context(
    struct process_context *destination,
    const struct process_context *source
);

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

bool process_exec_candidate_commit_current(
    struct process *process,
    struct process_exec_candidate *candidate)
{
    if (process == NULL) return false;
    if (candidate == NULL) return false;
    if (!candidate->prepared) return false;

    if (
        process->state !=
        PROCESS_STATE_RUNNING
    ) {
        return false;
    }

    if (process->image == NULL) {
        return false;
    }

    if (
        process->memory !=
            &process->image->memory ||
        process->layout !=
            &process->image->layout
    ) {
        return false;
    }

    uint64_t old_cr3 =
        process->image
            ->memory
            .address_space
            .pml4_physical &
        PAGE_ADDRESS_MASK_4K;

    uint64_t new_cr3 =
        candidate->image
            .memory
            .address_space
            .pml4_physical &
        PAGE_ADDRESS_MASK_4K;

    if (
        old_cr3 == 0 ||
        new_cr3 == 0 ||
        old_cr3 == new_cr3
    ) {
        return false;
    }

    uint64_t active_cr3 =
        paging_read_cr3() &
        PAGE_ADDRESS_MASK_4K;

    if (active_cr3 != old_cr3) {
        return false;
    }

    /*
     * Commit point.
     *
     * Until this succeeds, the old process image remains completely intact.
     */
    if (!paging_address_space_activate(
        &candidate->image.memory.address_space
    )) {
        return false;
    }

    active_cr3 =
        paging_read_cr3() &
        PAGE_ADDRESS_MASK_4K;

    if (active_cr3 != new_cr3) {
        kernel_panic(
            "Exec activated unexpected address space"
        );
    }

    /*
     * Exchange ownership in place.
     *
     * process->image keeps the same metadata address, but that storage now
     * owns the replacement image. candidate->image temporarily owns the
     * displaced image so it can be reclaimed safely under the new CR3.
     */
    process_exec_swap_images(
        process->image,
        &candidate->image
    );

    process->memory =
        &process->image->memory;

    process->layout =
        &process->image->layout;

    process_exec_copy_context(
        &process->context,
        &candidate->context
    );

    if (!process_image_destroy(
        &candidate->image
    )) {
        kernel_panic(
            "Unable to destroy displaced exec image"
        );
    }

    candidate->prepared = false;

    return true;
}

static void process_exec_swap_images(
    struct process_image *left,
    struct process_image *right)
{
    uint64_t u64;
    uint64_t *pml4_virtual;
    bool boolean;
    enum process_layout_kind kind;
    size_t size;
    struct elf64_load_segment *segments;

    u64 = left->memory.address_space.pml4_physical;
    left->memory.address_space.pml4_physical =
        right->memory.address_space.pml4_physical;
    right->memory.address_space.pml4_physical = u64;

    pml4_virtual =
        left->memory.address_space.pml4_virtual;
    left->memory.address_space.pml4_virtual =
        right->memory.address_space.pml4_virtual;
    right->memory.address_space.pml4_virtual =
        pml4_virtual;

    boolean =
        left->memory.address_space.kernel_half_shared;
    left->memory.address_space.kernel_half_shared =
        right->memory.address_space.kernel_half_shared;
    right->memory.address_space.kernel_half_shared =
        boolean;

    kind = left->layout.kind;
    left->layout.kind = right->layout.kind;
    right->layout.kind = kind;

    u64 = left->layout.code_base;
    left->layout.code_base = right->layout.code_base;
    right->layout.code_base = u64;

    u64 = left->layout.entry_point;
    left->layout.entry_point =
        right->layout.entry_point;
    right->layout.entry_point = u64;

    u64 = left->layout.initial_rsp;
    left->layout.initial_rsp =
        right->layout.initial_rsp;
    right->layout.initial_rsp = u64;

    u64 = left->layout.stack.guard_address;
    left->layout.stack.guard_address =
        right->layout.stack.guard_address;
    right->layout.stack.guard_address = u64;

    u64 = left->layout.stack.base_address;
    left->layout.stack.base_address =
        right->layout.stack.base_address;
    right->layout.stack.base_address = u64;

    u64 = left->layout.stack.stack_top;
    left->layout.stack.stack_top =
        right->layout.stack.stack_top;
    right->layout.stack.stack_top = u64;

    size = left->layout.stack.page_count;
    left->layout.stack.page_count =
        right->layout.stack.page_count;
    right->layout.stack.page_count = size;

    segments =
        left->layout.loaded_image.segments;
    left->layout.loaded_image.segments =
        right->layout.loaded_image.segments;
    right->layout.loaded_image.segments =
        segments;

    size =
        left->layout.loaded_image.segment_count;
    left->layout.loaded_image.segment_count =
        right->layout.loaded_image.segment_count;
    right->layout.loaded_image.segment_count =
        size;
}

static void process_exec_copy_context(
    struct process_context *destination,
    const struct process_context *source)
{
    destination->r15 = source->r15;
    destination->r14 = source->r14;
    destination->r13 = source->r13;
    destination->r12 = source->r12;
    destination->r11 = source->r11;
    destination->r10 = source->r10;
    destination->r9 = source->r9;
    destination->r8 = source->r8;

    destination->rbp = source->rbp;
    destination->rdi = source->rdi;
    destination->rsi = source->rsi;
    destination->rdx = source->rdx;
    destination->rcx = source->rcx;
    destination->rbx = source->rbx;
    destination->rax = source->rax;

    destination->rip = source->rip;
    destination->rsp = source->rsp;
    destination->rflags = source->rflags;
}
