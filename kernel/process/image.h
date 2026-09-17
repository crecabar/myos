// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file image.h
 * @brief Owned executable image of a MyOS userspace process.
 *
 * A process image owns one private process address space together with the
 * process layout materialized inside that address space.
 *
 * Process identity and scheduler registration are intentionally separate from
 * this object. This allows process creation to combine a new identity with a
 * new image while a future execve-style operation can replace only the image
 * of an existing process.
 */

#ifndef MYOS_PROCESS_IMAGE_H
#define MYOS_PROCESS_IMAGE_H

#include "../elf/elf64.h"
#include "layout.h"
#include "memory.h"

#include <stdbool.h>
#include <stddef.h>

/**
 * Owns the executable-address-space state of one userspace process.
 *
 * The image owns both memory and layout. The layout owns the mappings created
 * inside memory, while memory owns the private address-space structures.
 *
 * Destruction therefore releases the layout first and the address space
 * second.
 */
struct process_image {
    struct process_memory memory;
    struct process_layout layout;
};

/**
 * Creates an executable process image from a parsed ELF64 executable.
 *
 * A fresh private address space is created, the ELF PT_LOAD mappings and
 * initial userspace stack are materialized into it, and the resulting image
 * assumes ownership of all successfully created resources.
 *
 * The ELF image and argv/envp strings are borrowed only for the duration of
 * this call.
 *
 * On failure, all resources acquired by this operation are released before
 * returning.
 *
 * @param image Image descriptor to initialize.
 * @param elf Parsed ELF64 executable.
 * @param argc Number of argv strings.
 * @param argv Argument strings, or NULL when argc is zero.
 * @param envc Number of environment strings.
 * @param envp Environment strings, or NULL when envc is zero.
 *
 * @return true when the complete executable image was created; false
 * otherwise.
 */
bool process_image_create_elf64(
    struct process_image *image,
    const struct elf64_image *elf,
    size_t argc,
    const char *const argv[],
    size_t envc,
    const char *const envp[]
);

/**
 * Releases every resource owned by an executable process image.
 *
 * Layout-managed mappings are released before the private process address
 * space is destroyed.
 *
 * @param image Image whose resources are to be released.
 *
 * @return true when every owned resource was released; false otherwise.
 */
bool process_image_destroy(struct process_image *image);

#endif
