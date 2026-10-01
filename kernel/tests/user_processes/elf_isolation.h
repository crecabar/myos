// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file elf_isolation.h
 * @brief Cross-address-space executable isolation regression.
 */

#ifndef MYOS_TESTS_USER_PROCESSES_ELF_ISOLATION_H
#define MYOS_TESTS_USER_PROCESSES_ELF_ISOLATION_H

#include <stdbool.h>

struct process;

void user_process_elf_isolation_test_prepare(void);

bool user_process_elf_isolation_test_terminated(
    struct process *process
);

#endif
