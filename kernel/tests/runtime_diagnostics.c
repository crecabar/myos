// SPDX-License-Identifier: GPL-2.0-only

#include "runtime_diagnostics.h"

#include "../arch/x86_64/gdt.h"
#include "../arch/x86_64/paging.h"
#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../memory/memory.h"
#include "../memory/kernel_mapping.h"
#include "../process/layout.h"
#include "../process/memory.h"
#include "../process/stack.h"

#include <stddef.h>
#include <stdint.h>

extern char __kernel_start[];
extern char __kernel_end[];

extern char __text_start[];
extern char __text_end[];

extern char __rodata_start[];
extern char __rodata_end[];

extern char __limine_requests_start[];
extern char __limine_requests_end[];

extern char __data_start[];
extern char __data_end[];

extern char __bss_start[];
extern char __bss_end[];

/* Helpers and private functions */
static uint16_t read_cs(void);
static uint64_t read_rsp(void);
static uint16_t read_ss(void);
static uint16_t read_tr(void);
static void dump_page_size(enum paging_page_size page_size);
static void runtime_dump_kernel_layout(void);
static void runtime_dump_paging(void);
static const char *runtime_page_size_name(
    enum paging_page_size page_size
);

static bool runtime_translation_writable(
    const struct paging_translation *translation
);

static bool runtime_translation_user(
    const struct paging_translation *translation
);

static bool runtime_translation_executable(
    const struct paging_translation *translation
);

static void runtime_dump_mapping_point(
    const char *name,
    uint64_t virtual_address
);

static void runtime_dump_mapping_range(
    const char *name,
    uint64_t start,
    uint64_t end
);

static void runtime_dump_page_table_topology(
    const char *name,
    uint64_t virtual_address
);

static void runtime_dump_present_kernel_branches(void);
static void runtime_dump_kernel_mapping_inventory(void);
static void runtime_test_kernel_owned_mapping_build(void);
static void runtime_test_kernel_partial_mapping_destroy(void);
static void runtime_test_page_table_chain(void);
static void runtime_test_kernel_address_space(void);
static void runtime_test_active_page_mapping(void);
static void runtime_test_kernel_aware_address_space_destroy(void);
static void runtime_test_process_like_address_space(void);
static void runtime_test_process_memory_lifecycle(void);
static void runtime_test_process_memory_mapping(void);
static void runtime_test_process_memory_owned_page(void);
static void runtime_test_process_memory_range(void);
static void runtime_test_process_stack(void);
static void runtime_test_process_layout(void);
static void runtime_test_gdt(void);
/* END HELPERS */

void runtime_diagnostics_run(void)
{
    diagnostics_write("[tests] Starting runtime diagnostics\n");
    diagnostics_write("\n=== Runtime diagnostics ===\n");

    runtime_dump_kernel_layout();
    runtime_dump_paging();

    diagnostics_write(
        "\n--- Kernel mapping inventory ---\n"
    );

    runtime_dump_kernel_mapping_inventory();

    diagnostics_write(
        "\n--- Kernel owned mapping build ---\n"
    );

    runtime_test_kernel_owned_mapping_build();

    diagnostics_write(
        "\n--- Kernel partial mapping rollback ---\n"
    );

    runtime_test_kernel_partial_mapping_destroy();

    diagnostics_write("\n--- Physical memory ---\n");
    memory_dump_map();
    memory_dump_usable_frames();

    diagnostics_write("\n--- Paging experiment ---\n");
    runtime_test_page_table_chain();

    diagnostics_write("\n--- Kernel address space experiment ---\n");
    runtime_test_kernel_address_space();

    diagnostics_write("\n--- Test active page mapping ---\n");
    runtime_test_active_page_mapping();

    diagnostics_write("\n--- Address space ownership test ---\n");
    runtime_test_kernel_aware_address_space_destroy();

    diagnostics_write("\n--- Process-like address space test ---\n");
    runtime_test_process_like_address_space();

    diagnostics_write("\n--- Process memory test ---\n");
    runtime_test_process_memory_lifecycle();

    diagnostics_write("\n--- Process memory mapping test ---\n");
    runtime_test_process_memory_mapping();

    diagnostics_write("\n--- Owned process page test ---\n");
    runtime_test_process_memory_owned_page();

    diagnostics_write("\n--- Process memory range test ---\n");
    runtime_test_process_memory_range();

    diagnostics_write("\n--- Process stack test ---\n");
    runtime_test_process_stack();

    diagnostics_write("\n--- Minimal process layout test ---\n");
    runtime_test_process_layout();

    diagnostics_write("\n--- GDT and TSS test ---\n");
    runtime_test_gdt();

    diagnostics_write("[tests] Runtime diagnostics completed\n");
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

static uint16_t read_ss(void)
{
    uint16_t ss;

    __asm__ volatile (
        "mov %%ss, %0"
        : "=r"(ss)
    );

    return ss;
}

static uint16_t read_tr(void)
{
    uint16_t tr;

    __asm__ volatile (
        "str %0"
        : "=r"(tr)
    );

    return tr;
}

static void runtime_test_gdt(void)
{
    uint16_t cs = read_cs();
    uint16_t ss = read_ss();
    uint16_t tr = read_tr();

    if (cs != GDT_KERNEL_CODE_SELECTOR) {
        kernel_panic("Unexpected kernel code selector");
    }

    if (ss != GDT_KERNEL_DATA_SELECTOR) {
        kernel_panic("Unexpected kernel stack selector");
    }

    if (tr != GDT_TSS_SELECTOR) {
        kernel_panic("Unexpected task register selector");
    }

    diagnostics_printf(
        "GDT and TSS test:\n"
        "  CS=%x\n"
        "  SS=%x\n"
        "  TR=%x\n"
        "  RSP0=%x\n"
        "  user code selector=%x\n"
        "  user data selector=%x\n",
        (uint64_t) cs,
        (uint64_t) ss,
        (uint64_t) tr,
        gdt_kernel_stack_top(),
        (uint64_t) GDT_USER_CODE_SELECTOR,
        (uint64_t) GDT_USER_DATA_SELECTOR
    );
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

static const char *runtime_page_size_name(
    enum paging_page_size page_size)
{
    switch (page_size) {
        case PAGING_PAGE_SIZE_4K:
            return "4K";

        case PAGING_PAGE_SIZE_2M:
            return "2M";

        case PAGING_PAGE_SIZE_1G:
            return "1G";
    }

    return "unknown";
}

static bool runtime_translation_writable(
    const struct paging_translation *translation)
{
    if (translation == NULL) return false;

    if (
        (translation->pml4_entry &
         PAGE_ENTRY_WRITABLE) == 0 ||
        (translation->pdpt_entry &
         PAGE_ENTRY_WRITABLE) == 0
    ) {
        return false;
    }

    if (
        translation->page_size !=
            PAGING_PAGE_SIZE_1G &&
        (translation->pd_entry &
         PAGE_ENTRY_WRITABLE) == 0
    ) {
        return false;
    }

    if (
        translation->page_size ==
            PAGING_PAGE_SIZE_4K &&
        (translation->pt_entry &
         PAGE_ENTRY_WRITABLE) == 0
    ) {
        return false;
    }

    return true;
}

static bool runtime_translation_user(
    const struct paging_translation *translation)
{
    if (translation == NULL) return false;

    if (
        (translation->pml4_entry &
         PAGE_ENTRY_USER) == 0 ||
        (translation->pdpt_entry &
         PAGE_ENTRY_USER) == 0
    ) {
        return false;
    }

    if (
        translation->page_size !=
            PAGING_PAGE_SIZE_1G &&
        (translation->pd_entry &
         PAGE_ENTRY_USER) == 0
    ) {
        return false;
    }

    if (
        translation->page_size ==
            PAGING_PAGE_SIZE_4K &&
        (translation->pt_entry &
         PAGE_ENTRY_USER) == 0
    ) {
        return false;
    }

    return true;
}

static bool runtime_translation_executable(
    const struct paging_translation *translation)
{
    if (translation == NULL) return false;

    if (
        (translation->pml4_entry &
         PAGE_ENTRY_NO_EXECUTE) != 0 ||
        (translation->pdpt_entry &
         PAGE_ENTRY_NO_EXECUTE) != 0
    ) {
        return false;
    }

    if (
        translation->page_size !=
            PAGING_PAGE_SIZE_1G &&
        (translation->pd_entry &
         PAGE_ENTRY_NO_EXECUTE) != 0
    ) {
        return false;
    }

    if (
        translation->page_size ==
            PAGING_PAGE_SIZE_4K &&
        (translation->pt_entry &
         PAGE_ENTRY_NO_EXECUTE) != 0
    ) {
        return false;
    }

    return true;
}

static void runtime_dump_mapping_point(
    const char *name,
    uint64_t virtual_address)
{
    struct paging_translation translation;

    if (!paging_translate(
        virtual_address,
        &translation
    )) {
        diagnostics_printf(
            "    %s VA=%x unmapped\n",
            name,
            virtual_address
        );

        return;
    }

    diagnostics_printf(
        "    %s VA=%x PA=%x size=%s W=%u U=%u X=%u\n",
        name,
        virtual_address,
        translation.physical_address,
        runtime_page_size_name(
            translation.page_size
        ),
        runtime_translation_writable(
            &translation
        ) ? 1ULL : 0ULL,
        runtime_translation_user(
            &translation
        ) ? 1ULL : 0ULL,
        runtime_translation_executable(
            &translation
        ) ? 1ULL : 0ULL
    );
}

static void runtime_dump_mapping_range(
    const char *name,
    uint64_t start,
    uint64_t end)
{
    if (end <= start) {
        kernel_panic(
            "Kernel mapping inventory received invalid range"
        );
    }

    diagnostics_printf(
        "  %s start=%x end=%x bytes=%u\n",
        name,
        start,
        end,
        end - start
    );

    runtime_dump_mapping_point(
        "first",
        start
    );

    runtime_dump_mapping_point(
        "last",
        end - 1
    );
}

static void runtime_dump_page_table_topology(
    const char *name,
    uint64_t virtual_address)
{
    struct paging_translation translation;

    if (!paging_translate(
        virtual_address,
        &translation
    )) {
        diagnostics_printf(
            "  %s VA=%x unmapped\n",
            name,
            virtual_address
        );

        return;
    }

    uint64_t pml4_physical =
        paging_read_cr3() &
        PAGE_ADDRESS_MASK_4K;

    uint16_t pml4_index =
        paging_pml4_index(
            virtual_address
        );

    uint16_t pdpt_index =
        paging_pdpt_index(
            virtual_address
        );

    uint16_t pd_index =
        paging_pd_index(
            virtual_address
        );

    uint16_t pt_index =
        paging_pt_index(
            virtual_address
        );

    uint64_t pdpt_physical =
        paging_entry_address(
            translation.pml4_entry
        );

    diagnostics_printf(
        "  %s\n"
        "    VA=%x resolved PA=%x size=%s\n"
        "    PML4 PA=%x index=%u entry=%x\n"
        "    PDPT PA=%x index=%u entry=%x\n",
        name,
        virtual_address,
        translation.physical_address,
        runtime_page_size_name(
            translation.page_size
        ),
        pml4_physical,
        (uint64_t) pml4_index,
        translation.pml4_entry,
        pdpt_physical,
        (uint64_t) pdpt_index,
        translation.pdpt_entry
    );

    if (
        translation.page_size ==
        PAGING_PAGE_SIZE_1G
    ) {
        diagnostics_printf(
            "    1G leaf PA=%x\n",
            paging_entry_address(
                translation.pdpt_entry
            )
        );

        return;
    }

    uint64_t pd_physical =
        paging_entry_address(
            translation.pdpt_entry
        );

    diagnostics_printf(
        "    PD PA=%x index=%u entry=%x\n",
        pd_physical,
        (uint64_t) pd_index,
        translation.pd_entry
    );

    if (
        translation.page_size ==
        PAGING_PAGE_SIZE_2M
    ) {
        diagnostics_printf(
            "    2M leaf PA=%x\n",
            translation.pd_entry &
                PAGE_ADDRESS_MASK_2M
        );

        return;
    }

    uint64_t pt_physical =
        paging_entry_address(
            translation.pd_entry
        );

    diagnostics_printf(
        "    PT PA=%x index=%u entry=%x\n"
        "    4K leaf PA=%x\n",
        pt_physical,
        (uint64_t) pt_index,
        translation.pt_entry,
        paging_entry_address(
            translation.pt_entry
        )
    );
}

static void runtime_dump_present_kernel_branches(void)
{
    uint64_t pml4_physical =
        paging_read_cr3() &
        PAGE_ADDRESS_MASK_4K;

    uint64_t *pml4 =
        memory_physical_to_virtual(
            pml4_physical
        );

    diagnostics_write(
        "  Present higher-half PML4 entries:\n"
    );

    for (size_t index = 256; index < 512; ++index) {
        uint64_t entry = pml4[index];

        if ((entry & PAGE_ENTRY_PRESENT) == 0) {
            continue;
        }

        diagnostics_printf(
            "    PML4[%u] entry=%x child PA=%x\n",
            (uint64_t) index,
            entry,
            paging_entry_address(entry)
        );
    }

    uint16_t kernel_pml4_index =
        paging_pml4_index(
            (uint64_t) __kernel_start
        );

    uint64_t kernel_pml4_entry =
        pml4[kernel_pml4_index];

    if (
        (kernel_pml4_entry &
         PAGE_ENTRY_PRESENT) == 0
    ) {
        kernel_panic(
            "Kernel PML4 branch is not present"
        );
    }

    uint64_t kernel_pdpt_physical =
        paging_entry_address(
            kernel_pml4_entry
        );

    uint64_t *kernel_pdpt =
        memory_physical_to_virtual(
            kernel_pdpt_physical
        );

    diagnostics_printf(
        "  Present PDPT entries under PML4[%u]:\n",
        (uint64_t) kernel_pml4_index
    );

    for (size_t index = 0; index < 512; ++index) {
        uint64_t entry =
            kernel_pdpt[index];

        if ((entry & PAGE_ENTRY_PRESENT) == 0) {
            continue;
        }

        diagnostics_printf(
            "    PDPT[%u] entry=%x child/leaf PA=%x huge=%u\n",
            (uint64_t) index,
            entry,
            paging_entry_address(entry),
            (entry & PAGE_ENTRY_HUGE) != 0
                ? 1ULL
                : 0ULL
        );
    }
}

static void runtime_dump_kernel_mapping_inventory(void)
{
    diagnostics_printf(
        "  CR3=%x\n",
        paging_read_cr3() &
            PAGE_ADDRESS_MASK_4K
    );

    uint64_t replaced_kernel_entry;

    if (!kernel_mapping_replaced_branch_entry(
        &replaced_kernel_entry
    )) {
        kernel_panic(
            "Replaced boot kernel branch is unavailable"
        );
    }

    diagnostics_printf(
        "  replaced boot kernel PML4 entry=%x\n",
        replaced_kernel_entry
    );

    runtime_dump_mapping_range(
        ".text",
        (uint64_t) __text_start,
        (uint64_t) __text_end
    );

    runtime_dump_mapping_range(
        ".rodata",
        (uint64_t) __rodata_start,
        (uint64_t) __rodata_end
    );

    runtime_dump_mapping_range(
        ".limine_requests",
        (uint64_t) __limine_requests_start,
        (uint64_t) __limine_requests_end
    );

    runtime_dump_mapping_range(
        ".data",
        (uint64_t) __data_start,
        (uint64_t) __data_end
    );

    runtime_dump_mapping_range(
        ".bss",
        (uint64_t) __bss_start,
        (uint64_t) __bss_end
    );

    uint64_t active_rsp =
        read_rsp();

    runtime_dump_mapping_point(
        "active RSP",
        active_rsp
    );

    uint64_t tss_rsp0 =
        gdt_kernel_stack_top() - 1;

    runtime_dump_mapping_point(
        "TSS RSP0",
        tss_rsp0
    );

    uint64_t kernel_pml4_physical =
        paging_kernel_address_space()
            ->pml4_physical;

    uint64_t direct_map_pml4 =
        (uint64_t)
        memory_physical_to_virtual(
            kernel_pml4_physical
        );

    runtime_dump_mapping_point(
        "direct-map PML4 frame",
        direct_map_pml4
    );

    diagnostics_write(
        "\n  Page-table topology:\n"
    );

    runtime_dump_page_table_topology(
        ".text",
        (uint64_t) __text_start
    );

    runtime_dump_page_table_topology(
        "active RSP",
        active_rsp
    );

    runtime_dump_page_table_topology(
        "TSS RSP0",
        tss_rsp0
    );

    runtime_dump_page_table_topology(
        "direct-map PML4 frame",
        direct_map_pml4
    );

    diagnostics_write(
        "\n  Present higher-half branches:\n"
    );

    runtime_dump_present_kernel_branches();
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

static void runtime_test_kernel_owned_mapping_build(void)
{
    uint64_t free_before =
        physical_free_frame_count();

    struct paging_address_space candidate;

    if (!kernel_mapping_build(
        &candidate
    )) {
        kernel_panic(
            "Unable to build kernel mapping candidate"
        );
    }

    struct paging_translation active_text;
    struct paging_translation candidate_text;

    if (!paging_translate(
        (uint64_t) __text_start,
        &active_text
    )) {
        kernel_panic(
            "Unable to translate active kernel text"
        );
    }

    if (!paging_translate_address_space(
        &candidate,
        (uint64_t) __text_start,
        &candidate_text
    )) {
        kernel_panic(
            "Unable to translate candidate kernel text"
        );
    }

    uint16_t kernel_pml4_index =
        paging_pml4_index(
            (uint64_t) __kernel_start
        );

    uint64_t candidate_entry =
        candidate.pml4_virtual[
            kernel_pml4_index
        ];

    if ((candidate_entry & PAGE_ENTRY_PRESENT) == 0) {
        kernel_panic(
            "Candidate kernel PML4 branch is missing"
        );
    }

    if ((candidate_entry & PAGE_ENTRY_USER) != 0) {
        kernel_panic(
            "Candidate kernel PML4 branch is user accessible"
        );
    }

    if (candidate.pml4_virtual[256] != 0) {
        kernel_panic(
            "Candidate unexpectedly inherited direct-map branch"
        );
    }

    diagnostics_printf(
        "Kernel owned mapping candidate:\n"
        "  active PML4 PA=%x\n"
        "  candidate PML4 PA=%x\n"
        "  kernel PML4 index=%u\n"
        "  candidate entry=%x\n"
        "  candidate PDPT PA=%x\n"
        "  active text PA=%x\n"
        "  candidate text PA=%x\n"
        "  all kernel pages validated\n"
        "  free before=%u\n"
        "  free after build=%u\n",
        paging_read_cr3() &
            PAGE_ADDRESS_MASK_4K,
        candidate.pml4_physical,
        (uint64_t) kernel_pml4_index,
        candidate_entry,
        paging_entry_address(
            candidate_entry
        ),
        active_text.physical_address,
        candidate_text.physical_address,
        free_before,
        physical_free_frame_count()
    );

    if (!kernel_mapping_destroy(
        &candidate
    )) {
        kernel_panic(
            "Unable to destroy kernel mapping candidate"
        );
    }

    uint64_t free_after_destroy =
        physical_free_frame_count();

    if (free_after_destroy != free_before) {
        kernel_panic(
            "Kernel mapping candidate leaked page-table frames"
        );
    }

    diagnostics_printf(
        "  free after destroy=%u\n",
        free_after_destroy
    );
}

static void runtime_test_kernel_partial_mapping_destroy(void)
{
    uint64_t free_before =
        physical_free_frame_count();

    struct paging_address_space candidate;

    if (!paging_address_space_create(
        &candidate
    )) {
        kernel_panic(
            "Unable to create partial kernel mapping candidate"
        );
    }

    uint64_t pdpt_physical;
    uint64_t *pdpt;

    if (!paging_create_empty_table(
        &pdpt_physical,
        &pdpt
    )) {
        kernel_panic(
            "Unable to create partial kernel PDPT"
        );
    }

    uint16_t pml4_index =
        paging_pml4_index(
            (uint64_t) __kernel_start
        );

    candidate.pml4_virtual[
        pml4_index
    ] = paging_make_table_entry(
        pdpt_physical,
        true,
        false
    );

    uint64_t pd_physical;
    uint64_t *pd;

    if (!paging_create_empty_table(
        &pd_physical,
        &pd
    )) {
        kernel_panic(
            "Unable to create partial kernel PD"
        );
    }

    uint16_t pdpt_index =
        paging_pdpt_index(
            (uint64_t) __kernel_start
        );

    pdpt[pdpt_index] =
        paging_make_table_entry(
            pd_physical,
            true,
            false
        );

    uint64_t pt_physical;
    uint64_t *pt;

    if (!paging_create_empty_table(
        &pt_physical,
        &pt
    )) {
        kernel_panic(
            "Unable to create partial kernel PT"
        );
    }

    uint16_t pd_index =
        paging_pd_index(
            (uint64_t) __kernel_start
        );

    pd[pd_index] =
        paging_make_table_entry(
            pt_physical,
            true,
            false
        );

    uint64_t free_after_partial_build =
        physical_free_frame_count();

    if (
        free_after_partial_build !=
        free_before - 4
    ) {
        kernel_panic(
            "Partial kernel mapping allocated unexpected frame count"
        );
    }

    if (!kernel_mapping_destroy(
        &candidate
    )) {
        kernel_panic(
            "Unable to destroy partial kernel mapping candidate"
        );
    }

    uint64_t free_after_destroy =
        physical_free_frame_count();

    if (free_after_destroy != free_before) {
        kernel_panic(
            "Partial kernel mapping rollback leaked page-table frames"
        );
    }

    diagnostics_printf(
        "Partial kernel mapping rollback test:\n"
        "  free before=%u\n"
        "  free after partial build=%u\n"
        "  free after destroy=%u\n",
        free_before,
        free_after_partial_build,
        free_after_destroy
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
        false,
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
    uint64_t free_before = physical_free_frame_count();
    struct paging_address_space address_space;

    if (
        !paging_address_space_create_with_kernel(
            &address_space
        )
    ) {
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
    uint64_t direct_map_virtual_address = (uint64_t) memory_physical_to_virtual(
        address_space.pml4_physical
    );

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

    if (
        !paging_address_space_destroy(
            &address_space
        )
    ) {
        kernel_panic("Unable to destroy kernel-aware test address space");
    }

    uint64_t free_after = physical_free_frame_count();

    if (free_after != free_before) {
        kernel_panic("Kernel address space test leaked physical frames");
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
        "  direct map PA=%x\n"
        "  free before=%u\n"
        "  free after=%u\n",
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
        inherited_direct_map_translation.physical_address,
        free_before,
        free_after
    );
}

static void runtime_test_active_page_mapping(void)
{
    struct paging_address_space *kernel_space =
        paging_kernel_address_space();

    if (kernel_space == NULL) {
        kernel_panic("Kernel address space unavailable");
    }

    uint64_t physical_address;

    if (!physical_alloc_frame(&physical_address)) {
        kernel_panic("Unable to allocate active mapping test frame");
    }

    uint64_t virtual_address = 0x0000000040000000ULL;

    struct paging_translation translation;

    if (paging_translate(virtual_address, &translation)) {
        kernel_panic("Active mapping test VA is already mapped");
    }

    if (!paging_map_page(
        (struct paging_address_space *) kernel_space,
        virtual_address,
        physical_address,
        true,
        false,
        false))
    {
        kernel_panic("Unable to map page into active address space");
    }

    paging_invalidate_page(virtual_address);

    volatile uint64_t *mapped_value = (volatile uint64_t *) virtual_address;

    *mapped_value = 0xAABBCCDDEEFF0011ULL;

    if (*mapped_value != 0xAABBCCDDEEFF0011ULL) {
        kernel_panic("Active virtual mapping returned wrong data");
    }

    uint64_t *direct_map_value = memory_physical_to_virtual(physical_address);

    if (direct_map_value[0] != 0xAABBCCDDEEFF0011ULL) {
        kernel_panic("Active mapping does not reference expected physical frame");
    }

    uint64_t unmapped_physical;
    uint64_t observed_value = *mapped_value;

    if (!paging_unmap_page(
        kernel_space,
        virtual_address,
        &unmapped_physical
    )) {
        kernel_panic("Unable to unmap active test page");
    }

    paging_invalidate_page(virtual_address);

    if (unmapped_physical != physical_address) {
        kernel_panic("Active unmap returned wrong physical frame");
    }

    if (!physical_free_frame(unmapped_physical)) {
        kernel_panic("Unable to free active mapping test frame");
    }

    diagnostics_printf(
        "Active page mapping test:\n"
        "  VA=%x\n"
        "  PA=%x\n"
        "  value=%x\n"
        "  unmapped PA=%x\n"
        "  mapping lifecycle complete\n",
        virtual_address,
        physical_address,
        observed_value,
        unmapped_physical
    );
}

static void runtime_test_kernel_aware_address_space_destroy(void)
{
    uint64_t free_before = physical_free_frame_count();

    struct paging_address_space address_space;

    if (!paging_address_space_create_with_kernel(&address_space)) {
        kernel_panic("Unable to create kernel-aware destroy test address space");
    }

    uint64_t free_after_create = physical_free_frame_count();

    if (free_after_create != free_before - 1) {
        kernel_panic("Kernel-aware address space allocated unexpected frame count");
    }

    uint64_t kernel_virtual_address = (uint64_t) __kernel_start;
    uint16_t kernel_pml4_index = paging_pml4_index(kernel_virtual_address);

    if ((address_space.pml4_virtual[kernel_pml4_index] & PAGE_ENTRY_PRESENT) == 0) {
        kernel_panic("Kernel mapping missing before address space destroy");
    }

    if (!paging_address_space_destroy(&address_space)) {
        kernel_panic("Unable to destroy kernel-aware address space");
    }

    uint64_t free_after_destroy = physical_free_frame_count();

    if (free_after_destroy != free_before) {
        kernel_panic("Kernel-aware address space root was not reclaimed");
    }

    diagnostics_printf(
        "Kernel-aware address space destroy test:\n"
        "  kernel PML4 index=%u\n"
        "  free before=%u\n"
        "  free after create=%u\n"
        "  free after destroy=%u\n",
        (uint64_t) kernel_pml4_index,
        free_before,
        free_after_create,
        free_after_destroy
    );
}

static void runtime_test_process_like_address_space(void) {
    uint64_t free_before = physical_free_frame_count();

    struct paging_address_space address_space;

    if (!paging_address_space_create_with_kernel(&address_space)) {
        kernel_panic("Unable to create process-like address space");
    }

    uint64_t data_physical;

    if (!physical_alloc_frame(&data_physical)) {
        kernel_panic("Unable to allocate process-like data frame");
    }

    uint64_t test_virtual_address = 0x0000000040203000ULL;

    if (!paging_map_page(
        &address_space,
        test_virtual_address,
        data_physical,
        true,
        true,
        false))
    {
        kernel_panic("Unable to map process-like user page");
    }

    uint64_t free_after_mapping = physical_free_frame_count();

    if (free_after_mapping != free_before - 5) {
        kernel_panic("Process-like mapping allocated unexpected frame count");
    }

    struct paging_translation translation;

    if (!paging_translate_address_space(
        &address_space,
        test_virtual_address,
        &translation
    )) {
        kernel_panic("Unable to translate process-like user page");
    }

    if (translation.physical_address != data_physical) {
        kernel_panic("Process-like mapping resolved wrong physical frame");
    }

    if ((translation.pml4_entry & PAGE_ENTRY_USER) == 0) {
        kernel_panic("Process-like PML4 entry is not user accessible");
    }

    if ((translation.pdpt_entry & PAGE_ENTRY_USER) == 0) {
        kernel_panic("Process-like PDPT entry is not user accessible");
    }

    if ((translation.pd_entry & PAGE_ENTRY_USER) == 0) {
        kernel_panic("Process-like PD entry is not user accessible");
    }

    if ((translation.pt_entry & PAGE_ENTRY_USER) == 0) {
        kernel_panic("Process-like PT entry is not user accessible");
    }

    if ((translation.pt_entry & PAGE_ENTRY_WRITABLE) == 0) {
        kernel_panic("Process-like user page is not writable");
    }

    uint64_t unmapped_physical;

    if (!paging_unmap_page(
        &address_space,
        test_virtual_address,
        &unmapped_physical
    )) {
        kernel_panic("Unable to unmap process-like user page");
    }

    if (unmapped_physical != data_physical) {
        kernel_panic("Process-like unmap returned wrong physical frame");
    }

    uint64_t free_after_unmap = physical_free_frame_count();

    if (free_after_unmap != free_before - 2) {
        kernel_panic("Process-like page tables were not reclaimed");
    }

    if (!physical_free_frame(unmapped_physical)) {
        kernel_panic("Unable to release process-like data frame");
    }

    uint64_t free_after_data_free = physical_free_frame_count();

    if (free_after_data_free != free_before - 1) {
        kernel_panic("Process-like data frame was not reclaimed");
    }

    if (!paging_address_space_destroy(&address_space)) {
        kernel_panic("Unable to destroy process-like address space");
    }

    uint64_t free_after_destroy = physical_free_frame_count();

    if (free_after_destroy != free_before) {
        kernel_panic("Process-like address space leaked physical frames");
    }

    diagnostics_printf(
        "Process-like address space lifecycle test:\n"
        "  user VA=%x\n"
        "  data PA=%x\n"
        "  resolved PA=%x\n"
        "  PML4E=%x\n"
        "  PDPTE=%x\n"
        "  PDE=%x\n"
        "  PTE=%x\n"
        "  free before=%u\n"
        "  free after mapping=%u\n"
        "  free after unmap=%u\n"
        "  free after data free=%u\n"
        "  free after destroy=%u\n",
        test_virtual_address,
        data_physical,
        translation.physical_address,
        translation.pml4_entry,
        translation.pdpt_entry,
        translation.pd_entry,
        translation.pt_entry,
        free_before,
        free_after_mapping,
        free_after_unmap,
        free_after_data_free,
        free_after_destroy
    );
}

static void runtime_test_process_memory_lifecycle(void)
{
    uint64_t free_before = physical_free_frame_count();

    struct process_memory memory;

    if (!process_memory_create(&memory)) {
        kernel_panic("Unable to create process memory");
    }

    uint64_t free_after_create = physical_free_frame_count();

    if (free_after_create != free_before - 1) {
        kernel_panic("Process memory allocated unexpected frame count");
    }

    if (!process_memory_destroy(&memory)) {
        kernel_panic("Unable to destroy process memory");
    }

    uint64_t free_after_destroy = physical_free_frame_count();

    if (free_after_destroy != free_before) {
        kernel_panic("Process memory leaked physical frames");
    }

    diagnostics_printf(
        "Process memory lifecycle test:\n"
        "  free before=%u\n"
        "  free after create=%u\n"
        "  free after destroy=%u\n",
        free_before,
        free_after_create,
        free_after_destroy
    );
}

static void runtime_test_process_memory_mapping(void)
{
    uint64_t free_before = physical_free_frame_count();

    struct process_memory memory;

    if (!process_memory_create(&memory)) {
        kernel_panic("Unable to create process memory mapping test");
    }

    uint64_t data_physical;

    if (!physical_alloc_frame(&data_physical)) {
        kernel_panic("Unable to allocate process memory data frame");
    }

    uint64_t test_virtual_address = 0x0000000040203000ULL;

    if (!process_memory_map_page(
        &memory,
        test_virtual_address,
        data_physical,
        true
    )) {
        kernel_panic("Unable to map process memory page");
    }

    uint64_t free_after_mapping = physical_free_frame_count();

    if (free_after_mapping != free_before - 5) {
        kernel_panic("Process memory mapping allocated unexpected frame count");
    }

    struct paging_translation translation;

    if (!paging_translate_address_space(
        &memory.address_space,
        test_virtual_address,
        &translation
    )) {
        kernel_panic("Unable to translate process memory page");
    }

    if (translation.physical_address != data_physical) {
        kernel_panic("Process memory mapping resolved wrong physical frame");
    }

    if ((translation.pml4_entry & PAGE_ENTRY_USER) == 0) {
        kernel_panic("Process memory PML4 entry is not user accessible");
    }

    if ((translation.pdpt_entry & PAGE_ENTRY_USER) == 0) {
        kernel_panic("Process memory PDPT entry is not user accessible");
    }

    if ((translation.pd_entry & PAGE_ENTRY_USER) == 0) {
        kernel_panic("Process memory PD entry is not user accessible");
    }

    if ((translation.pt_entry & PAGE_ENTRY_USER) == 0) {
        kernel_panic("Process memory PT entry is not user accessible");
    }

    if ((translation.pt_entry & PAGE_ENTRY_WRITABLE) == 0) {
        kernel_panic("Process memory page is not writable");
    }

    uint64_t unmapped_physical;

    if (!process_memory_unmap_page(
        &memory,
        test_virtual_address,
        &unmapped_physical
    )) {
        kernel_panic("Unable to unmap process memory page");
    }

    if (unmapped_physical != data_physical) {
        kernel_panic("Process memory unmap returned wrong physical frame");
    }

    uint64_t free_after_unmap = physical_free_frame_count();

    if (free_after_unmap != free_before - 2) {
        kernel_panic("Process memory page tables were not reclaimed");
    }

    if (!physical_free_frame(unmapped_physical)) {
        kernel_panic("Unable to free process memory data frame");
    }

    uint64_t free_after_data_free = physical_free_frame_count();

    if (free_after_data_free != free_before - 1) {
        kernel_panic("Process memory data frame was not reclaimed");
    }

    if (!process_memory_destroy(&memory)) {
        kernel_panic("Unable to destroy process memory after mapping test");
    }

    uint64_t free_after_destroy = physical_free_frame_count();

    if (free_after_destroy != free_before) {
        kernel_panic("Process memory mapping test leaked physical frames");
    }

    diagnostics_printf(
        "Process memory mapping test:\n"
        "  user VA=%x\n"
        "  data PA=%x\n"
        "  resolved PA=%x\n"
        "  PML4E=%x\n"
        "  PDPTE=%x\n"
        "  PDE=%x\n"
        "  PTE=%x\n"
        "  free before=%u\n"
        "  free after mapping=%u\n"
        "  free after unmap=%u\n"
        "  free after data free=%u\n"
        "  free after destroy=%u\n",
        test_virtual_address,
        data_physical,
        translation.physical_address,
        translation.pml4_entry,
        translation.pdpt_entry,
        translation.pd_entry,
        translation.pt_entry,
        free_before,
        free_after_mapping,
        free_after_unmap,
        free_after_data_free,
        free_after_destroy
    );
}

static void runtime_test_process_memory_owned_page(void)
{
    uint64_t free_before = physical_free_frame_count();

    struct process_memory memory;

    if (!process_memory_create(&memory)) {
        kernel_panic("Unable to create owned process memory test");
    }

    uint64_t test_virtual_address = 0x0000000040203000ULL;

    if (!process_memory_allocate_page(
        &memory,
        test_virtual_address,
        true
    )) {
        kernel_panic("Unable to allocate owned process page");
    }

    uint64_t free_after_allocate = physical_free_frame_count();

    if (free_after_allocate != free_before - 5) {
        kernel_panic("Owned process page allocated unexpected frame count");
    }

    struct paging_translation translation;

    if (!paging_translate_address_space(
        &memory.address_space,
        test_virtual_address,
        &translation
    )) {
        kernel_panic("Unable to translate owned process page");
    }

    if ((translation.pt_entry & PAGE_ENTRY_USER) == 0) {
        kernel_panic("Owned process page is not user accessible");
    }

    if ((translation.pt_entry & PAGE_ENTRY_WRITABLE) == 0) {
        kernel_panic("Owned process page is not writable");
    }

    uint64_t physical_address = translation.physical_address;

    if (!process_memory_release_page(
        &memory,
        test_virtual_address
    )) {
        kernel_panic("Unable to release owned process page");
    }

    uint64_t free_after_release = physical_free_frame_count();

    if (free_after_release != free_before - 1) {
        kernel_panic("Owned process page was not fully reclaimed");
    }

    if (!process_memory_destroy(&memory)) {
        kernel_panic("Unable to destroy owned process memory");
    }

    uint64_t free_after_destroy = physical_free_frame_count();

    if (free_after_destroy != free_before) {
        kernel_panic("Owned process memory leaked physical frames");
    }

    diagnostics_printf(
        "Owned process page lifecycle test:\n"
        "  user VA=%x\n"
        "  data PA=%x\n"
        "  free before=%u\n"
        "  free after allocate=%u\n"
        "  free after release=%u\n"
        "  free after destroy=%u\n",
        test_virtual_address,
        physical_address,
        free_before,
        free_after_allocate,
        free_after_release,
        free_after_destroy
    );
}

static void runtime_test_process_memory_range(void)
{
    uint64_t free_before = physical_free_frame_count();

    struct process_memory memory;

    if (!process_memory_create(&memory)) {
        kernel_panic("Unable to create process memory range test");
    }

    uint64_t start_virtual_address = 0x0000000040200000ULL;
    size_t page_count = 4;

    if (!process_memory_allocate_pages(
        &memory,
        start_virtual_address,
        page_count,
        true
    )) {
        kernel_panic("Unable to allocate process memory range");
    }

    uint64_t free_after_allocate = physical_free_frame_count();

    if (free_after_allocate != free_before - 8) {
        kernel_panic("Process memory range allocated unexpected frame count");
    }

    for (size_t index = 0; index < page_count; ++index) {
        uint64_t virtual_address =
            start_virtual_address + ((uint64_t) index * 4096ULL);

        struct paging_translation translation;

        if (!paging_translate_address_space(
            &memory.address_space,
            virtual_address,
            &translation
        )) {
            kernel_panic("Unable to translate process memory range page");
        }

        if ((translation.pt_entry & PAGE_ENTRY_USER) == 0) {
            kernel_panic("Process memory range page is not user accessible");
        }

        if ((translation.pt_entry & PAGE_ENTRY_WRITABLE) == 0) {
            kernel_panic("Process memory range page is not writable");
        }

        if ((translation.pt_entry & PAGE_ENTRY_NO_EXECUTE) == 0) {
            kernel_panic("Owned process page is executable");
        }
    }

    if (!process_memory_release_pages(
        &memory,
        start_virtual_address,
        page_count)) {
        kernel_panic("Unable to release process memory range");
    }

    uint64_t free_after_release = physical_free_frame_count();

    if (free_after_release != free_before - 1) {
        kernel_panic("Process memory range was not fully reclaimed");
    }

    if (!process_memory_destroy(&memory)) {
        kernel_panic("Unable to destroy process memory range test");
    }

    uint64_t free_after_destroy = physical_free_frame_count();

    if (free_after_destroy != free_before) {
        kernel_panic("Process memory range test leaked physical frames");
    }

    diagnostics_printf(
        "Process memory range lifecycle test:\n"
        "  start VA=%x\n"
        "  pages=%u\n"
        "  bytes=%u\n"
        "  free before=%u\n"
        "  free after allocate=%u\n"
        "  free after release=%u\n"
        "  free after destroy=%u\n",
        start_virtual_address,
        (uint64_t) page_count,
        (uint64_t) page_count * 4096ULL,
        free_before,
        free_after_allocate,
        free_after_release,
        free_after_destroy
    );
}

static void runtime_test_process_stack(void)
{
    uint64_t free_before = physical_free_frame_count();

    struct process_memory memory;

    if (!process_memory_create(&memory)) {
        kernel_panic("Unable to create process stack test memory");
    }

    struct process_stack stack;

    uint64_t stack_top = 0x0000000070000000ULL;
    size_t stack_pages = 4;

    if (!process_stack_create(
        &memory,
        &stack,
        stack_top,
        stack_pages
    )) {
        kernel_panic("Unable to create process stack");
    }

    uint64_t free_after_create = physical_free_frame_count();

    if (free_after_create != free_before - 8) {
        kernel_panic("Process stack allocated unexpected frame count");
    }

    for (size_t index = 0; index < stack.page_count; ++index) {
        uint64_t virtual_address =
            stack.base_address + ((uint64_t) index * 4096ULL);

        struct paging_translation translation;

        if (!paging_translate_address_space(
            &memory.address_space,
            virtual_address,
            &translation
        )) {
            kernel_panic("Process stack page is not mapped");
        }

        if ((translation.pt_entry & PAGE_ENTRY_USER) == 0) {
            kernel_panic("Process stack page is not user accessible");
        }

        if ((translation.pt_entry & PAGE_ENTRY_WRITABLE) == 0) {
            kernel_panic("Process stack page is not writable");
        }

        if ((translation.pt_entry & PAGE_ENTRY_NO_EXECUTE) == 0) {
            kernel_panic("Process stack page is executable");
        }
    }

    struct paging_translation guard_translation;

    if (paging_translate_address_space(
        &memory.address_space,
        stack.guard_address,
        &guard_translation
    )) {
        kernel_panic("Process stack guard page is mapped");
    }

    uint64_t guard_address = stack.guard_address;
    uint64_t base_address = stack.base_address;
    uint64_t top_address = stack.stack_top;

    if (!process_stack_destroy(&memory, &stack)) {
        kernel_panic("Unable to destroy process stack");
    }

    uint64_t free_after_stack_destroy = physical_free_frame_count();

    if (free_after_stack_destroy != free_before - 1) {
        kernel_panic("Process stack pages were not fully reclaimed");
    }

    if (!process_memory_destroy(&memory)) {
        kernel_panic("Unable to destroy process stack test memory");
    }

    uint64_t free_after_destroy = physical_free_frame_count();

    if (free_after_destroy != free_before) {
        kernel_panic("Process stack test leaked physical frames");
    }

    diagnostics_printf(
        "Process stack lifecycle test:\n"
        "  guard VA=%x\n"
        "  base VA=%x\n"
        "  top VA=%x\n"
        "  pages=%u\n"
        "  bytes=%u\n"
        "  free before=%u\n"
        "  free after create=%u\n"
        "  free after stack destroy=%u\n"
        "  free after destroy=%u\n",
        guard_address,
        base_address,
        top_address,
        (uint64_t) stack_pages,
        (uint64_t) stack_pages * 4096ULL,
        free_before,
        free_after_create,
        free_after_stack_destroy,
        free_after_destroy
    );
}

static void runtime_test_process_layout(void) {
    uint64_t free_before = physical_free_frame_count();

    struct process_memory memory;

    if (!process_memory_create(&memory)) {
        kernel_panic("Unable to create process layout test memory");
    }

    struct process_layout layout;

    if (!process_layout_create(
        &memory,
        &layout
    )) {
        kernel_panic("Unable to create process layout");
    }

    static const uint8_t user_program[] = {
        0xB8, 0x78, 0x56, 0x34, 0x12,
        0xEB, 0xFE,
        0x90
    };

    if (!process_memory_write(
            &memory,
            layout.code_base,
            user_program,
            sizeof(user_program))
    ) {
        kernel_panic("Unable to load process user code");
    }

    uint64_t free_after_create = physical_free_frame_count();

    struct paging_translation code_translation;

    if (!paging_translate_address_space(
        &memory.address_space,
        layout.code_base,
        &code_translation
    )) {
        kernel_panic("Process code page is not mapped");
    }

    if ((code_translation.pt_entry & PAGE_ENTRY_USER) == 0) {
        kernel_panic("Process code page is not user accessible");
    }

    if ((code_translation.pt_entry & PAGE_ENTRY_WRITABLE) != 0) {
        kernel_panic("Process code page is writable");
    }

    if ((code_translation.pt_entry & PAGE_ENTRY_NO_EXECUTE) != 0) {
        kernel_panic("Process code page is not executable");
    }

    const uint8_t *loaded_code =
    memory_physical_to_virtual(code_translation.physical_address);

    for (size_t index = 0; index < sizeof(user_program); ++index) {
        if (loaded_code[index] != user_program[index]) {
            kernel_panic("Process user code was loaded incorrectly");
        }
    }

    if ((code_translation.pt_entry & PAGE_ENTRY_WRITABLE) != 0) {
        kernel_panic("Loaded process code became writable");
    }

    if ((code_translation.pt_entry & PAGE_ENTRY_NO_EXECUTE) != 0) {
        kernel_panic("Loaded process code became non-executable");
    }

    uint64_t code_signature = 0;

    for (size_t index = 0; index < sizeof(user_program); ++index) {
        code_signature |=
            ((uint64_t) loaded_code[index]) <<
            (index * 8);
    }

    struct paging_translation stack_translation;

    if (!paging_translate_address_space(
        &memory.address_space,
        layout.stack.base_address,
        &stack_translation
    )) {
        kernel_panic("Process stack is not mapped");
    }

    if ((stack_translation.pt_entry & PAGE_ENTRY_USER) == 0) {
        kernel_panic("Process stack is not user accessible");
    }

    if ((stack_translation.pt_entry & PAGE_ENTRY_WRITABLE) == 0) {
        kernel_panic("Process stack is not writable");
    }

    if ((stack_translation.pt_entry & PAGE_ENTRY_NO_EXECUTE) == 0) {
        kernel_panic("Process stack is executable");
    }

    struct paging_translation guard_translation;

    if (paging_translate_address_space(
        &memory.address_space,
        layout.stack.guard_address,
        &guard_translation
    )) {
        kernel_panic("Process stack guard page is mapped");
    }

    uint64_t code_base = layout.code_base;
    uint64_t entry_point = layout.entry_point;
    uint64_t stack_base = layout.stack.base_address;
    uint64_t stack_top = layout.stack.stack_top;
    uint64_t guard_address = layout.stack.guard_address;

    if (!process_layout_destroy(&memory,&layout))
    {
        kernel_panic("Unable to destroy process layout");
    }

    uint64_t free_after_layout_destroy = physical_free_frame_count();

    if (free_after_layout_destroy != free_before - 1) {
        kernel_panic("Process layout mappings were not fully reclaimed");
    }

    if (!process_memory_destroy(&memory)) {
        kernel_panic("Unable to destroy process layout test memory");
    }

    uint64_t free_after_destroy = physical_free_frame_count();

    if (free_after_destroy != free_before) {
        kernel_panic("Process layout test leaked physical frames");
    }

    diagnostics_printf(
        "Minimal process layout test:\n"
        "  code VA=%x\n"
        "  entry point=%x\n"
        "  code PA=%x\n"
        "  code PTE=%x\n"
        "  code signature=%x\n"
        "  stack base=%x\n"
        "  stack top=%x\n"
        "  stack PTE=%x\n"
        "  guard VA=%x\n"
        "  free before=%u\n"
        "  free after create=%u\n"
        "  free after layout destroy=%u\n"
        "  free after destroy=%u\n",
        code_base,
        entry_point,
        code_translation.physical_address,
        code_translation.pt_entry,
        code_signature,
        stack_base,
        stack_top,
        stack_translation.pt_entry,
        guard_address,
        free_before,
        free_after_create,
        free_after_layout_destroy,
        free_after_destroy
    );
}
