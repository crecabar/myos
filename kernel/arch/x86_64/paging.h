// SPDX-License-Identifier: GPL-2.0-only

#ifndef MYOS_ARCH_X86_64_PAGING_H
#define MYOS_ARCH_X86_64_PAGING_H

#include <stdbool.h>
#include <stdint.h>

#define PAGE_ADDRESS_MASK_4K  0x000ffffffffff000ULL
#define PAGE_ADDRESS_MASK_2M  0x000fffffffe00000ULL
#define PAGE_ADDRESS_MASK_1G  0x000fffffc0000000ULL

#define PAGE_ENTRY_PRESENT  (1ULL << 0)
#define PAGE_ENTRY_WRITABLE (1ULL << 1)
#define PAGE_ENTRY_USER     (1ULL << 2)
#define PAGE_ENTRY_HUGE     (1ULL << 7)
#define PAGE_ENTRY_ACCESSED (1ULL << 5)

enum paging_page_size {
    PAGING_PAGE_SIZE_4K,
    PAGING_PAGE_SIZE_2M,
    PAGING_PAGE_SIZE_1G,
};

struct paging_translation {
    uint64_t physical_address;
    enum paging_page_size page_size;

    uint64_t pml4_entry;
    uint64_t pdpt_entry;
    uint64_t pd_entry;
    uint64_t pt_entry;
};

uint64_t paging_entry_address(uint64_t entry);

uint64_t paging_read_cr3(void);

uint16_t paging_pml4_index(uint64_t virtual_address);
uint16_t paging_pdpt_index(uint64_t virtual_address);
uint16_t paging_pd_index(uint64_t virtual_address);
uint16_t paging_pt_index(uint64_t virtual_address);

uint16_t paging_page_offset(uint64_t virtual_address);

bool paging_translate(
    uint64_t virtual_address,
    struct paging_translation *translation
);

#endif
