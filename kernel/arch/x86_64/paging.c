// SPDX-License-Identifier: GPL-2.0-only

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "paging.h"
#include "../../memory/memory.h"

#define PAGING_TABLE_ENTRY_COUNT 512
#define PAGING_PML4_KERNEL_START 256

/* PRIVATE HELPERS */
static bool paging_get_or_create_table(
    uint64_t *parent_table,
    uint16_t index,
    bool user,
    uint64_t **child_table
);
static bool paging_table_is_empty(const uint64_t *table);

static bool paging_translate_from_pml4(
    uint64_t *pml4,
    uint64_t virtual_address,
    struct paging_translation *translation
);
/* END PRIVATE HELPERS */

uint64_t paging_entry_address(uint64_t entry)
{
    return entry & PAGE_ADDRESS_MASK_4K;
}

uint64_t paging_read_cr3(void)
{
    uint64_t value;

    __asm__ volatile (
        "mov %%cr3, %0"
        : "=r"(value)
    );

    return value;
}

uint16_t paging_pml4_index(uint64_t virtual_address)
{
    return (uint16_t) (
        (virtual_address >> 39) & 0x1FF
    );
}

uint16_t paging_pdpt_index(uint64_t virtual_address)
{
    return (uint16_t) (
        (virtual_address >> 30) & 0x1FF
    );
}

uint16_t paging_pd_index(uint64_t virtual_address)
{
    return (uint16_t) (
        (virtual_address >> 21) & 0x1FF
    );
}

uint16_t paging_pt_index(uint64_t virtual_address)
{
    return (uint16_t) (
        (virtual_address >> 12) & 0x1FF
    );
}

uint16_t paging_page_offset(uint64_t virtual_address)
{
    return (uint16_t) (
        virtual_address & 0xFFF
    );
}

bool paging_translate(uint64_t virtual_address, struct paging_translation *translation)
{
    if (translation == NULL) return false;

    uint64_t pml4_physical = paging_read_cr3() & PAGE_ADDRESS_MASK_4K;
    uint64_t *pml4_virtual = memory_physical_to_virtual(pml4_physical);

    return paging_translate_from_pml4(
        pml4_virtual,
        virtual_address,
        translation
    );
}

bool paging_translate_address_space(
    const struct paging_address_space *address_space,
    uint64_t virtual_address,
    struct paging_translation *translation)
{
    if (address_space == NULL) return false;
    if (address_space->pml4_virtual == NULL) return false;
    if (translation == NULL) return false;

    return paging_translate_from_pml4(
        address_space->pml4_virtual,
        virtual_address,
        translation
    );
}

bool paging_create_empty_table(uint64_t *physical_address, uint64_t **virtual_address)
{
    if (physical_address == NULL) {
        return false;
    }

    if (virtual_address == NULL) {
        return false;
    }

    uint64_t table_physical;

    if (!physical_alloc_frame(&table_physical)) {
        return false;
    }

    uint64_t *table_virtual = memory_physical_to_virtual(table_physical);

    for (size_t index = 0; index < PAGING_TABLE_ENTRY_COUNT; ++index) {
        table_virtual[index] = 0;
    }

    *physical_address = table_physical;
    *virtual_address = table_virtual;

    return true;
}

bool paging_address_space_create(struct paging_address_space *address_space)
{
    if (address_space == NULL) return false;

    uint64_t pml4_physical;
    uint64_t *pml4_virtual;

    if (!paging_create_empty_table(&pml4_physical, &pml4_virtual)) {
        return false;
    }

    address_space->pml4_physical = pml4_physical;
    address_space->pml4_virtual = pml4_virtual;

    return true;
}

bool paging_address_space_destroy(struct paging_address_space *address_space)
{
    if (address_space == NULL) return false;
    if (address_space->pml4_virtual == NULL) return false;

    if (!paging_table_is_empty(address_space->pml4_virtual)) {
        return false;
    }

    if (!physical_free_frame(address_space->pml4_physical)) {
        return false;
    }

    address_space->pml4_physical = 0;
    address_space->pml4_virtual = NULL;

    return true;
}

bool paging_address_space_create_with_kernel(
    struct paging_address_space *address_space)
{
    if (address_space == NULL) return false;

    if (!paging_address_space_create(address_space)) {
        return false;
    }

    uint64_t active_pml4_physical = paging_read_cr3() & PAGE_ADDRESS_MASK_4K;
    uint64_t *active_pml4_virtual = memory_physical_to_virtual(active_pml4_physical);

    for (size_t index = PAGING_PML4_KERNEL_START; index < PAGING_TABLE_ENTRY_COUNT; ++index) {
        address_space->pml4_virtual[index] = active_pml4_virtual[index];
    }

    return true;
}

bool paging_map_page(
    struct paging_address_space *address_space,
    uint64_t virtual_address,
    uint64_t physical_address,
    bool writable,
    bool user)
{
    if (address_space == NULL) return false;
    if (address_space->pml4_virtual == NULL) return false;

    if ((virtual_address & 0xfffULL) != 0) return false;
    if ((physical_address & 0xfffULL) != 0) return false;

    uint16_t pml4_index = paging_pml4_index(virtual_address);
    uint16_t pdpt_index = paging_pdpt_index(virtual_address);
    uint16_t pd_index = paging_pd_index(virtual_address);
    uint16_t pt_index = paging_pt_index(virtual_address);

    uint64_t *pdpt;

    if (!paging_get_or_create_table(
        address_space->pml4_virtual,
        pml4_index,
        user,
        &pdpt
    )) {
        return false;
    }

    uint64_t *pd;

    if (!paging_get_or_create_table(
        pdpt,
        pdpt_index,
        user,
        &pd
    )) {
        return false;
    }

    uint64_t *pt;

    if (!paging_get_or_create_table(
        pd,
        pd_index,
        user,
        &pt
    )) {
        return false;
    }

    if ((pt[pt_index] & PAGE_ENTRY_PRESENT) != 0) {
        return false;
    }

    pt[pt_index] = paging_make_page_entry(
        physical_address,
        writable,
        user
    );

    return true;
}

bool paging_unmap_page(
    struct paging_address_space *address_space,
    uint64_t virtual_address,
    uint64_t *physical_address)
{
    if (address_space == NULL) return false;
    if (address_space->pml4_virtual == NULL) return false;

    if (physical_address == NULL) return false;
    if ((virtual_address & 0xfffULL) != 0) return false;

    uint16_t pml4_index = paging_pml4_index(virtual_address);
    uint16_t pdpt_index = paging_pdpt_index(virtual_address);
    uint16_t pd_index = paging_pd_index(virtual_address);
    uint16_t pt_index = paging_pt_index(virtual_address);

    uint64_t pml4_entry = address_space->pml4_virtual[pml4_index];

    if ((pml4_entry & PAGE_ENTRY_PRESENT) == 0) {
        return false;
    }

    uint64_t pdpt_physical = paging_entry_address(pml4_entry);
    uint64_t *pdpt = memory_physical_to_virtual(pdpt_physical);

    uint64_t pdpt_entry = pdpt[pdpt_index];

    if ((pdpt_entry & PAGE_ENTRY_PRESENT) == 0) {
        return false;
    }

    uint64_t pd_physical = paging_entry_address(pdpt_entry);
    uint64_t *pd = memory_physical_to_virtual(pd_physical);

    uint64_t pd_entry = pd[pd_index];

    if ((pd_entry & PAGE_ENTRY_PRESENT) == 0) {
        return false;
    }

    uint64_t pt_physical = paging_entry_address(pd_entry);
    uint64_t *pt = memory_physical_to_virtual(pt_physical);

    uint64_t page_entry = pt[pt_index];

    if ((page_entry & PAGE_ENTRY_PRESENT) == 0) {
        return false;
    }

    *physical_address = paging_entry_address(page_entry);
    pt[pt_index] = 0;

    if (paging_table_is_empty(pt)) {
        physical_free_frame(pt_physical);
        pd[pd_index] = 0;
    }

    if (paging_table_is_empty(pd)) {
        physical_free_frame(pd_physical);
        pdpt[pdpt_index] = 0;
    }

    if (paging_table_is_empty(pdpt)) {
        physical_free_frame(pdpt_physical);
        address_space->pml4_virtual[pml4_index] = 0;
    }

    return true;
}

uint64_t paging_make_table_entry(uint64_t table_physical_address, bool writable, bool user)
{
    uint64_t entry =
        table_physical_address &
        PAGE_ADDRESS_MASK_4K;

    entry |= PAGE_ENTRY_PRESENT;

    if (writable) {
        entry |= PAGE_ENTRY_WRITABLE;
    }

    if (user) {
        entry |= PAGE_ENTRY_USER;
    }

    return entry;
}

uint64_t paging_make_page_entry(uint64_t page_physical_address, bool writable, bool user)
{
    uint64_t entry = page_physical_address & PAGE_ADDRESS_MASK_4K;

    entry |= PAGE_ENTRY_PRESENT;

    if (writable) {
        entry |= PAGE_ENTRY_WRITABLE;
    }

    if (user) {
        entry |= PAGE_ENTRY_USER;
    }

    return entry;
}

static bool paging_get_or_create_table(
    uint64_t *parent_table,
    uint16_t index,
    bool user,
    uint64_t **child_table)
{
    if (parent_table == NULL) return false;
    if (child_table == NULL) return false;

    uint64_t entry = parent_table[index];

    if ((entry & PAGE_ENTRY_PRESENT) != 0) {
        if (user) {
            parent_table[index] |= PAGE_ENTRY_USER;
        }

        uint64_t child_physical = paging_entry_address(parent_table[index]);

        *child_table = memory_physical_to_virtual(child_physical);

        return true;
    }

    uint64_t child_physical;
    uint64_t *child_virtual;

    if (!paging_create_empty_table(&child_physical, &child_virtual)) {
        return false;
    }

    parent_table[index] = paging_make_table_entry(
        child_physical,
        true,
        user
    );

    *child_table = child_virtual;

    return true;
}

static bool paging_table_is_empty(const uint64_t *table)
{
    if (table == NULL) return true;

    for (size_t index = 0; index < PAGING_TABLE_ENTRY_COUNT; ++index) {
        if ((table[index] & PAGE_ENTRY_PRESENT) != 0) {
            return false;
        }
    }

    return true;
}

static bool paging_translate_from_pml4(
    uint64_t *pml4,
    uint64_t virtual_address,
    struct paging_translation *translation
) {
    uint64_t pml4_entry = pml4[paging_pml4_index(virtual_address)];
    translation->pml4_entry = pml4_entry;

    if ((pml4_entry & PAGE_ENTRY_PRESENT) == 0) {
        return false;
    }

    uint64_t pdpt_physical = paging_entry_address(pml4_entry);
    uint64_t *pdpt = memory_physical_to_virtual(pdpt_physical);
    uint64_t pdpt_entry = pdpt[paging_pdpt_index(virtual_address)];
    translation->pdpt_entry = pdpt_entry;

    if ((pdpt_entry & PAGE_ENTRY_PRESENT) == 0) {
        return false;
    }

    if ((pdpt_entry & PAGE_ENTRY_HUGE) != 0) {
        uint64_t page_physical = pdpt_entry & PAGE_ADDRESS_MASK_1G;
        uint64_t offset = virtual_address & ((1ULL << 30) - 1);
        translation->physical_address = page_physical + offset;
        translation->page_size = PAGING_PAGE_SIZE_1G;

        return true;
    }

    uint64_t pd_physical = paging_entry_address(pdpt_entry);
    uint64_t *pd = memory_physical_to_virtual(pd_physical);
    uint64_t pd_entry = pd[paging_pd_index(virtual_address)];
    translation->pd_entry = pd_entry;

    if ((pd_entry & PAGE_ENTRY_PRESENT) == 0) {
        return false;
    }

    if ((pd_entry & PAGE_ENTRY_HUGE) != 0) {
        uint64_t page_physical = pd_entry & PAGE_ADDRESS_MASK_2M;
        uint64_t offset = virtual_address & ((1ULL << 21) - 1);
        translation->physical_address = page_physical + offset;
        translation->page_size = PAGING_PAGE_SIZE_2M;

        return true;
    }

    uint64_t pt_physical = paging_entry_address(pd_entry);
    uint64_t *pt = memory_physical_to_virtual(pt_physical);
    uint64_t pt_entry = pt[paging_pt_index(virtual_address)];
    translation->pt_entry = pt_entry;

    if ((pt_entry & PAGE_ENTRY_PRESENT) == 0) {
        return false;
    }

    uint64_t page_physical = paging_entry_address(pt_entry);
    uint64_t offset = paging_page_offset(virtual_address);
    translation->physical_address = page_physical + offset;
    translation->page_size = PAGING_PAGE_SIZE_4K;

    return true;
}
