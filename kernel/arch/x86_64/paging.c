// SPDX-License-Identifier: GPL-2.0-only

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "paging.h"

uint64_t paging_entry_address(uint64_t entry)
{
    return entry & 0x000ffffffffff000ULL;
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

bool paging_translate(
    uint64_t virtual_address,
    uint64_t hhdm_offset,
    struct paging_translation *translation)
{
    if (translation == NULL) {
        return false;
    }

    uint64_t cr3 =
        paging_read_cr3();

    uint64_t pml4_physical =
        cr3 & 0x000ffffffffff000ULL;

    uint64_t *pml4 =
        (uint64_t *) (pml4_physical + hhdm_offset);

    uint64_t pml4_entry =
        pml4[paging_pml4_index(virtual_address)];

    translation->pml4_entry =
        pml4_entry;

    if ((pml4_entry & PAGE_ENTRY_PRESENT) == 0) {
        return false;
    }

    uint64_t pdpt_physical =
        paging_entry_address(pml4_entry);

    uint64_t *pdpt =
        (uint64_t *) (pdpt_physical + hhdm_offset);

    uint64_t pdpt_entry =
        pdpt[paging_pdpt_index(virtual_address)];

    translation->pdpt_entry =
        pdpt_entry;

    if ((pdpt_entry & PAGE_ENTRY_PRESENT) == 0) {
        return false;
    }

    if ((pdpt_entry & PAGE_ENTRY_HUGE) != 0) {
        return false;
    }

    uint64_t pd_physical =
        paging_entry_address(pdpt_entry);

    uint64_t *pd =
        (uint64_t *) (pd_physical + hhdm_offset);

    uint64_t pd_entry =
        pd[paging_pd_index(virtual_address)];

    translation->pd_entry =
        pd_entry;

    if ((pd_entry & PAGE_ENTRY_PRESENT) == 0) {
        return false;
    }

    if ((pd_entry & PAGE_ENTRY_HUGE) != 0) {
        return false;
    }

    uint64_t pt_physical =
        paging_entry_address(pd_entry);

    uint64_t *pt =
        (uint64_t *) (pt_physical + hhdm_offset);

    uint64_t pt_entry =
        pt[paging_pt_index(virtual_address)];

    translation->pt_entry =
        pt_entry;

    if ((pt_entry & PAGE_ENTRY_PRESENT) == 0) {
        return false;
    }

    uint64_t page_physical =
        paging_entry_address(pt_entry);

    uint64_t offset =
        paging_page_offset(virtual_address);

    translation->physical_address =
        page_physical + offset;

    translation->page_size =
        PAGING_PAGE_SIZE_4K;

    return true;
}
