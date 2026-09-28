// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file syscall_abi.c
 * @brief Ring-3 syscall ABI regression fixture.
 */

#include "syscall_abi.h"

#include "../../core/panic.h"
#include "../../diagnostics/diagnostics.h"
#include "../../elf/elf64.h"
#include "../../memory/memory.h"
#include "../../process/create.h"

#include <stddef.h>
#include <stdint.h>

// Static local variables
static struct process_instance
    *syscall_abi_test_instance;

static uint64_t syscall_abi_test_free_frame_baseline;

extern const uint8_t process_syscall_abi_fixture_start[];
extern const uint8_t process_syscall_abi_fixture_end[];

// Public functions implementations
void user_process_syscall_abi_test_prepare(void)
{
    uintptr_t image_start =
        (uintptr_t)
        process_syscall_abi_fixture_start;

    uintptr_t image_end =
        (uintptr_t)
        process_syscall_abi_fixture_end;

    if (image_end <= image_start) {
        kernel_panic(
            "Syscall ABI ELF fixture has invalid bounds"
        );
    }

    uintptr_t image_size_value =
        image_end -
        image_start;

    if (image_size_value > SIZE_MAX) {
        kernel_panic(
            "Syscall ABI ELF fixture is too large"
        );
    }

    size_t image_size =
        (size_t) image_size_value;

    struct elf64_image image;

    if (!elf64_parse(
        process_syscall_abi_fixture_start,
        image_size,
        &image
    )) {
        kernel_panic(
            "Unable to parse syscall ABI ELF fixture"
        );
    }

    syscall_abi_test_free_frame_baseline =
        physical_free_frame_count();

    const char *argv[] = {
        "syscall-abi",
    };

    syscall_abi_test_instance =
        process_create_elf64(
            &image,
            1,
            argv,
            0,
            NULL
        );

    if (syscall_abi_test_instance == NULL) {
        kernel_panic(
            "Unable to create syscall ABI test process"
        );
    }
}

void user_process_syscall_abi_test_terminated(
    struct process *process)
{
    if (
        syscall_abi_test_instance == NULL ||
        process !=
            &syscall_abi_test_instance->process
    ) {
        kernel_panic(
            "Syscall ABI test received unexpected process"
        );
    }

    if (
        process->state !=
            PROCESS_STATE_TERMINATED ||
        process->termination_reason !=
            PROCESS_TERMINATION_EXITED ||
        process->exit_status != 0
    ) {
        kernel_panic(
            "Ring-3 syscall ABI contract test failed"
        );
    }

    if (!process_release_terminated(
        syscall_abi_test_instance
    )) {
        kernel_panic(
            "Unable to release syscall ABI test process"
        );
    }

    syscall_abi_test_instance = NULL;

    if (
        physical_free_frame_count() !=
        syscall_abi_test_free_frame_baseline
    ) {
        kernel_panic(
            "Syscall ABI test leaked physical frames"
        );
    }

    diagnostics_write(
        "[syscall] Ring-3 register and RFLAGS contract test passed\n"
    );
}
