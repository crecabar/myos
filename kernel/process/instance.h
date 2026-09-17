// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file instance.h
 * @brief Owned lifecycle storage for one MyOS userspace process.
 *
 * A process instance owns both the schedulable process descriptor and the
 * executable image referenced by that descriptor.
 *
 * Scheduler registration remains separate. The scheduler may borrow the
 * embedded process descriptor but never owns the instance or its image.
 */

#ifndef MYOS_PROCESS_INSTANCE_H
#define MYOS_PROCESS_INSTANCE_H

#include "../elf/elf64.h"
#include "image.h"
#include "process.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Owns the lifecycle storage of one userspace process.
 *
 * The embedded process descriptor borrows the memory and layout contained in
 * image. Therefore the instance must remain alive while the process may be
 * referenced by the scheduler.
 */
struct process_instance {
    struct process process;
    struct process_image image;
};

/**
 * Prepares an ELF-backed process instance.
 *
 * A complete executable image is created first. The process descriptor is then
 * initialized to borrow that image's memory and layout.
 *
 * This function does not register the process with the scheduler and does not
 * allocate the supplied process identifier.
 *
 * On failure, all resources acquired during preparation are released before
 * returning.
 *
 * @param instance Instance storage to initialize.
 * @param id Process identifier supplied by the lifecycle layer.
 * @param elf Parsed ELF64 executable.
 * @param argc Number of argv strings.
 * @param argv Argument strings, or NULL when argc is zero.
 * @param envc Number of environment strings.
 * @param envp Environment strings, or NULL when envc is zero.
 *
 * @return true when the complete process instance was prepared; false
 * otherwise.
 */
bool process_instance_prepare_elf64(
    struct process_instance *instance,
    uint64_t id,
    const struct elf64_image *elf,
    size_t argc,
    const char *const argv[],
    size_t envc,
    const char *const envp[]
);

/**
 * Releases an unregistered prepared process instance.
 *
 * This function is intended for rollback before scheduler ownership has been
 * established. A process that has run and terminated must instead be reclaimed
 * through the normal terminated-process lifecycle.
 *
 * @param instance Prepared instance that is not registered with the scheduler.
 *
 * @return true when all image resources were released; false otherwise.
 */
bool process_instance_discard(
    struct process_instance *instance
);

#endif
