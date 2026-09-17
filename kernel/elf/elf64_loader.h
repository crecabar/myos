// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file elf64_loader.h
 * @brief ELF64 load-segment validation and process loading.
 */

#ifndef MYOS_ELF_ELF64_LOADER_H
#define MYOS_ELF_ELF64_LOADER_H

#include "elf64.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct process_memory;

/**
 * Page size used by the ELF64 userspace loader.
 */
#define ELF64_LOADER_PAGE_SIZE 4096ULL

/**
 * Validated mapping plan for one ELF64 PT_LOAD segment.
 *
 * The mapping range covers complete 4 KiB pages while file and memory sizes
 * retain the original ELF segment semantics.
 */
struct elf64_load_segment {
    uint64_t virtual_address;
    uint64_t file_offset;

    uint64_t file_size;
    uint64_t memory_size;

    uint64_t mapping_start;
    uint64_t mapping_end;
    size_t page_count;

    bool writable;
    bool executable;
};

/**
 * Represents the process mappings created from one ELF64 image.
 *
 * The segment array is dynamically allocated by elf64_load_image() and owns
 * only loader metadata. The mapped process pages themselves remain owned by
 * the process memory object until released through elf64_unload_image().
 *
 * The descriptor remains valid independently of the original ELF image
 * buffer.
 */
struct elf64_loaded_image {
    struct elf64_load_segment *segments;
    size_t segment_count;
};

/**
 * Validates one ELF64 PT_LOAD segment for MyOS userspace loading.
 *
 * This function performs no allocation and does not modify process memory.
 *
 * @param image Previously validated ELF64 image.
 * @param program_header Decoded PT_LOAD program header.
 * @param segment Output validated load plan.
 *
 * @return true when the segment is loadable by MyOS; false otherwise.
 */
bool elf64_load_segment_validate(
    const struct elf64_image *image,
    const struct elf64_program_header *program_header,
    struct elf64_load_segment *segment
);

/**
 * Validates all ELF64 PT_LOAD segments as a coherent MyOS load image.
 *
 * Every loadable segment must satisfy the individual PT_LOAD policy and
 * effective mapped-page ranges must not overlap.
 *
 * No process memory is allocated or modified.
 *
 * @param image Previously validated ELF64 image.
 *
 * @return true when every PT_LOAD segment is mutually loadable; false
 * otherwise.
 */
bool elf64_load_image_validate(
    const struct elf64_image *image
);

/**
 * Validates the ELF64 entry point for MyOS userspace execution.
 *
 * The entry point must lie inside the semantic memory range of a valid
 * executable PT_LOAD segment. Page-alignment padding outside the original
 * segment memory range is not considered executable program content even when
 * it belongs to an executable mapped page.
 *
 * This function performs no allocation and does not modify process memory.
 *
 * @param image Previously parsed ELF64 image.
 *
 * @return true when e_entry belongs to a valid executable PT_LOAD segment;
 * false otherwise.
 */
bool elf64_entry_point_validate(
    const struct elf64_image *image
);

/**
 * Loads all ELF64 PT_LOAD segments into an existing empty process address
 * space.
 *
 * The image is validated as a coherent MyOS load image before any process
 * memory is modified.
 *
 * Loadable pages receive their final writable and executable permissions
 * before file contents are copied. Newly allocated memory is cleared before
 * file-backed bytes are written, providing zero-filled BSS and preventing
 * disclosure of stale physical-frame contents.
 *
 * On failure, every mapping created by this operation is released before the
 * function returns.
 *
 * The process_memory object itself is borrowed and is neither created nor
 * destroyed by this function.
 *
 * @param image Previously parsed ELF64 image.
 * @param memory Empty process address space that receives the loadable
 *        segments.
 * @param loaded_image
 *
 * @return true when every PT_LOAD segment was loaded successfully; false
 * otherwise.
 */
 bool elf64_load_image(
    const struct elf64_image *image,
    struct process_memory *memory,
    struct elf64_loaded_image *loaded_image
);

/**
 * Releases every process mapping owned by a loaded ELF64 image.
 *
 * All recorded PT_LOAD mappings are released and the dynamically allocated
 * segment metadata is returned to the kernel heap.
 *
 * The descriptor is cleared before the function returns. Cleanup of remaining
 * segments is attempted even if releasing one segment fails.
 *
 * @param memory Process address space containing the loaded image.
 * @param loaded_image Loaded-image descriptor produced by elf64_load_image().
 *
 * @return true when every mapping was released successfully; false when the
 *         arguments are invalid or at least one segment could not be fully
 *         released.
 */
bool elf64_unload_image(
    struct process_memory *memory,
    struct elf64_loaded_image *loaded_image
);

#endif
