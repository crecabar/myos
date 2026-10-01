// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file standard.c
 * @brief Standard Ring-3 process regression fixtures.
 */

#include "standard.h"

#include "fixture.h"

#include "../../arch/x86_64/tests/user_programs.h"
#include "../../core/panic.h"
#include "../../diagnostics/diagnostics.h"
#include "../../elf/elf64.h"
#include "../../process/create.h"
#include "../../scheduler/scheduler.h"

#if MYOS_QEMU_TEST_EXIT
#include "../../arch/x86_64/qemu_test_exit.h"
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Defines
#define USER_PROCESS_LEGACY_TEST_COUNT 7
#define USER_PROCESS_TEST_COUNT 8
#define USER_PROCESS_ELF_TEST_INDEX 7

// Static local variables
static struct user_process_fixture fixtures[
    USER_PROCESS_LEGACY_TEST_COUNT
];

static struct process_instance *elf_test_instance;

static bool standard_process_completed[
    USER_PROCESS_TEST_COUNT
];

static size_t standard_process_completed_count;

extern const uint8_t process_elf_entry_fixture_start[];
extern const uint8_t process_elf_entry_fixture_end[];

// Private functions and helpers declarations
static void user_process_standard_elf_prepare(void);

static bool user_process_standard_result_valid(
    size_t index,
    const struct process *process
);

static void user_process_standard_dump(void);

// Public functions implementations
void user_process_standard_test_prepare(void)
{
    standard_process_completed_count = 0;

    for (
        size_t index = 0;
        index < USER_PROCESS_TEST_COUNT;
        ++index
    ) {
        standard_process_completed[index] = false;
    }

    user_process_fixture_prepare(
        &fixtures[0],
        1,
        user_program_hello()
    );

    user_process_fixture_prepare(
        &fixtures[1],
        2,
        user_program_counter()
    );

    user_process_fixture_prepare(
        &fixtures[2],
        3,
        user_program_malicious_page_fault()
    );

    user_process_fixture_prepare(
        &fixtures[3],
        4,
        user_program_malicious_ud2()
    );

    user_process_fixture_prepare(
        &fixtures[4],
        5,
        user_program_malicious_hlt()
    );

    user_process_fixture_prepare(
        &fixtures[5],
        6,
        user_program_survivor()
    );

    user_process_fixture_prepare(
        &fixtures[6],
        7,
        user_program_malicious_x87()
    );

    user_process_standard_elf_prepare();

    user_process_standard_dump();
}

void user_process_standard_test_terminated(
    struct process *process)
{
    if (process == NULL) {
        kernel_panic(
            "Standard process test received null process"
        );
    }

    size_t index = USER_PROCESS_TEST_COUNT;

    if (
        elf_test_instance != NULL &&
        process == &elf_test_instance->process
    ) {
        if (
            process->termination_reason !=
                PROCESS_TERMINATION_EXITED ||
            process->exit_status != 0
        ) {
            kernel_panic(
                "Dynamic ELF user process produced unexpected result"
            );
        }

        if (!process_release_terminated(
            elf_test_instance
        )) {
            kernel_panic(
                "Unable to release dynamic ELF user process"
            );
        }

        elf_test_instance = NULL;

        standard_process_completed[
            USER_PROCESS_ELF_TEST_INDEX
        ] = true;

        ++standard_process_completed_count;

        if (
            standard_process_completed_count <
            USER_PROCESS_TEST_COUNT
        ) {
            return;
        }

        scheduler_set_terminated_handler(NULL);

        diagnostics_write(
            "[test] Kernel test suite passed\n"
        );

#if MYOS_QEMU_TEST_EXIT
        qemu_test_exit_success();
#endif

        return;
    }

    for (
        size_t candidate = 0;
        candidate < USER_PROCESS_LEGACY_TEST_COUNT;
        ++candidate
    ) {
        if (
            process ==
            &fixtures[candidate].process
        ) {
            index = candidate;
            break;
        }
    }

    if (index == USER_PROCESS_TEST_COUNT) {
        kernel_panic(
            "Standard process test received unexpected process"
        );
    }

    if (standard_process_completed[index]) {
        kernel_panic(
            "Standard process test completed twice"
        );
    }

    if (!user_process_standard_result_valid(
        index,
        process
    )) {
        kernel_panic(
            "Standard process test produced unexpected result"
        );
    }

    standard_process_completed[index] = true;
    ++standard_process_completed_count;

    if (
        standard_process_completed_count <
        USER_PROCESS_TEST_COUNT
    ) {
        return;
    }

    scheduler_set_terminated_handler(NULL);

    diagnostics_write(
        "[test] Kernel test suite passed\n"
    );

#if MYOS_QEMU_TEST_EXIT
    qemu_test_exit_success();
#endif
}

// Private functions and helpers implementations
static void user_process_standard_elf_prepare(void)
{
    uintptr_t image_start =
        (uintptr_t)
        process_elf_entry_fixture_start;

    uintptr_t image_end =
        (uintptr_t)
        process_elf_entry_fixture_end;

    if (image_end <= image_start) {
        kernel_panic(
            "ELF user test fixture has invalid bounds"
        );
    }

    uintptr_t image_size_value =
        image_end -
        image_start;

    if (image_size_value > SIZE_MAX) {
        kernel_panic(
            "ELF user test fixture is too large"
        );
    }

    size_t image_size =
        (size_t) image_size_value;

    struct elf64_image image;

    if (!elf64_parse(
        process_elf_entry_fixture_start,
        image_size,
        &image
    )) {
        kernel_panic(
            "Unable to parse ELF user test image"
        );
    }

    const char *argv[] = {
        "elf-entry",
        "argument",
    };

    const char *envp[] = {
        "TERM=myos",
    };

    elf_test_instance =
        process_create_elf64(
            &image,
            2,
            argv,
            1,
            envp
        );

    if (elf_test_instance == NULL) {
        kernel_panic(
            "Unable to dynamically create ELF user test process"
        );
    }
}

static bool user_process_standard_result_valid(
    size_t index,
    const struct process *process)
{
    if (
        process->state !=
        PROCESS_STATE_TERMINATED
    ) {
        return false;
    }

    switch (index) {
        case 0:
        case 1:
        case 5:
            return
                process->termination_reason ==
                    PROCESS_TERMINATION_EXITED &&
                process->exit_status == 0;

        case 2:
            return
                process->termination_reason ==
                    PROCESS_TERMINATION_SEGMENTATION_FAULT;

        case 3:
        case 6:
            return
                process->termination_reason ==
                    PROCESS_TERMINATION_ILLEGAL_INSTRUCTION;

        case 4:
            return
                process->termination_reason ==
                    PROCESS_TERMINATION_PROTECTION_FAULT;

        default:
            return false;
    }
}

static void user_process_standard_dump(void)
{
    diagnostics_write(
        "\n--- Initial processes ---\n"
    );

    for (
        size_t index = 0;
        index < USER_PROCESS_LEGACY_TEST_COUNT;
        ++index
    ) {
        const struct user_process_fixture *fixture =
            &fixtures[index];

        diagnostics_printf(
            "PID %u:\n"
            "  CR3=%x\n"
            "  RIP=%x\n"
            "  RSP=%x\n",
            fixture->process.id,
            fixture->memory
                .address_space
                .pml4_physical,
            fixture->layout.entry_point,
            fixture->layout.stack.stack_top
        );
    }

    if (elf_test_instance != NULL) {
        diagnostics_printf(
            "PID %u:\n"
            "  CR3=%x\n"
            "  RIP=%x\n"
            "  RSP=%x\n",
            elf_test_instance->process.id,
            elf_test_instance
                ->image
                .memory
                .address_space
                .pml4_physical,
            elf_test_instance
                ->image
                .layout
                .entry_point,
            elf_test_instance
                ->image
                .layout
                .initial_rsp
        );
    }

    diagnostics_write("\n");
}
