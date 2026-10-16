// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file fixture.c
 * @brief Shared built-in user-process test fixture.
 */

#include "fixture.h"

#include "../../arch/x86_64/tests/user_programs.h"
#include "../../core/panic.h"
#include "../../scheduler/scheduler.h"

// Public functions implementations
void user_process_fixture_prepare(
    struct user_process_fixture *fixture,
    uint64_t id,
    const struct user_program *program)
{
    if (!process_memory_create(&fixture->memory)) {
        kernel_panic(
            "Unable to create user test process address space"
        );
    }

    if (!process_layout_create(
        &fixture->memory,
        &fixture->layout
    )) {
        kernel_panic(
            "Unable to create user test process layout"
        );
    }

    if (!process_memory_write(
        &fixture->memory,
        fixture->layout.code_base,
        program->data,
        program->size
    )) {
        kernel_panic(
            "Unable to load user test process program"
        );
    }

    if (!process_init(
        &fixture->process,
        id,
        &fixture->memory,
        &fixture->layout
    )) {
        kernel_panic(
            "Unable to initialize user test process"
        );
    }

    if (!scheduler_add(&fixture->process)) {
        kernel_panic(
            "Unable to schedule user test process"
        );
    }
}
