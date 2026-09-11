// SPDX-License-Identifier: GPL-2.0-only

#include "memory.h"
#include "../memory/memory.h"

#include <stddef.h>

#define PROCESS_MEMORY_PAGE_SIZE 4096ULL

bool process_memory_create(struct process_memory *memory)
{
    if (memory == NULL) return false;

    return paging_address_space_create_with_kernel(
        &memory->address_space
    );
}

bool process_memory_destroy(struct process_memory *memory)
{
    if (memory == NULL) return false;

    return paging_address_space_destroy(
        &memory->address_space
    );
}

bool process_memory_map_page(
    struct process_memory *memory,
    uint64_t virtual_address,
    uint64_t physical_address,
    bool writable)
{
    if (memory == NULL) return false;

    return paging_map_page(
        &memory->address_space,
        virtual_address,
        physical_address,
        writable,
        true,
        false
    );
}

bool process_memory_unmap_page(
    struct process_memory *memory,
    uint64_t virtual_address,
    uint64_t *physical_address)
{
    if (memory == NULL) return false;

    return paging_unmap_page(
        &memory->address_space,
        virtual_address,
        physical_address
    );
}

bool process_memory_allocate_page(
    struct process_memory *memory,
    uint64_t virtual_address,
    bool writable)
{
    if (memory == NULL) return false;

    uint64_t physical_address;

    if (!physical_alloc_frame(&physical_address)) {
        return false;
    }

    if (!process_memory_map_page(
        memory,
        virtual_address,
        physical_address,
        writable
    )) {
        physical_free_frame(physical_address);

        return false;
    }

    return true;
}

bool process_memory_release_page(
    struct process_memory *memory,
    uint64_t virtual_address)
{
    if (memory == NULL) return false;

    uint64_t physical_address;

    if (!process_memory_unmap_page(
        memory,
        virtual_address,
        &physical_address
    )) {
        return false;
    }

    if (!physical_free_frame(physical_address)) {
        return false;
    }

    return true;
}

bool process_memory_allocate_pages(
    struct process_memory *memory,
    uint64_t virtual_address,
    size_t page_count,
    bool writable)
{
    if (memory == NULL) return false;
    if (page_count == 0) return false;
    if ((virtual_address & (PROCESS_MEMORY_PAGE_SIZE - 1)) != 0) return false;

    size_t allocated_pages = 0;

    for (size_t index = 0; index < page_count; ++index) {
        uint64_t page_virtual_address =
            virtual_address + ((uint64_t) index * PROCESS_MEMORY_PAGE_SIZE);

        if (!process_memory_allocate_page(
            memory,
            page_virtual_address,
            writable
        )) {
            break;
        }

        ++allocated_pages;
    }

    if (allocated_pages == page_count) {
        return true;
    }

    for (size_t index = 0; index < allocated_pages; ++index) {
        uint64_t page_virtual_address =
            virtual_address + ((uint64_t) index * PROCESS_MEMORY_PAGE_SIZE);

        if (!process_memory_release_page(
            memory,
            page_virtual_address
        )) {
            return false;
        }
    }

    return false;
}

bool process_memory_allocate_executable_page(
    struct process_memory *memory,
    uint64_t virtual_address)
{
    if (memory == NULL) return false;

    uint64_t physical_address;

    if (!physical_alloc_frame(&physical_address)) {
        return false;
    }

    if (!paging_map_page(
        &memory->address_space,
        virtual_address,
        physical_address,
        false,
        true,
        true
    )) {
        physical_free_frame(physical_address);

        return false;
    }

    return true;
}

bool process_memory_write(
    struct process_memory *memory,
    uint64_t virtual_address,
    const void *source,
    size_t size)
{
    if (memory == NULL) return false;

    if (size == 0) {
        return true;
    }

    if (source == NULL) return false;

    const uint8_t *source_bytes = source;
    size_t remaining = size;
    uint64_t current_virtual_address = virtual_address;

    while (remaining > 0) {
        struct paging_translation translation;

        if (!paging_translate_address_space(
            &memory->address_space,
            current_virtual_address,
            &translation
        )) {
            return false;
        }

        uint64_t page_offset = current_virtual_address & 0xfffULL;
        size_t page_remaining = (size_t) (4096ULL - page_offset);
        size_t chunk_size = remaining < page_remaining
            ? remaining
            : page_remaining;

        uint8_t *destination =
            memory_physical_to_virtual(translation.physical_address);

        for (size_t index = 0; index < chunk_size; ++index) {
            destination[index] = source_bytes[index];
        }

        source_bytes += chunk_size;
        current_virtual_address += chunk_size;
        remaining -= chunk_size;
    }

    return true;
}

bool process_memory_read(
    const struct process_memory *memory,
    uint64_t virtual_address,
    void *destination,
    size_t size)
{
    if (memory == NULL) return false;
    if (destination == NULL && size != 0) return false;

    uint8_t *output = destination;
    size_t copied = 0;

    if (size != 0 && virtual_address > UINT64_MAX - (uint64_t) (size - 1)) {
        return false;
    }

    while (copied < size) {
        uint64_t current_virtual = virtual_address + copied;

        struct paging_translation translation;

        if (!paging_translate_address_space(
            &memory->address_space,
            current_virtual,
            &translation
        )) {
            return false;
        }

        /*
         * For this first version, user memory exposed to syscalls must be a
         * normal 4 KiB mapping.
         */
        if (translation.page_size != PAGING_PAGE_SIZE_4K) {
            return false;
        }

        if ((translation.pt_entry & PAGE_ENTRY_USER) == 0) {
            return false;
        }

        uint64_t page_offset = current_virtual & 0xFFFULL;
        size_t page_remaining = 4096 - (size_t) page_offset;
        size_t remaining = size - copied;
        size_t chunk_size = remaining < page_remaining
            ? remaining
            : page_remaining;

        const uint8_t *source =
            memory_physical_to_virtual(
                translation.physical_address
            );

        for (size_t index = 0; index < chunk_size; ++index) {
            output[copied + index] = source[index];
        }

        copied += chunk_size;
    }

    return true;
}

bool process_memory_release_pages(
    struct process_memory *memory,
    uint64_t virtual_address,
    size_t page_count)
{
    if (memory == NULL) return false;
    if (page_count == 0) return false;
    if ((virtual_address & (PROCESS_MEMORY_PAGE_SIZE - 1)) != 0) return false;

    for (size_t index = 0; index < page_count; ++index) {
        uint64_t page_virtual_address =
            virtual_address + ((uint64_t) index * PROCESS_MEMORY_PAGE_SIZE);

        if (!process_memory_release_page(
            memory,
            page_virtual_address
        )) {
            return false;
        }
    }

    return true;
}
