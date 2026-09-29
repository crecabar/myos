// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file elf_isolation.c
 * @brief Cross-address-space executable isolation regression.
 */

#include "elf_isolation.h"

#include "../../arch/x86_64/paging.h"
#include "../../core/panic.h"
#include "../../diagnostics/diagnostics.h"
#include "../../elf/elf64.h"
#include "../../memory/heap.h"
#include "../../memory/memory.h"
#include "../../process/create.h"
#include "../../process/process.h"
#include "../../scheduler/scheduler.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Defines
#define ELF_ISOLATION_SHARED_VA 0x0000000000400000ULL
#define ELF_ISOLATION_VICTIM_MARKER 0x5AU
#define ELF_ISOLATION_ATTACKER_VALUE 0xA5U

// Static local variables
static struct process_instance *elf_isolation_attacker;
static struct process_instance *elf_isolation_victim;

static uint64_t elf_isolation_free_frame_baseline;
static struct kernel_heap_stats elf_isolation_heap_baseline;

static bool elf_isolation_attacker_completed;

static size_t elf_isolation_scheduler_count_baseline;

extern const uint8_t
    process_elf_isolation_attacker_fixture_start[];

extern const uint8_t
    process_elf_isolation_attacker_fixture_end[];

extern const uint8_t
    process_elf_isolation_victim_fixture_start[];

extern const uint8_t
    process_elf_isolation_victim_fixture_end[];

// Private functions and helpers declarations
static void user_process_elf_isolation_parse_fixture(
    const uint8_t *start,
    const uint8_t *end,
    struct elf64_image *image
);

// Public functions implementations
void user_process_elf_isolation_test_prepare(void)
{
    struct elf64_image attacker_image;
    struct elf64_image victim_image;

    user_process_elf_isolation_parse_fixture(
        process_elf_isolation_attacker_fixture_start,
        process_elf_isolation_attacker_fixture_end,
        &attacker_image
    );

    user_process_elf_isolation_parse_fixture(
        process_elf_isolation_victim_fixture_start,
        process_elf_isolation_victim_fixture_end,
        &victim_image
    );

    elf_isolation_free_frame_baseline =
        physical_free_frame_count();

    elf_isolation_scheduler_count_baseline =
        scheduler_test_process_count();

    if (!kernel_heap_stats_get(
        &elf_isolation_heap_baseline
    )) {
        kernel_panic(
            "Unable to read heap baseline before ELF isolation test"
        );
    }

    elf_isolation_attacker_completed = false;

    /*
     * Register the attacker first so its tiny write/exit path normally runs
     * first. The victim additionally yields several times so the ordering
     * remains safe if scheduling changes.
     */
    elf_isolation_attacker =
        process_create_elf64(
            &attacker_image,
            0,
            NULL,
            0,
            NULL
        );

    if (elf_isolation_attacker == NULL) {
        kernel_panic(
            "Unable to create ELF isolation attacker"
        );
    }

    elf_isolation_victim =
        process_create_elf64(
            &victim_image,
            0,
            NULL,
            0,
            NULL
        );

    if (elf_isolation_victim == NULL) {
        kernel_panic(
            "Unable to create ELF isolation victim"
        );
    }

    struct paging_translation attacker_translation;
    struct paging_translation victim_translation;

    if (
        !paging_translate_address_space(
            &elf_isolation_attacker
                ->image
                .memory
                .address_space,
            ELF_ISOLATION_SHARED_VA,
            &attacker_translation
        ) ||
        !paging_translate_address_space(
            &elf_isolation_victim
                ->image
                .memory
                .address_space,
            ELF_ISOLATION_SHARED_VA,
            &victim_translation
        )
    ) {
        kernel_panic(
            "Unable to translate ELF isolation mappings"
        );
    }

    if (
        attacker_translation.page_size !=
            PAGING_PAGE_SIZE_4K ||
        victim_translation.page_size !=
            PAGING_PAGE_SIZE_4K
    ) {
        kernel_panic(
            "ELF isolation fixture is not using 4 KiB pages"
        );
    }

    /*
     * Same VA must resolve through different address spaces to different
     * physical frames.
     */
    if (
        (
            attacker_translation.physical_address &
            PAGE_ADDRESS_MASK_4K
        ) ==
        (
            victim_translation.physical_address &
            PAGE_ADDRESS_MASK_4K
        )
    ) {
        kernel_panic(
            "ELF isolation processes share physical frame"
        );
    }

    /*
     * Attacker owns a private RW/NX mapping at 0x400000.
     */
    if (
        (
            attacker_translation.pt_entry &
            PAGE_ENTRY_USER
        ) == 0 ||
        (
            attacker_translation.pt_entry &
            PAGE_ENTRY_WRITABLE
        ) == 0 ||
        (
            attacker_translation.pt_entry &
            PAGE_ENTRY_NO_EXECUTE
        ) == 0
    ) {
        kernel_panic(
            "ELF isolation attacker mapping is not user RW/NX"
        );
    }

    /*
     * Victim owns executable read-only text at that same VA.
     */
    if (
        (
            victim_translation.pt_entry &
            PAGE_ENTRY_USER
        ) == 0 ||
        (
            victim_translation.pt_entry &
            PAGE_ENTRY_WRITABLE
        ) != 0 ||
        (
            victim_translation.pt_entry &
            PAGE_ENTRY_NO_EXECUTE
        ) != 0
    ) {
        kernel_panic(
            "ELF isolation victim mapping is not user RX"
        );
    }

    uint8_t victim_marker = 0;

    if (!process_memory_read(
        &elf_isolation_victim->image.memory,
        ELF_ISOLATION_SHARED_VA,
        &victim_marker,
        sizeof(victim_marker)
    )) {
        kernel_panic(
            "Unable to read ELF isolation victim marker"
        );
    }

    if (
        victim_marker !=
        ELF_ISOLATION_VICTIM_MARKER
    ) {
        kernel_panic(
            "ELF isolation victim marker is invalid before execution"
        );
    }
}

bool user_process_elf_isolation_test_terminated(
    struct process *process)
{
    if (
        process == NULL ||
        process->instance == NULL
    ) {
        kernel_panic(
            "ELF isolation test received invalid process"
        );
    }

    if (
        elf_isolation_attacker != NULL &&
        process ==
            &elf_isolation_attacker->process
    ) {
        if (
            elf_isolation_attacker_completed ||
            process->termination_reason !=
                PROCESS_TERMINATION_EXITED ||
            process->exit_status != 0
        ) {
            kernel_panic(
                "ELF isolation attacker produced unexpected result"
            );
        }

        uint8_t attacker_value = 0;
        uint8_t victim_value = 0;

        if (
            !process_memory_read(
                &elf_isolation_attacker->image.memory,
                ELF_ISOLATION_SHARED_VA,
                &attacker_value,
                sizeof(attacker_value)
            ) ||
            !process_memory_read(
                &elf_isolation_victim->image.memory,
                ELF_ISOLATION_SHARED_VA,
                &victim_value,
                sizeof(victim_value)
            )
        ) {
            kernel_panic(
                "Unable to inspect ELF isolation mappings after attack"
            );
        }

        if (
            attacker_value !=
                ELF_ISOLATION_ATTACKER_VALUE ||
            victim_value !=
                ELF_ISOLATION_VICTIM_MARKER
        ) {
            kernel_panic(
                "Cross-address-space executable isolation failed"
            );
        }

        if (!process_release_terminated(
            elf_isolation_attacker
        )) {
            kernel_panic(
                "Unable to release ELF isolation attacker"
            );
        }

        elf_isolation_attacker = NULL;
        elf_isolation_attacker_completed = true;

        if (
            scheduler_test_process_count() !=
            elf_isolation_scheduler_count_baseline + 1
        ) {
            kernel_panic(
                "ELF isolation attacker did not release scheduler slot"
            );
        }

        diagnostics_write(
            "[process] Hostile cross-address-space write contained\n"
        );

        return false;
    }

    if (
        elf_isolation_victim != NULL &&
        process ==
            &elf_isolation_victim->process
    ) {
        if (
            !elf_isolation_attacker_completed ||
            process->termination_reason !=
                PROCESS_TERMINATION_EXITED ||
            process->exit_status != 0
        ) {
            kernel_panic(
                "ELF isolation victim produced unexpected result"
            );
        }

        if (!process_release_terminated(
            elf_isolation_victim
        )) {
            kernel_panic(
                "Unable to release ELF isolation victim"
            );
        }

        elf_isolation_victim = NULL;

        if (
            scheduler_test_process_count() !=
            elf_isolation_scheduler_count_baseline
        ) {
            kernel_panic(
                "ELF isolation test leaked scheduler registrations"
            );
        }

        if (
            physical_free_frame_count() !=
            elf_isolation_free_frame_baseline
        ) {
            kernel_panic(
                "ELF isolation test leaked physical frames"
            );
        }

        struct kernel_heap_stats heap_after;

        if (!kernel_heap_stats_get(
            &heap_after
        )) {
            kernel_panic(
                "Unable to read heap after ELF isolation test"
            );
        }

        if (
            heap_after.allocated_block_count !=
                elf_isolation_heap_baseline.allocated_block_count ||
            heap_after.allocated_bytes !=
                elf_isolation_heap_baseline.allocated_bytes
        ) {
            kernel_panic(
                "ELF isolation test leaked kernel heap allocations"
            );
        }

        diagnostics_write(
            "[process] Cross-address-space executable isolation test passed\n"
        );

        return true;
    }

    kernel_panic(
        "ELF isolation test received unexpected process"
    );
}

// Private functions and helpers implementations
static void user_process_elf_isolation_parse_fixture(
    const uint8_t *start,
    const uint8_t *end,
    struct elf64_image *image)
{
    if (
        start == NULL ||
        end == NULL ||
        image == NULL ||
        end <= start
    ) {
        kernel_panic(
            "ELF isolation fixture has invalid bounds"
        );
    }

    uintptr_t image_size_value =
        (uintptr_t) end -
        (uintptr_t) start;

    if (image_size_value > SIZE_MAX) {
        kernel_panic(
            "ELF isolation fixture is too large"
        );
    }

    if (!elf64_parse(
        start,
        (size_t) image_size_value,
        image
    )) {
        kernel_panic(
            "Unable to parse ELF isolation fixture"
        );
    }
}
