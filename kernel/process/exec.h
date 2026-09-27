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
 * @param candidate Candidate storage whose prepared member is false.
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

/**
 * Commits a prepared executable image to the currently running process.
 *
 * The process must be backed by an owned process_image and its current address
 * space must be active when this function is called.
 *
 * All validation occurs before the commit point. The candidate address space
 * is activated first; only after successful activation is ownership exchanged.
 *
 * On success, process identity and scheduler state remain unchanged while the
 * executable image and saved userspace CPU context are replaced. The previous
 * image is destroyed after the new address space becomes active.
 *
 * This function does not rewrite the active syscall/interrupt frame. The
 * caller must arrange for return to userspace using process->context.
 *
 * A failure returned as false occurs before ownership transfer and leaves both
 * the process and candidate intact. Failure while destroying the displaced
 * image after commit is a kernel invariant violation and is fatal.
 *
 * @param process Currently running image-backed process.
 * @param candidate Completely prepared replacement image.
 *
 * @return true when the replacement was committed; false when pre-commit
 * validation or address-space activation failed.
 */
bool process_exec_candidate_commit_current(
    struct process *process,
    struct process_exec_candidate *candidate
);

#endif
