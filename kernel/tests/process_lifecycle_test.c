// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file process_lifecycle_test.c
 * @brief Process lifecycle reclamation regression tests.
 */

#include "process_lifecycle_test.h"

#include "../arch/x86_64/paging.h"
#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../memory/memory.h"
#include "../process/layout.h"
#include "../process/memory.h"
#include "../process/process.h"

#include <stddef.h>
#include <stdint.h>

#define PROCESS_LIFECYCLE_TEST_KERNEL_ADDRESS 0xFFFFFFFF80000000ULL

#define TEST_PROCESS_PID 42

void process_lifecycle_test_run(void)
{
    uint64_t free_before =
        physical_free_frame_count();

    struct paging_address_space *kernel_space =
        paging_kernel_address_space();

    if (kernel_space == NULL) {
        kernel_panic(
            "Kernel address space unavailable for lifecycle test"
        );
    }

    uint16_t kernel_pml4_index =
        paging_pml4_index(
            PROCESS_LIFECYCLE_TEST_KERNEL_ADDRESS
        );

    uint64_t kernel_pml4_entry_before =
        kernel_space->pml4_virtual[kernel_pml4_index];

    struct paging_translation kernel_translation_before;

    if (!paging_translate_address_space(
        kernel_space,
        PROCESS_LIFECYCLE_TEST_KERNEL_ADDRESS,
        &kernel_translation_before
    )) {
        kernel_panic(
            "Unable to translate kernel mapping before lifecycle test"
        );
    }

    struct process_memory memory;

    if (!process_memory_create(&memory)) {
        kernel_panic(
            "Unable to create lifecycle test process memory"
        );
    }

    if (!memory.address_space.kernel_half_shared) {
        kernel_panic(
            "Lifecycle test process did not inherit shared kernel half"
        );
    }

    struct process_layout layout;

    if (!process_layout_create(
        &memory,
        &layout
    )) {
        kernel_panic(
            "Unable to create lifecycle test process layout"
        );
    }

    if (
        layout.initial_rsp !=
        layout.stack.stack_top
    ) {
        kernel_panic(
            "Legacy process layout initial RSP does not match stack top"
        );
    }

    uint64_t custom_initial_rsp =
        layout.stack.stack_top - 0x100ULL;

    if (
        custom_initial_rsp <
        layout.stack.base_address ||
        custom_initial_rsp >=
        layout.stack.stack_top
    ) {
        kernel_panic(
            "Lifecycle test custom initial RSP is outside user stack"
        );
    }

    if (
        (custom_initial_rsp & 0xFULL) != 0
    ) {
        kernel_panic(
            "Lifecycle test custom initial RSP is not 16-byte aligned"
        );
    }

    layout.initial_rsp =
        custom_initial_rsp;

    struct process process;

    if (!process_init(
        &process,
        TEST_PROCESS_PID,
        &memory,
        &layout
    )) {
        kernel_panic(
            "Unable to initialize lifecycle test process"
        );
    }

    if (
        process.context.rip !=
        layout.entry_point
    ) {
        kernel_panic(
            "Process context did not inherit layout entry point"
        );
    }

    if (
        process.context.rsp !=
        custom_initial_rsp
    ) {
        kernel_panic(
            "Process context did not inherit layout initial RSP"
        );
    }

    uint64_t free_after_create =
        physical_free_frame_count();

    if (free_after_create >= free_before) {
        kernel_panic(
            "Lifecycle test did not allocate process resources"
        );
    }

    if (process_reclaim_resources(&process)) {
        kernel_panic(
            "Lifecycle reclaim accepted a non-terminated process"
        );
    }

    if (physical_free_frame_count() != free_after_create) {
        kernel_panic(
            "Rejected lifecycle reclaim changed frame ownership"
        );
    }

    if (
        process.memory != &memory ||
        process.layout != &layout
    ) {
        kernel_panic(
            "Rejected lifecycle reclaim changed process references"
        );
    }

    process.state = PROCESS_STATE_TERMINATED;

    process.termination_reason = PROCESS_TERMINATION_EXITED;

    process.exit_status = 42;

    if (process.id != TEST_PROCESS_PID) {
        kernel_panic(
            "Lifecycle reclaim changed process identifier"
        );
    }

    if (!process_reclaim_resources(&process)) {
        kernel_panic(
            "Unable to reclaim terminated process resources"
        );
    }

    if (process.memory != NULL) {
        kernel_panic(
            "Reclaimed process retained memory reference"
        );
    }

    if (process.layout != NULL) {
        kernel_panic(
            "Reclaimed process retained layout reference"
        );
    }

    if (process.state != PROCESS_STATE_TERMINATED) {
        kernel_panic(
            "Lifecycle reclaim changed process state"
        );
    }

    if (
        process.termination_reason !=
        PROCESS_TERMINATION_EXITED
    ) {
        kernel_panic(
            "Lifecycle reclaim changed termination reason"
        );
    }

    if (process.exit_status != 42) {
        kernel_panic(
            "Lifecycle reclaim changed exit status"
        );
    }

    if (
        memory.address_space.pml4_physical != 0 ||
        memory.address_space.pml4_virtual != NULL ||
        memory.address_space.kernel_half_shared
    ) {
        kernel_panic(
            "Lifecycle reclaim left process address space alive"
        );
    }

    if (
        layout.code_base != 0 ||
        layout.entry_point != 0 ||
        layout.initial_rsp != 0 ||
        layout.stack.guard_address != 0 ||
        layout.stack.base_address != 0 ||
        layout.stack.stack_top != 0 ||
        layout.stack.page_count != 0
    ) {
        kernel_panic(
            "Lifecycle reclaim left layout resources initialized"
        );
    }

    if (physical_free_frame_count() != free_before) {
        kernel_panic(
            "Lifecycle reclaim leaked physical frames"
        );
    }

    if (
        kernel_space->pml4_virtual[kernel_pml4_index] !=
        kernel_pml4_entry_before
    ) {
        kernel_panic(
            "Lifecycle reclaim modified kernel PML4 entry"
        );
    }

    struct paging_translation kernel_translation_after;

    if (!paging_translate_address_space(
        kernel_space,
        PROCESS_LIFECYCLE_TEST_KERNEL_ADDRESS,
        &kernel_translation_after
    )) {
        kernel_panic(
            "Kernel mapping disappeared after lifecycle reclaim"
        );
    }

    if (
        kernel_translation_after.physical_address !=
        kernel_translation_before.physical_address
    ) {
        kernel_panic(
            "Lifecycle reclaim changed kernel mapping"
        );
    }

    if (process_reclaim_resources(&process)) {
        kernel_panic(
            "Lifecycle reclaim accepted an already-reclaimed process"
        );
    }

    if (physical_free_frame_count() != free_before) {
        kernel_panic(
            "Repeated lifecycle reclaim changed frame ownership"
        );
    }

    diagnostics_write(
        "[process] Terminated process resource reclaim test passed\n"
    );
}
