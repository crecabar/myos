// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file elf_protection.h
 * @brief Hostile Ring-3 ELF protection regressions.
 */

#ifndef MYOS_TESTS_USER_PROCESSES_ELF_PROTECTION_H
#define MYOS_TESTS_USER_PROCESSES_ELF_PROTECTION_H

struct process;

/**
 * Prepares the hostile self-modifying ELF regression.
 */
void user_process_elf_protection_test_prepare(void);

/**
 * Validates termination and cleanup of the hostile ELF process.
 *
 * @param process Detached terminated hostile process.
 */
void user_process_elf_protection_test_terminated(
    struct process *process
);

#endif
