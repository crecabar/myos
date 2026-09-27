// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file exec.h
 * @brief Transactional executable-image replacement preparation.
 */

#ifndef MYOS_PROCESS_EXEC_H
#define MYOS_PROCESS_EXEC_H

#include "../elf/elf64.h"
#include "image.h"
#include "process.h"

#include <stdbool.h>
#include <stddef.h>

/**
 * Represents a completely prepared executable-image replacement.
 *
 * Preparation is intentionally independent from the process being replaced.
 * Until the candidate is committed, the currently running process identity,
 * image, address space, and CPU context remain untouched.
 *
 * Before first use, prepared must be initialized to false. The image and
 * context fields need not be initialized because successful preparation
 * constructs them completely before they become owned by the candidate.
 */
struct process_exec_candidate {
    struct process_image image;
    struct process_context context;
    bool prepared;
};

/**
 * Prepares a complete ELF64 executable-image replacement.
 *
 * A fresh address space, ELF layout, userspace stack, and initial CPU context
 * are constructed without modifying an existing process.
 *
 * On failure, every resource acquired while preparing the candidate is
 * released before returning.
 *
 * @param candidate Zero-initialized candidate storage.
 * @param elf Parsed ELF64 executable.
 * @param argc Number of argv strings.
 * @param argv Argument strings, or NULL when argc is zero.
 * @param envc Number of environment strings.
 * @param envp Environment strings, or NULL when envc is zero.
 *
 * @return true when a complete replacement candidate is ready; false
 * otherwise.
 */
bool process_exec_candidate_prepare_elf64(
    struct process_exec_candidate *candidate,
    const struct elf64_image *elf,
    size_t argc,
    const char *const argv[],
    size_t envc,
    const char *const envp[]
);

/**
 * Releases a prepared executable-image replacement candidate.
 *
 * This operation is used when a prepared candidate will not be committed.
 *
 * @param candidate Prepared candidate to discard.
 *
 * @return true when every candidate resource was released; false otherwise.
 */
bool process_exec_candidate_discard(
    struct process_exec_candidate *candidate
);

#endif
