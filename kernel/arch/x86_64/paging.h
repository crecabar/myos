// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file paging.h
 * @brief x86-64 virtual memory and address-space management.
 *
 * Defines the public paging API used by MyOS to inspect and modify page
 * tables, manage address spaces, map virtual pages, and control CR3/TLB state.
 */

#ifndef MYOS_ARCH_X86_64_PAGING_H
#define MYOS_ARCH_X86_64_PAGING_H

#include <stdbool.h>
#include <stdint.h>

#define PAGE_ADDRESS_MASK_4K  0x000ffffffffff000ULL
#define PAGE_ADDRESS_MASK_2M  0x000fffffffe00000ULL
#define PAGE_ADDRESS_MASK_1G  0x000fffffc0000000ULL

#define PAGE_ENTRY_PRESENT       (1ULL << 0)
#define PAGE_ENTRY_WRITABLE      (1ULL << 1)
#define PAGE_ENTRY_USER          (1ULL << 2)
#define PAGE_ENTRY_WRITE_THROUGH (1ULL << 3)
#define PAGE_ENTRY_CACHE_DISABLE (1ULL << 4)
#define PAGE_ENTRY_ACCESSED      (1ULL << 5)
#define PAGE_ENTRY_DIRTY         (1ULL << 6)
#define PAGE_ENTRY_HUGE          (1ULL << 7)
#define PAGE_ENTRY_NO_EXECUTE    (1ULL << 63)

enum paging_page_size {
    PAGING_PAGE_SIZE_4K,
    PAGING_PAGE_SIZE_2M,
    PAGING_PAGE_SIZE_1G,
};

enum paging_cache_type {
    PAGING_CACHE_WRITE_BACK,
    PAGING_CACHE_WRITE_COMBINING,
    PAGING_CACHE_UNCACHEABLE,
};

struct paging_translation {
    uint64_t physical_address;
    enum paging_page_size page_size;

    uint64_t pml4_entry;
    uint64_t pdpt_entry;
    uint64_t pd_entry;
    uint64_t pt_entry;
};

/**
 * Represents an x86-64 virtual address space rooted at a PML4 table.
 *
 * The physical address is suitable for loading into CR3.
 * The virtual address points to the same PML4 through the kernel direct map
 * and is used by MyOS to inspect and modify the table.
 *
 * Address spaces may reference a shared kernel higher half. In that case,
 * PML4 entries 256 through 511 remain visible but are not owned for mutation
 * by this address space.
 */
struct paging_address_space {
    uint64_t pml4_physical;
    uint64_t *pml4_virtual;

    /**
     * Whether PML4 entries 256 through 511 form a non-owned shared kernel half.
     *
     * When true, the entries remain available for translation and kernel
     * execution but must not be mutated or reclaimed through this address
     * space.
     */
    bool kernel_half_shared;
};

/**
 * Initializes the MyOS paging subsystem and activates the kernel-owned
 * address space.
 *
 * Creates a new PML4 root, inherits the shared kernel mappings from the
 * boot-time address space, and installs the new root in CR3.
 *
 * @return true on success; false if paging is already initialized or the
 *         kernel address space cannot be created or activated.
 */
bool paging_init(void);

uint64_t paging_entry_address(uint64_t entry);

struct paging_address_space *paging_kernel_address_space(void);

/**
 * Returns the physical address of the boot-time PML4 root that was active
 * before MyOS installed its own kernel address-space root.
 *
 * The returned frame remains owned by the boot environment until explicitly
 * reclaimed.
 *
 * @param physical_address Receives the boot-time PML4 physical address.
 *
 * @return true when paging is initialized and the address is available; false
 *         otherwise.
 */
bool paging_boot_pml4_physical(
    uint64_t *physical_address
);

uint64_t paging_read_cr3(void);

void paging_invalidate_page(uint64_t virtual_address);

uint16_t paging_pml4_index(uint64_t virtual_address);
uint16_t paging_pdpt_index(uint64_t virtual_address);
uint16_t paging_pd_index(uint64_t virtual_address);
uint16_t paging_pt_index(uint64_t virtual_address);

uint16_t paging_page_offset(uint64_t virtual_address);

bool paging_translate(uint64_t virtual_address, struct paging_translation *translation);

bool paging_translate_address_space(
    const struct paging_address_space *address_space,
    uint64_t virtual_address,
    struct paging_translation *translation
);

bool paging_address_space_activate(const struct paging_address_space *address_space);

/**
 * Transfers one owned PML4 branch from one address space to another.
 *
 * The source entry must be present and mutable by the source address space.
 * The destination entry must also be mutable by the destination address
 * space. The source entry is cleared after the transfer.
 *
 * The previous destination entry is returned to the caller without being
 * reclaimed. Ownership of that replaced branch therefore remains the
 * caller's responsibility.
 *
 * This operation does not invalidate the TLB or reload CR3.
 *
 * @param destination Address space receiving the branch.
 * @param source Address space relinquishing the branch.
 * @param pml4_index PML4 entry to transfer.
 * @param replaced_entry Receives the previous destination entry.
 *
 * @return true when ownership was transferred; false otherwise.
 */
bool paging_address_space_transfer_pml4_branch(
    struct paging_address_space *destination,
    struct paging_address_space *source,
    uint16_t pml4_index,
    uint64_t *replaced_entry
);

bool paging_create_empty_table(uint64_t *physical_address, uint64_t **virtual_address);

bool paging_address_space_create(struct paging_address_space *address_space);

/**
 * Destroys an address space and releases its PML4 root frame.
 *
 * The private lower half of the address space must already be empty.
 * Shared kernel mappings in PML4 entries 256 through 511 are not released.
 *
 * @param address_space Address space to destroy.
 *
 * @return true when the root was released successfully; false otherwise.
 */
bool paging_address_space_destroy(struct paging_address_space *address_space);

/**
 * Creates a new address space with an empty private lower half and the
 * kernel mappings inherited in the upper half.
 *
 * PML4 entries 256 through 511 are marked as a shared kernel half. They remain
 * available for translation but must not be mutated through the returned
 * address space.
 *
 * @param address_space Receives the newly created address space.
 *
 * @return true on success; false when the root table cannot be allocated.
 */
bool paging_address_space_create_with_kernel(struct paging_address_space *address_space);

/**
 * Maps one 4 KiB virtual page to a physical frame in an address space.
 *
 * Missing intermediate page tables are allocated automatically.
 * The virtual and physical addresses must both be 4 KiB aligned.
 * Existing mappings are not overwritten.
 *
 * Mapping is rejected if an intermediate entry represents a huge page,
 * because this API only creates 4 KiB mappings.
 *
 * Address spaces with a shared kernel higher half cannot create mappings
 * through PML4 entries 256 through 511.
 *
 * When user is true, newly created intermediate page-table entries are marked
 * user-accessible. Existing intermediate entries must already permit user
 * access; supervisor-only branches are never promoted implicitly.
 *
 * @param address_space Address space to modify.
 * @param virtual_address 4 KiB-aligned virtual page address.
 * @param physical_address 4 KiB-aligned physical frame address.
 * @param writable Whether the resulting page mapping is writable.
 * @param user Whether the mapping is accessible from user mode.
 * @param executable Whether is executable from user mode.
 *
 * @return true when the mapping was created; false otherwise.
 */
bool paging_map_page(
    struct paging_address_space *address_space,
    uint64_t virtual_address,
    uint64_t physical_address,
    bool writable,
    bool user,
    bool executable
);

/**
 * Maps one 4 KiB device page into the kernel address space.
 *
 * The mapping is supervisor-only, writable, and non-executable. Cache
 * attributes are selected from the active IA32_PAT configuration according
 * to the requested semantic memory type.
 *
 * @param virtual_address Kernel virtual page address.
 * @param physical_address Physical device page address.
 * @param cache_type Required cache policy.
 *
 * @return true when the mapping was created; false otherwise.
 */
bool paging_map_kernel_device_page(
    uint64_t virtual_address,
    uint64_t physical_address,
    enum paging_cache_type cache_type
);

/**
 * Removes one 4 KiB page mapping from an address space.
 *
 * Unmapping is rejected if the walk encounters a huge-page mapping.
 * This API only removes 4 KiB page mappings.
 *
 * Empty intermediate PT, PD, and PDPT tables owned by the address space are
 * reclaimed automatically. The mapped data frame itself is not released.
 *
 * Address spaces with a shared kernel higher half cannot remove mappings
 * through PML4 entries 256 through 511.
 *
 * If no present 4 KiB leaf mapping exists, the operation is rejected and
 * the physical_address output is left unchanged.
 *
 * @param address_space Address space to modify.
 * @param virtual_address 4 KiB-aligned virtual page address to unmap.
 * @param physical_address Receives the physical frame previously mapped.
 *
 * @return true when a mapping existed and was removed; false otherwise.
 */
bool paging_unmap_page(
    struct paging_address_space *address_space,
    uint64_t virtual_address,
    uint64_t *physical_address
);

uint64_t paging_make_table_entry(uint64_t table_physical_address, bool writable, bool user);

/**
 * Creates a 4 KiB page-table entry for a physical frame.
 *
 * The entry is marked present and receives the requested write, user, and
 * execute permissions. Non-executable pages receive the x86-64 NX bit.
 *
 * @param page_physical_address 4 KiB-aligned physical frame address.
 * @param writable Whether writes are permitted.
 * @param user Whether user-mode access is permitted.
 * @param executable Whether instruction execution is permitted.
 *
 * @return Encoded x86-64 page-table entry.
 */
uint64_t paging_make_page_entry(
    uint64_t page_physical_address,
    bool writable,
    bool user,
    bool executable
);

#endif
