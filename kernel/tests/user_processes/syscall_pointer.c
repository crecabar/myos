// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file syscall_pointer.c
 * @brief Ring-3 hostile userspace pointer regression fixture.
 */

#include "syscall_pointer.h"

#include "../../arch/x86_64/paging.h"
#include "../../core/panic.h"
#include "../../diagnostics/diagnostics.h"
#include "../../elf/elf64.h"
#include "../../memory/memory.h"
#include "../../process/create.h"

#include <stddef.h>
#include <stdint.h>

// Static local variables
extern const uint8_t process_syscall_pointer_fixture_start[];
extern const uint8_t process_syscall_pointer_fixture_end[];

static struct process_instance
    *syscall_pointer_test_instance;

static struct paging_translation
    syscall_pointer_kernel_translation_before;

static uint64_t syscall_pointer_test_free_frame_baseline;

// Public functions implementations
void user_process_syscall_pointer_test_prepare(void)
{
    uintptr_t image_start =
        (uintptr_t)
        process_syscall_pointer_fixture_start;

    uintptr_t image_end =
        (uintptr_t)
        process_syscall_pointer_fixture_end;

    if (image_end <= image_start) {
        kernel_panic(
            "Syscall pointer ELF fixture has invalid bounds"
        );
    }

    uintptr_t image_size_value =
        image_end -
        image_start;

    if (image_size_value > SIZE_MAX) {
        kernel_panic(
            "Syscall pointer ELF fixture is too large"
        );
    }

    size_t image_size =
        (size_t) image_size_value;

    struct elf64_image image;

    if (!elf64_parse(
        process_syscall_pointer_fixture_start,
        image_size,
        &image
    )) {
        kernel_panic(
            "Unable to parse syscall pointer ELF fixture"
        );
    }

    syscall_pointer_test_free_frame_baseline =
        physical_free_frame_count();

    const char *argv[] = {
        "syscall-pointer",
    };

    syscall_pointer_test_instance =
        process_create_elf64(
            &image,
            1,
            argv,
            0,
            NULL
        );

    if (syscall_pointer_test_instance == NULL) {
        kernel_panic(
            "Unable to create syscall pointer test process"
        );
    }

    /*
     * Capture one shared higher-half kernel translation before the hostile
     * process executes. The userspace copy path must never modify it.
     *
     * Use a kernel data object's address rather than a function pointer so
     * the conversion to uintptr_t remains ordinary object-pointer arithmetic.
     */
    uint64_t kernel_probe_address =
        (uint64_t) (uintptr_t)
        &syscall_pointer_test_free_frame_baseline;

    if (!paging_translate_address_space(
        &syscall_pointer_test_instance
            ->image.memory.address_space,
        kernel_probe_address,
        &syscall_pointer_kernel_translation_before
    )) {
        kernel_panic(
            "Unable to capture syscall pointer kernel mapping"
        );
    }
}

void user_process_syscall_pointer_test_terminated(
    struct process *process)
{
    if (
        syscall_pointer_test_instance == NULL ||
        process !=
            &syscall_pointer_test_instance->process
    ) {
        kernel_panic(
            "Syscall pointer test received unexpected process"
        );
    }

    /*
     * Every hostile pointer must have returned BAD_ADDRESS to Ring 3.
     * The userspace fixture exits zero only when every check succeeded.
     *
     * A page fault, protection fault, or any other forced termination is
     * therefore itself a test failure.
     */
    if (
        process->state !=
            PROCESS_STATE_TERMINATED ||
        process->termination_reason !=
            PROCESS_TERMINATION_EXITED ||
        process->exit_status != 0
    ) {
        kernel_panic(
            "Ring-3 syscall pointer regression test failed"
        );
    }

    uint64_t kernel_probe_address =
        (uint64_t) (uintptr_t)
        &syscall_pointer_test_free_frame_baseline;

    struct paging_translation translation_after;

    if (!paging_translate_address_space(
        &syscall_pointer_test_instance
            ->image.memory.address_space,
        kernel_probe_address,
        &translation_after
    )) {
        kernel_panic(
            "Syscall pointer test lost shared kernel mapping"
        );
    }

    if (
        translation_after.physical_address !=
            syscall_pointer_kernel_translation_before
                .physical_address ||
        translation_after.page_size !=
            syscall_pointer_kernel_translation_before
                .page_size ||
        translation_after.pml4_entry !=
            syscall_pointer_kernel_translation_before
                .pml4_entry ||
        translation_after.pdpt_entry !=
            syscall_pointer_kernel_translation_before
                .pdpt_entry ||
        translation_after.pd_entry !=
            syscall_pointer_kernel_translation_before
                .pd_entry ||
        translation_after.pt_entry !=
            syscall_pointer_kernel_translation_before
                .pt_entry
    ) {
        kernel_panic(
            "Syscall pointer test modified shared kernel mapping"
        );
    }

    if (!process_release_terminated(
        syscall_pointer_test_instance
    )) {
        kernel_panic(
            "Unable to release syscall pointer test process"
        );
    }

    syscall_pointer_test_instance = NULL;

    if (
        physical_free_frame_count() !=
        syscall_pointer_test_free_frame_baseline
    ) {
        kernel_panic(
            "Syscall pointer test leaked physical frames"
        );
    }

    diagnostics_write(
        "[syscall] Ring-3 invalid user-pointer regressions passed\n"
    );
}
