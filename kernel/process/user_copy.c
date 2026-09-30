// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include "user_copy.h"

#include "../arch/x86_64/paging.h"
#include "../memory/memory.h"

#include <stddef.h>
#include <stdint.h>

#define USER_COPY_PAGE_SIZE 4096ULL

enum user_copy_access {
    USER_COPY_ACCESS_READ,
    USER_COPY_ACCESS_WRITE
};

// Private functions and helpers declarations
static bool user_copy_translation_permits(
    const struct paging_translation *translation,
    enum user_copy_access access
);

static bool user_copy_range_accessible(
    const struct process_memory *memory,
    uint64_t user_address,
    size_t size,
    enum user_copy_access access
);

static size_t user_copy_page_chunk_size(
    uint64_t virtual_address,
    size_t remaining
);

// Public functions implementations
bool copy_from_user(
    const struct process_memory *memory,
    uint64_t user_address,
    void *destination,
    size_t size)
{
    if (memory == NULL) {
        return false;
    }

    if (size == 0) {
        return true;
    }

    if (destination == NULL) {
        return false;
    }

    if (!user_copy_range_accessible(
        memory,
        user_address,
        size,
        USER_COPY_ACCESS_READ
    )) {
        return false;
    }

    uint8_t *output =
        destination;

    size_t copied = 0;

    while (copied < size) {
        uint64_t current_address =
            user_address +
            (uint64_t) copied;

        struct paging_translation translation;

        if (!paging_translate_address_space(
            &memory->address_space,
            current_address,
            &translation
        )) {
            return false;
        }

        size_t remaining =
            size - copied;

        size_t chunk_size =
            user_copy_page_chunk_size(
                current_address,
                remaining
            );

        const uint8_t *source =
            memory_physical_to_virtual(
                translation.physical_address
            );

        for (
            size_t index = 0;
            index < chunk_size;
            ++index
        ) {
            output[copied + index] =
                source[index];
        }

        copied += chunk_size;
    }

    return true;
}

bool copy_to_user(
    struct process_memory *memory,
    uint64_t user_address,
    const void *source,
    size_t size)
{
    if (memory == NULL) {
        return false;
    }

    if (size == 0) {
        return true;
    }

    if (source == NULL) {
        return false;
    }

    if (!user_copy_range_accessible(
        memory,
        user_address,
        size,
        USER_COPY_ACCESS_WRITE
    )) {
        return false;
    }

    const uint8_t *input =
        source;

    size_t copied = 0;

    while (copied < size) {
        uint64_t current_address =
            user_address +
            (uint64_t) copied;

        struct paging_translation translation;

        if (!paging_translate_address_space(
            &memory->address_space,
            current_address,
            &translation
        )) {
            return false;
        }

        size_t remaining =
            size - copied;

        size_t chunk_size =
            user_copy_page_chunk_size(
                current_address,
                remaining
            );

        uint8_t *destination =
            memory_physical_to_virtual(
                translation.physical_address
            );

        for (
            size_t index = 0;
            index < chunk_size;
            ++index
        ) {
            destination[index] =
                input[copied + index];
        }

        copied += chunk_size;
    }

    return true;
}

// Private functions and helpers implementations
static bool user_copy_translation_permits(
    const struct paging_translation *translation,
    enum user_copy_access access)
{
    if (translation == NULL) {
        return false;
    }

    if (
        translation->page_size !=
        PAGING_PAGE_SIZE_4K
    ) {
        return false;
    }

    const uint64_t entries[] = {
        translation->pml4_entry,
        translation->pdpt_entry,
        translation->pd_entry,
        translation->pt_entry,
    };

    for (
        size_t index = 0;
        index < sizeof(entries) / sizeof(entries[0]);
        ++index
    ) {
        if (
            (entries[index] & PAGE_ENTRY_USER) == 0
        ) {
            return false;
        }

        if (
            access == USER_COPY_ACCESS_WRITE &&
            (entries[index] & PAGE_ENTRY_WRITABLE) == 0
        ) {
            return false;
        }
    }

    return true;
}

static size_t user_copy_page_chunk_size(
    uint64_t virtual_address,
    size_t remaining)
{
    uint64_t page_offset =
        virtual_address &
        (USER_COPY_PAGE_SIZE - 1);

    size_t page_remaining =
        (size_t) (
            USER_COPY_PAGE_SIZE -
            page_offset
        );

    return
        remaining < page_remaining
            ? remaining
            : page_remaining;
}

static bool user_copy_range_accessible(
    const struct process_memory *memory,
    uint64_t user_address,
    size_t size,
    enum user_copy_access access)
{
    if (memory == NULL) {
        return false;
    }

    if (size == 0) {
        return true;
    }

    if (!process_memory_user_range_valid(
        user_address,
        size
    )) {
        return false;
    }

    size_t checked = 0;

    while (checked < size) {
        uint64_t current_address =
            user_address +
            (uint64_t) checked;

        struct paging_translation translation;

        if (!paging_translate_address_space(
            &memory->address_space,
            current_address,
            &translation
        )) {
            return false;
        }

        if (!user_copy_translation_permits(
            &translation,
            access
        )) {
            return false;
        }

        size_t remaining =
            size - checked;

        checked +=
            user_copy_page_chunk_size(
                current_address,
                remaining
            );
    }

    return true;
}
