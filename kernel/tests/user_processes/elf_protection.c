// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file elf_protection.c
 * @brief Hostile Ring-3 ELF protection regressions.
 */

#include "elf_protection.h"

#include "../../arch/x86_64/paging.h"
#include "../../core/panic.h"
#include "../../diagnostics/diagnostics.h"
#include "../../elf/elf64.h"
#include "../../memory/heap.h"
#include "../../memory/memory.h"
#include "../../process/create.h"
#include "../../process/process.h"
#include "../../scheduler/scheduler.h"

#include <stddef.h>
#include <stdint.h>

// Static local variables
static struct process_instance *elf_protection_instance;

static uint64_t elf_protection_free_frame_baseline;
static struct kernel_heap_stats elf_protection_heap_baseline;

static uint64_t elf_protection_entry_point;
static uint8_t elf_protection_original_entry_byte;

static size_t elf_protection_scheduler_count_baseline;

extern const uint8_t process_elf_protection_fixture_start[];
extern const uint8_t process_elf_protection_fixture_end[];

// Private functions and helpers declarations
static void user_process_elf_protection_parse_fixture(
    struct elf64_image *image
);

// Public functions implementations
void user_process_elf_protection_test_prepare(void)
{
    struct elf64_image image;

    user_process_elf_protection_parse_fixture(
        &image
    );

    elf_protection_free_frame_baseline =
        physical_free_frame_count();

    elf_protection_scheduler_count_baseline =
        scheduler_test_process_count();

    if (!kernel_heap_stats_get(
        &elf_protection_heap_baseline
    )) {
        kernel_panic(
            "Unable to read heap baseline before ELF protection test"
        );
    }

    elf_protection_instance =
        process_create_elf64(
            &image,
            0,
            NULL,
            0,
            NULL
        );

    if (elf_protection_instance == NULL) {
        kernel_panic(
            "Unable to create hostile ELF protection process"
        );
    }

    elf_protection_entry_point =
        elf_protection_instance
            ->image
            .layout
            .entry_point;

    struct paging_translation translation;

    if (!paging_translate_address_space(
        &elf_protection_instance
            ->image
            .memory
            .address_space,
        elf_protection_entry_point,
        &translation
    )) {
        kernel_panic(
            "Unable to translate hostile ELF entry point"
        );
    }

    if (
        translation.page_size !=
            PAGING_PAGE_SIZE_4K ||
        (
            translation.pt_entry &
            PAGE_ENTRY_USER
        ) == 0 ||
        (
            translation.pt_entry &
            PAGE_ENTRY_WRITABLE
        ) != 0 ||
        (
            translation.pt_entry &
            PAGE_ENTRY_NO_EXECUTE
        ) != 0
    ) {
        kernel_panic(
            "Hostile ELF text mapping is not user RX"
        );
    }

    if (!process_memory_read(
        &elf_protection_instance->image.memory,
        elf_protection_entry_point,
        &elf_protection_original_entry_byte,
        sizeof(elf_protection_original_entry_byte)
    )) {
        kernel_panic(
            "Unable to read hostile ELF entry byte"
        );
    }
}

void user_process_elf_protection_test_terminated(
    struct process *process)
{
    if (
        process == NULL ||
        elf_protection_instance == NULL ||
        process !=
            &elf_protection_instance->process
    ) {
        kernel_panic(
            "ELF protection test received unexpected process"
        );
    }

    if (
        process->state !=
            PROCESS_STATE_TERMINATED ||
        process->termination_reason !=
            PROCESS_TERMINATION_SEGMENTATION_FAULT
    ) {
        kernel_panic(
            "Self-modifying ELF was not contained by page protection"
        );
    }

    uint8_t entry_byte_after_fault = 0;

    if (!process_memory_read(
        &elf_protection_instance->image.memory,
        elf_protection_entry_point,
        &entry_byte_after_fault,
        sizeof(entry_byte_after_fault)
    )) {
        kernel_panic(
            "Unable to read hostile ELF text after protection fault"
        );
    }

    if (
        entry_byte_after_fault !=
        elf_protection_original_entry_byte
    ) {
        kernel_panic(
            "Hostile ELF modified executable text before termination"
        );
    }

    if (!process_release_terminated(
        elf_protection_instance
    )) {
        kernel_panic(
            "Unable to release hostile ELF protection process"
        );
    }

    elf_protection_instance = NULL;

    if (
        scheduler_test_process_count() !=
        elf_protection_scheduler_count_baseline
    ) {
        kernel_panic(
            "Hostile ELF protection test leaked scheduler registration"
        );
    }

    if (
        physical_free_frame_count() !=
        elf_protection_free_frame_baseline
    ) {
        kernel_panic(
            "Hostile ELF protection test leaked physical frames"
        );
    }

    struct kernel_heap_stats heap_after;

    if (!kernel_heap_stats_get(
        &heap_after
    )) {
        kernel_panic(
            "Unable to read heap after ELF protection test"
        );
    }

    if (
        heap_after.allocated_block_count !=
            elf_protection_heap_baseline.allocated_block_count ||
        heap_after.allocated_bytes !=
            elf_protection_heap_baseline.allocated_bytes
    ) {
        kernel_panic(
            "Hostile ELF protection test leaked kernel heap allocations"
        );
    }

    diagnostics_write(
        "[process] Hostile ELF self-modifying text test passed\n"
    );
}

// Private functions and helpers implementations
static void user_process_elf_protection_parse_fixture(
    struct elf64_image *image)
{
    if (image == NULL) {
        kernel_panic(
            "ELF protection test received null image destination"
        );
    }

    uintptr_t image_start =
        (uintptr_t)
        process_elf_protection_fixture_start;

    uintptr_t image_end =
        (uintptr_t)
        process_elf_protection_fixture_end;

    if (image_end <= image_start) {
        kernel_panic(
            "ELF protection fixture has invalid bounds"
        );
    }

    uintptr_t image_size_value =
        image_end -
        image_start;

    if (image_size_value > SIZE_MAX) {
        kernel_panic(
            "ELF protection fixture is too large"
        );
    }

    if (!elf64_parse(
        process_elf_protection_fixture_start,
        (size_t) image_size_value,
        image
    )) {
        kernel_panic(
            "Unable to parse hostile ELF protection fixture"
        );
    }
}