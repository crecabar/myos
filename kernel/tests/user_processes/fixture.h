// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file fixture.h
 * @brief Shared built-in user-process test fixture.
 */

#ifndef MYOS_TESTS_USER_PROCESSES_FIXTURE_H
#define MYOS_TESTS_USER_PROCESSES_FIXTURE_H

#include "../../process/layout.h"
#include "../../process/memory.h"
#include "../../process/process.h"

#include <stdint.h>

struct user_program;

/**
 * Owns the statically allocated resources of one built-in user-process test.
 */
struct user_process_fixture {
    struct process_memory memory;
    struct process_layout layout;
    struct process process;
};

/**
 * Prepares and schedules one built-in user-process test fixture.
 *
 * @param fixture Fixture storage owned by the caller.
 * @param id Process identifier.
 * @param program Built-in machine-code program to load.
 */
void user_process_fixture_prepare(
    struct user_process_fixture *fixture,
    uint64_t id,
    const struct user_program *program
);

#endif
