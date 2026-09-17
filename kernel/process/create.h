// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file create.h
 * @brief Dynamic userspace process creation.
 *
 * Provides the lifecycle orchestration required to allocate a process instance,
 * materialize an executable image, initialize its process descriptor, and
 * register the resulting READY process with the scheduler.
 */

#ifndef MYOS_PROCESS_CREATE_H
#define MYOS_PROCESS_CREATE_H

#include "../elf/elf64.h"
#include "instance.h"

#include <stddef.h>
#include <stdint.h>

/**
 * Dynamically creates and schedules an ELF64 userspace process.
 *
 * Lifecycle storage is allocated from the kernel heap. A complete ELF-backed
 * process instance is prepared and then registered with the scheduler.
 *
 * The supplied process identifier is currently borrowed from the caller's PID
 * allocation policy. Central PID allocation will be added before issue #44 is
 * considered complete.
 *
 * On failure, all resources acquired during this operation are released before
 * returning.
 *
 * On success, the returned process instance remains owned by the process
 * lifecycle layer. The scheduler stores only a borrowed pointer to its embedded
 * process descriptor.
 *
 * @param id Process identifier.
 * @param elf Parsed ELF64 executable.
 * @param argc Number of argv strings.
 * @param argv Argument strings, or NULL when argc is zero.
 * @param envc Number of environment strings.
 * @param envp Environment strings, or NULL when envc is zero.
 *
 * @return Owned process instance when creation and scheduler registration
 * succeed; NULL otherwise.
 */
struct process_instance *process_create_elf64(
    uint64_t id,
    const struct elf64_image *elf,
    size_t argc,
    const char *const argv[],
    size_t envc,
    const char *const envp[]
);

#endif
