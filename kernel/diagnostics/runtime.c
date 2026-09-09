// SPDX-License-Identifier: GPL-2.0-only

#include "runtime.h"
#include "diagnostics.h"

#include "../arch/x86_64/paging.h"
#include "../core/panic.h"
#include "../memory/memory.h"

#include <stdint.h>
#include <stddef.h>

extern char __kernel_start[];
extern char __kernel_end[];

/* Helpers and private functions */
static uint16_t read_cs(void);
static void dump_page_size(enum paging_page_size page_size);
static void runtime_dump_kernel_layout(void);
static void runtime_dump_paging(void);
static void runtime_test_page_table_chain(void);
static void runtime_test_kernel_address_space(void);
static uint64_t read_rsp(void);
/* END HELPERS */

void runtime_diagnostics_dump(void)
{
    diagnostics_write("\n=== Runtime diagnostics ===\n");

    runtime_dump_kernel_layout();
    runtime_dump_paging();

    diagnostics_write("\n--- Physical memory ---\n");
    memory_dump_map();
    memory_dump_usable_frames();

    diagnostics_write("\n--- Paging experiment ---\n");
    runtime_test_page_table_chain();

    diagnostics_write("\n--- Kernel address space experiment ---\n");
    runtime_test_kernel_address_space();
}

static uint16_t read_cs(void)
{
    uint16_t cs;

    __asm__ volatile (
        "mov %%cs, %0"
        : "=r"(cs)
    );

    return cs;
}

static uint64_t read_rsp(void)
{
    uint64_t rsp;

    __asm__ volatile (
        "mov %%rsp, %0"
        : "=r"(rsp)
    );

    return rsp;
}

static void dump_page_size(enum paging_page_size page_size)
{
    switch (page_size) {
        case PAGING_PAGE_SIZE_4K:
            diagnostics_write(
                "  Page size=4 KiB\n"
            );
            break;

        case PAGING_PAGE_SIZE_2M:
            diagnostics_write(
                "  Page size=2 MiB\n"
            );
            break;

        case PAGING_PAGE_SIZE_1G:
            diagnostics_write(
                "  Page size=1 GiB\n"
            );
            break;
    }
}

static void runtime_dump_kernel_layout(void)
{
    uintptr_t kernel_start = (uintptr_t) __kernel_start;
    uintptr_t kernel_end = (uintptr_t) __kernel_end;
    uint64_t kernel_size = (uint64_t) (kernel_end - kernel_start);

    diagnostics_printf(
        "Kernel start=%x\n"
        "Kernel end=%x\n"
        "Kernel size=%u bytes\n",
        (uint64_t) kernel_start,
        (uint64_t) kernel_end,
        kernel_size
    );

    diagnostics_printf(
        "CS=%x\n",
        (uint64_t) read_cs()
    );
}

static void runtime_dump_paging(void)
{
    uint64_t virtual_address = (uint64_t) __kernel_start;
    struct paging_translation translation;

    if (!paging_translate(virtual_address, &translation)) {
        kernel_panic("Unable to translate kernel address");
    }

    dump_page_size(translation.page_size);

    diagnostics_printf(
        "Paging translation:\n"
        "  VA=%x\n"
        "  PA=%x\n"
        "  PML4E=%x\n"
        "  PDPTE=%x\n"
        "  PDE=%x\n"
        "  PTE=%x\n",
        virtual_address,
        translation.physical_address,
        translation.pml4_entry,
        translation.pdpt_entry,
        translation.pd_entry,
        translation.pt_entry
    );
}

static void runtime_test_page_table_chain(void)
{
    uint64_t free_before = physical_free_frame_count();

    struct paging_address_space address_space;

    if (!paging_address_space_create(&address_space)) {
        kernel_panic("Unable to create test address space");
    }

    uint64_t data_physical;

    if (!physical_alloc_frame(&data_physical)) {
        kernel_panic("Unable to allocate test data frame");
    }

    uint64_t *data_virtual = memory_physical_to_virtual(data_physical);

    uint64_t test_virtual_address = 0x0000000040203000ULL;

    if (!paging_map_page(
        &address_space,
        test_virtual_address,
        data_physical,
        true,
        false
    )) {
        kernel_panic("Unable to map test page");
    }

    uint64_t free_after_mapping = physical_free_frame_count();

    if (free_after_mapping != free_before - 5) {
        kernel_panic("Page mapping allocated unexpected frame count");
    }

    data_virtual[0] = 0x1122334455667788ULL;

    uint16_t pml4_index = paging_pml4_index(test_virtual_address);
    uint16_t pdpt_index = paging_pdpt_index(test_virtual_address);
    uint16_t pd_index = paging_pd_index(test_virtual_address);
    uint16_t pt_index = paging_pt_index(test_virtual_address);

    uint64_t decoded_pdpt_physical = paging_entry_address(address_space.pml4_virtual[pml4_index]);
    uint64_t *decoded_pdpt_virtual = memory_physical_to_virtual(decoded_pdpt_physical);
    uint64_t decoded_pd_physical = paging_entry_address(decoded_pdpt_virtual[pdpt_index]);
    uint64_t *decoded_pd_virtual = memory_physical_to_virtual(decoded_pd_physical);
    uint64_t decoded_pt_physical = paging_entry_address(decoded_pd_virtual[pd_index]);
    uint64_t *decoded_pt_virtual = memory_physical_to_virtual(decoded_pt_physical);
    uint64_t decoded_data_physical = paging_entry_address(decoded_pt_virtual[pt_index]);
    uint64_t *decoded_data_virtual = memory_physical_to_virtual(decoded_data_physical);

    if (decoded_data_physical != data_physical) {
        kernel_panic("Page table chain resolved wrong physical frame");
    }

    if (decoded_data_virtual[0] != 0x1122334455667788ULL) {
        kernel_panic("Page table chain resolved wrong data");
    }

    diagnostics_printf(
        "Complete page table chain test:\n"
        "  test VA=%x\n"
        "  PML4 PA=%x index=%u\n"
        "  PDPT PA=%x index=%u\n"
        "  PD PA=%x index=%u\n"
        "  PT PA=%x index=%u\n"
        "  data PA=%x\n"
        "  data VA=%x\n"
        "  resolved PA=%x\n"
        "  value=%x\n",
        test_virtual_address,
        address_space.pml4_physical,
        (uint64_t) pml4_index,
        decoded_pdpt_physical,
        (uint64_t) pdpt_index,
        decoded_pd_physical,
        (uint64_t) pd_index,
        decoded_pt_physical,
        (uint64_t) pt_index,
        data_physical,
        (uint64_t) data_virtual,
        decoded_data_physical,
        decoded_data_virtual[0]
    );

    uint64_t unmapped_physical;

    if (!paging_unmap_page(
        &address_space,
        test_virtual_address,
        &unmapped_physical
    )) {
        kernel_panic("Unable to unmap test page");
    }

    if (unmapped_physical != data_physical) {
        kernel_panic("Unmapped page returned wrong physical frame");
    }

    if ((address_space.pml4_virtual[pml4_index] & PAGE_ENTRY_PRESENT) != 0) {
        kernel_panic("Empty page table hierarchy was not reclaimed");
    }

    if (!physical_free_frame(unmapped_physical)) {
        kernel_panic("Unable to release unmapped physical frame");
    }

    uint64_t free_after_reclaim = physical_free_frame_count();

    if (free_after_reclaim != free_before - 1) {
        kernel_panic("Page table frames were not reclaimed correctly");
    }

    if (!paging_address_space_destroy(&address_space)) {
        kernel_panic("Unable to destroy test address space");
    }

    uint64_t free_after_destroy = physical_free_frame_count();

    if (free_after_destroy != free_before) {
        kernel_panic("Address space root frame was not reclaimed");
    }

    diagnostics_printf(
        "Page unmap and address space destroy test:\n"
        "  VA=%x\n"
        "  unmapped PA=%x\n"
        "  free before=%u\n"
        "  free after mapping=%u\n"
        "  free after reclaim=%u\n"
        "  free after destroy=%u\n",
        test_virtual_address,
        unmapped_physical,
        free_before,
        free_after_mapping,
        free_after_reclaim,
        free_after_destroy
    );
}

static void runtime_test_kernel_address_space(void)
{
    struct paging_address_space address_space;

    if (!paging_address_space_create_with_kernel(&address_space)) {
        kernel_panic("Unable to create kernel-aware address space");
    }

    /*
     * Kernel mapping
     */
    uint64_t kernel_virtual_address = (uint64_t) __kernel_start;

    struct paging_translation active_kernel_translation;
    struct paging_translation inherited_kernel_translation;

    if (!paging_translate(
        kernel_virtual_address,
        &active_kernel_translation
    )) {
        kernel_panic("Unable to translate kernel in active address space");
    }

    if (!paging_translate_address_space(
        &address_space,
        kernel_virtual_address,
        &inherited_kernel_translation
    )) {
        kernel_panic("Kernel missing from inherited address space");
    }

    if (active_kernel_translation.physical_address !=
        inherited_kernel_translation.physical_address) {
        kernel_panic("Inherited kernel mapping differs from active mapping");
    }

    uint16_t pml4_index = paging_pml4_index(kernel_virtual_address);

    uint64_t active_pml4_physical = paging_read_cr3() & 0x000ffffffffff000ULL;
    uint64_t *active_pml4_virtual = memory_physical_to_virtual(active_pml4_physical);

    uint64_t active_entry = active_pml4_virtual[pml4_index];
    uint64_t copied_entry = address_space.pml4_virtual[pml4_index];

    if (copied_entry != active_entry) {
        kernel_panic("Kernel PML4 entry was not inherited correctly");
    }

    /*
     * Kernel stack mapping
     */
    uint64_t stack_virtual_address = read_rsp();

    struct paging_translation active_stack_translation;
    struct paging_translation inherited_stack_translation;

    if (!paging_translate(
        stack_virtual_address,
        &active_stack_translation
    )) {
        kernel_panic("Unable to translate active kernel stack");
    }

    if (!paging_translate_address_space(
        &address_space,
        stack_virtual_address,
        &inherited_stack_translation
    )) {
        kernel_panic("Kernel stack missing from inherited address space");
    }

    if (active_stack_translation.physical_address !=
        inherited_stack_translation.physical_address) {
        kernel_panic("Inherited kernel stack mapping differs");
    }

    /*
     * Direct map
     */
    uint64_t direct_map_virtual_address = (uint64_t) memory_physical_to_virtual(address_space.pml4_physical);

    struct paging_translation inherited_direct_map_translation;

    if (!paging_translate_address_space(
        &address_space,
        direct_map_virtual_address,
        &inherited_direct_map_translation
    )) {
        kernel_panic("Direct map missing from inherited address space");
    }

    if (inherited_direct_map_translation.physical_address !=
        address_space.pml4_physical) {
        kernel_panic("Inherited direct map resolved wrong physical address");
    }

    diagnostics_printf(
        "Kernel address space execution test:\n"
        "  kernel VA=%x\n"
        "  active kernel PA=%x\n"
        "  inherited kernel PA=%x\n"
        "  PML4 index=%u\n"
        "  active PML4 entry=%x\n"
        "  copied PML4 entry=%x\n"
        "  stack VA=%x\n"
        "  active stack PA=%x\n"
        "  inherited stack PA=%x\n"
        "  direct map VA=%x\n"
        "  direct map PA=%x\n",
        kernel_virtual_address,
        active_kernel_translation.physical_address,
        inherited_kernel_translation.physical_address,
        (uint64_t) pml4_index,
        active_entry,
        copied_entry,
        stack_virtual_address,
        active_stack_translation.physical_address,
        inherited_stack_translation.physical_address,
        direct_map_virtual_address,
        inherited_direct_map_translation.physical_address
    );
}
