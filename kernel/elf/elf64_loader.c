// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file elf64_loader.c
 * @brief ELF64 load-segment validation and process loading.
 */

#include "elf64_loader.h"

#include "../memory/heap.h"
#include "../process/memory.h"

#include <stddef.h>
#include <stdint.h>

static bool elf64_loader_add_u64(
    uint64_t left,
    uint64_t right,
    uint64_t *result
);

static bool elf64_loader_is_power_of_two(
    uint64_t value
);

static uint64_t elf64_loader_align_down(
    uint64_t value
);

static bool elf64_loader_align_up(
    uint64_t value,
    uint64_t *result
);

static bool elf64_loader_segment_allocate(
    struct process_memory *memory,
    const struct elf64_load_segment *segment
);

static bool elf64_loader_segment_release(
    struct process_memory *memory,
    const struct elf64_load_segment *segment
);

static bool elf64_loader_segment_populate(
    const struct elf64_image *image,
    struct process_memory *memory,
    const struct elf64_load_segment *segment
);

static bool elf64_loader_add_u64(
    uint64_t left,
    uint64_t right,
    uint64_t *result)
{
    if (result == NULL) {
        return false;
    }

    if (left > UINT64_MAX - right) {
        return false;
    }

    *result = left + right;

    return true;
}

static bool elf64_loader_is_power_of_two(
    uint64_t value)
{
    return
        value != 0 &&
        (value & (value - 1)) == 0;
}

static uint64_t elf64_loader_align_down(
    uint64_t value)
{
    return
        value &
        ~(ELF64_LOADER_PAGE_SIZE - 1ULL);
}

static bool elf64_loader_align_up(
    uint64_t value,
    uint64_t *result)
{
    if (result == NULL) {
        return false;
    }

    uint64_t remainder =
        value &
        (ELF64_LOADER_PAGE_SIZE - 1ULL);

    if (remainder == 0) {
        *result = value;

        return true;
    }

    uint64_t adjustment =
        ELF64_LOADER_PAGE_SIZE -
        remainder;

    return elf64_loader_add_u64(
        value,
        adjustment,
        result
    );
}

static bool elf64_loader_ranges_overlap(
    uint64_t first_start,
    uint64_t first_end,
    uint64_t second_start,
    uint64_t second_end)
{
    return
        first_start < second_end &&
        second_start < first_end;
}

static bool elf64_loader_segment_allocate(
    struct process_memory *memory,
    const struct elf64_load_segment *segment)
{
    if (
        memory == NULL ||
        segment == NULL
    ) {
        return false;
    }

    if (
        segment->page_count == 0 ||
        segment->mapping_end <=
        segment->mapping_start
    ) {
        return false;
    }

    if (
        segment->writable &&
        segment->executable
    ) {
        return false;
    }

    size_t allocated_pages = 0;

    for (
        size_t index = 0;
        index < segment->page_count;
        ++index
    ) {
        uint64_t virtual_address =
            segment->mapping_start +
            (uint64_t) index *
            ELF64_LOADER_PAGE_SIZE;

        bool allocated;

        if (segment->executable) {
            allocated =
                process_memory_allocate_executable_page(
                    memory,
                    virtual_address
                );
        } else {
            allocated =
                process_memory_allocate_page(
                    memory,
                    virtual_address,
                    segment->writable
                );
        }

        if (!allocated) {
            break;
        }

        ++allocated_pages;
    }

    if (
        allocated_pages ==
        segment->page_count
    ) {
        return true;
    }

    for (
        size_t index = 0;
        index < allocated_pages;
        ++index
    ) {
        uint64_t virtual_address =
            segment->mapping_start +
            (uint64_t) index *
            ELF64_LOADER_PAGE_SIZE;

        if (!process_memory_release_page(
            memory,
            virtual_address
        )) {
            /*
             * Continue attempting to release the remaining pages.
             *
             * The function still reports failure, but one teardown failure
             * must not prevent cleanup of other mappings created by this
             * operation.
             */
            continue;
        }
    }

    return false;
}

static bool elf64_loader_segment_release(
    struct process_memory *memory,
    const struct elf64_load_segment *segment)
{
    if (
        memory == NULL ||
        segment == NULL
    ) {
        return false;
    }

    if (
        segment->page_count == 0 ||
        segment->mapping_end <=
        segment->mapping_start
    ) {
        return false;
    }

    bool released_all = true;

    for (
        size_t remaining = segment->page_count;
        remaining > 0;
        --remaining
    ) {
        size_t index =
            remaining - 1;

        uint64_t virtual_address =
            segment->mapping_start +
            (uint64_t) index *
            ELF64_LOADER_PAGE_SIZE;

        if (!process_memory_release_page(
            memory,
            virtual_address
        )) {
            released_all = false;
        }
    }

    return released_all;
}

static bool elf64_loader_segment_populate(
    const struct elf64_image *image,
    struct process_memory *memory,
    const struct elf64_load_segment *segment)
{
    if (
        image == NULL ||
        image->data == NULL ||
        memory == NULL ||
        segment == NULL
    ) {
        return false;
    }

    uint64_t mapping_size_u64 =
        segment->mapping_end -
        segment->mapping_start;

    if (mapping_size_u64 > SIZE_MAX) {
        return false;
    }

    if (!process_memory_zero(
        memory,
        segment->mapping_start,
        (size_t) mapping_size_u64
    )) {
        return false;
    }

    if (segment->file_size == 0) {
        return true;
    }

    if (!process_memory_write(
        memory,
        segment->virtual_address,
        image->data + (size_t) segment->file_offset,
        (size_t) segment->file_size
    )) {
        return false;
    }

    return true;
}

bool elf64_load_segment_validate(
    const struct elf64_image *image,
    const struct elf64_program_header *program_header,
    struct elf64_load_segment *segment)
{
    if (
        image == NULL ||
        program_header == NULL ||
        segment == NULL
    ) {
        return false;
    }

    if (
        program_header->type !=
        ELF64_PROGRAM_TYPE_LOAD
    ) {
        return false;
    }

    if (
        program_header->file_size >
        program_header->memory_size
    ) {
        return false;
    }

    uint64_t file_end;

    if (
        !elf64_loader_add_u64(
            program_header->file_offset,
            program_header->file_size,
            &file_end
        )
    ) {
        return false;
    }

    if (file_end > (uint64_t) image->size) {
        return false;
    }

    uint64_t memory_end;

    if (
        !elf64_loader_add_u64(
            program_header->virtual_address,
            program_header->memory_size,
            &memory_end
        )
    ) {
        return false;
    }

    if (program_header->memory_size == 0) {
        return false;
    }

    if (program_header->file_size > SIZE_MAX) {
        return false;
    }

    if (program_header->memory_size > SIZE_MAX) {
        return false;
    }

    if (
        !process_memory_user_range_valid(
            program_header->virtual_address,
            (size_t) program_header->memory_size
        )
    ) {
        return false;
    }

    if (program_header->alignment > 1) {
        if (!elf64_loader_is_power_of_two(program_header->alignment)) {
            return false;
        }

        if (
            (
                program_header->virtual_address %
                program_header->alignment
            ) !=
            (
                program_header->file_offset %
                program_header->alignment
            )
        ) {
            return false;
        }
    }

    bool writable =
        (
            program_header->flags &
            ELF64_PROGRAM_FLAG_WRITE
        ) != 0;

    bool executable =
        (
            program_header->flags &
            ELF64_PROGRAM_FLAG_EXECUTE
        ) != 0;

    if (
        writable &&
        executable
    ) {
        return false;
    }

    uint64_t mapping_start =
        elf64_loader_align_down(
            program_header->virtual_address
        );

    uint64_t mapping_end;

    if (
        !elf64_loader_align_up(
            memory_end,
            &mapping_end
        )
    ) {
        return false;
    }

    if (
        mapping_end <=
        mapping_start
    ) {
        return false;
    }

    uint64_t mapping_size =
        mapping_end -
        mapping_start;

    uint64_t page_count_u64 =
        mapping_size /
        ELF64_LOADER_PAGE_SIZE;

    if (
        page_count_u64 == 0 ||
        page_count_u64 > SIZE_MAX
    ) {
        return false;
    }

    segment->virtual_address =
        program_header->virtual_address;

    segment->file_offset =
        program_header->file_offset;

    segment->file_size =
        program_header->file_size;

    segment->memory_size =
        program_header->memory_size;

    segment->mapping_start =
        mapping_start;

    segment->mapping_end =
        mapping_end;

    segment->page_count =
        (size_t) page_count_u64;

    segment->writable =
        writable;

    segment->executable =
        executable;

    return true;
}

bool elf64_load_image_validate(
    const struct elf64_image *image)
{
    if (
        image == NULL ||
        image->data == NULL
    ) {
        return false;
    }

    if (image->load_segment_count == 0) {
        return false;
    }

    for (
        size_t first_index = 0;
        first_index < image->program_header_count;
        ++first_index
    ) {
        struct elf64_program_header first_header;

        if (!elf64_program_header_get(
            image,
            first_index,
            &first_header
        )) {
            return false;
        }

        if (first_header.type != ELF64_PROGRAM_TYPE_LOAD) {
            continue;
        }

        struct elf64_load_segment first_segment;

        if (!elf64_load_segment_validate(
            image,
            &first_header,
            &first_segment
        )) {
            return false;
        }

        for (
            size_t second_index = first_index + 1;
            second_index < image->program_header_count;
            ++second_index
        ) {
            struct elf64_program_header second_header;

            if (!elf64_program_header_get(
                image,
                second_index,
                &second_header
            )) {
                return false;
            }

            if (
                second_header.type !=
                ELF64_PROGRAM_TYPE_LOAD
            ) {
                continue;
            }

            struct elf64_load_segment second_segment;

            if (!elf64_load_segment_validate(
                image,
                &second_header,
                &second_segment
            )) {
                return false;
            }

            if (elf64_loader_ranges_overlap(
                first_segment.mapping_start,
                first_segment.mapping_end,
                second_segment.mapping_start,
                second_segment.mapping_end
            )) {
                return false;
            }
        }
    }

    return true;
}

bool elf64_entry_point_validate(
    const struct elf64_image *image)
{
    if (
        image == NULL ||
        image->data == NULL
    ) {
        return false;
    }

    if (!elf64_load_image_validate(image)) {
        return false;
    }

    for (
        size_t index = 0;
        index < image->program_header_count;
        ++index
    ) {
        struct elf64_program_header program_header;

        if (!elf64_program_header_get(
            image,
            index,
            &program_header
        )) {
            return false;
        }

        if (
            program_header.type !=
            ELF64_PROGRAM_TYPE_LOAD
        ) {
            continue;
        }

        struct elf64_load_segment segment;

        if (!elf64_load_segment_validate(
            image,
            &program_header,
            &segment
        )) {
            return false;
        }

        if (!segment.executable) {
            continue;
        }

        uint64_t segment_end;

        if (!elf64_loader_add_u64(
            segment.virtual_address,
            segment.memory_size,
            &segment_end
        )) {
            return false;
        }

        if (
            image->entry_point >=
                segment.virtual_address &&
            image->entry_point <
                segment_end
        ) {
            return true;
        }
    }

    return false;
}

bool elf64_load_image(
    const struct elf64_image *image,
    struct process_memory *memory,
    struct elf64_loaded_image *loaded_image)
{
    if (
        image == NULL ||
        memory == NULL ||
        loaded_image == NULL
    ) {
        return false;
    }

    if (
        loaded_image->segments != NULL ||
        loaded_image->segment_count != 0
    ) {
        return false;
    }

    if (!elf64_load_image_validate(image)) {
        return false;
    }

    if (image->load_segment_count == 0) {
        return false;
    }

    size_t segment_count =
        (size_t) image->load_segment_count;

    size_t metadata_size =
        segment_count *
        sizeof(struct elf64_load_segment);

    struct elf64_load_segment *segments =
        kmalloc(metadata_size);

    if (segments == NULL) {
        return false;
    }

    size_t loaded_segment_count = 0;
    bool loaded_all = true;

    for (
        size_t index = 0;
        index < image->program_header_count;
        ++index
    ) {
        struct elf64_program_header program_header;

        if (!elf64_program_header_get(
            image,
            index,
            &program_header
        )) {
            loaded_all = false;
            break;
        }

        if (
            program_header.type !=
            ELF64_PROGRAM_TYPE_LOAD
        ) {
            continue;
        }

        struct elf64_load_segment segment;

        if (!elf64_load_segment_validate(
            image,
            &program_header,
            &segment
        )) {
            loaded_all = false;
            break;
        }

        if (!elf64_loader_segment_allocate(
            memory,
            &segment
        )) {
            loaded_all = false;
            break;
        }

        if (!elf64_loader_segment_populate(
            image,
            memory,
            &segment
        )) {
            (void) elf64_loader_segment_release(
                memory,
                &segment
            );

            loaded_all = false;
            break;
        }

        if (
            loaded_segment_count >=
            segment_count
        ) {
            (void) elf64_loader_segment_release(
                memory,
                &segment
            );

            loaded_all = false;
            break;
        }

        segments[loaded_segment_count] =
            segment;

        ++loaded_segment_count;
    }

    if (
        loaded_all &&
        loaded_segment_count ==
        segment_count
    ) {
        loaded_image->segments =
            segments;

        loaded_image->segment_count =
            loaded_segment_count;

        return true;
    }

    for (
        size_t remaining =
            loaded_segment_count;
        remaining > 0;
        --remaining
    ) {
        size_t index =
            remaining - 1;

        (void) elf64_loader_segment_release(
            memory,
            &segments[index]
        );
    }

    kfree(segments);

    return false;
}

bool elf64_unload_image(
    struct process_memory *memory,
    struct elf64_loaded_image *loaded_image)
{
    if (
        memory == NULL ||
        loaded_image == NULL
    ) {
        return false;
    }

    if (
        loaded_image->segments == NULL &&
        loaded_image->segment_count == 0
    ) {
        return true;
    }

    if (
        loaded_image->segments == NULL ||
        loaded_image->segment_count == 0
    ) {
        return false;
    }

    bool released_all = true;

    for (
        size_t remaining =
            loaded_image->segment_count;
        remaining > 0;
        --remaining
    ) {
        size_t index =
            remaining - 1;

        if (!elf64_loader_segment_release(
            memory,
            &loaded_image->segments[index]
        )) {
            released_all = false;
        }
    }

    kfree(
        loaded_image->segments
    );

    loaded_image->segments = NULL;
    loaded_image->segment_count = 0;

    return released_all;
}
