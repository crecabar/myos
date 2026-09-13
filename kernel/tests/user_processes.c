// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file user_processes.c
 * @brief User-process scheduler test fixtures.
 */

#include "user_processes.h"

#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../process/layout.h"
#include "../process/memory.h"
#include "../process/process.h"
#include "../arch/x86_64/tests/user_programs.h"
#include "../scheduler/scheduler.h"

#include <stddef.h>
#include <stdint.h>

#define USER_PROCESS_TEST_COUNT 7

struct user_process_fixture {
    struct process_memory memory;
    struct process_layout layout;
    struct process process;
};

static struct user_process_fixture fixtures[USER_PROCESS_TEST_COUNT];

// Private helpers declarations
static void user_process_test_prepare(
    struct user_process_fixture *fixture,
    uint64_t id,
    const struct user_program *program
);

static void user_process_tests_dump(void);

void user_process_tests_prepare(void)
{
    user_process_test_prepare(
        &fixtures[0],
        1,
        user_program_hello()
    );

    user_process_test_prepare(
        &fixtures[1],
        2,
        user_program_counter()
    );

    user_process_test_prepare(
        &fixtures[2],
        3,
        user_program_malicious_page_fault()
    );

    user_process_test_prepare(
        &fixtures[3],
        4,
        user_program_malicious_ud2()
    );

    user_process_test_prepare(
        &fixtures[4],
        5,
        user_program_malicious_hlt()
    );

    user_process_test_prepare(
        &fixtures[5],
        6,
        user_program_survivor()
    );

    user_process_test_prepare(
        &fixtures[6],
        7,
        user_program_malicious_x87()
    );

    user_process_tests_dump();
}

// Private helpers implementations
static void user_process_test_prepare(
    struct user_process_fixture *fixture,
    uint64_t id,
    const struct user_program *program)
{
    if (!process_memory_create(&fixture->memory)) {
        kernel_panic("Unable to create user test process address space");
    }

    if (!process_layout_create(
        &fixture->memory,
        &fixture->layout
    )) {
        kernel_panic("Unable to create user test process layout");
    }

    if (!process_memory_write(
        &fixture->memory,
        fixture->layout.code_base,
        program->data,
        program->size
    )) {
        kernel_panic("Unable to load user test process program");
    }

    if (!process_init(
        &fixture->process,
        id,
        &fixture->memory,
        &fixture->layout
    )) {
        kernel_panic("Unable to initialize user test process");
    }

    if (!scheduler_add(&fixture->process)) {
        kernel_panic("Unable to schedule user test process");
    }
}

static void user_process_tests_dump(void)
{
    diagnostics_write("\n--- Initial processes ---\n");

    for (size_t index = 0; index < USER_PROCESS_TEST_COUNT; ++index) {
        const struct user_process_fixture *fixture = &fixtures[index];

        diagnostics_printf(
            "PID %u:\n"
            "  CR3=%x\n"
            "  RIP=%x\n"
            "  RSP=%x\n",
            fixture->process.id,
            fixture->memory.address_space.pml4_physical,
            fixture->layout.entry_point,
            fixture->layout.stack.stack_top
        );
    }

    diagnostics_write("\n");
}
