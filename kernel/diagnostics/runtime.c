// SPDX-License-Identifier: GPL-2.0-only

#include "runtime.h"
#include "diagnostics.h"

#include "../arch/x86_64/paging.h"
#include "../core/panic.h"

#include <stdint.h>

extern char __kernel_start[];
extern char __kernel_end[];

static uint16_t read_cs(void)
{
    uint16_t cs;

    __asm__ volatile (
        "mov %%cs, %0"
        : "=r"(cs)
    );

    return cs;
}

static void dump_page_size(
    enum paging_page_size page_size)
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

void runtime_diagnostics_dump(void)
{
    uintptr_t kernel_start =
        (uintptr_t) __kernel_start;

    uintptr_t kernel_end =
        (uintptr_t) __kernel_end;

    uint64_t kernel_size =
        (uint64_t) (
            kernel_end - kernel_start
        );

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

    struct paging_translation translation;

    uint64_t virtual_address =
        (uint64_t) kernel_start;

    if (!paging_translate(
            virtual_address,
            &translation)) {
        kernel_panic(
            "Unable to translate kernel address"
        );
            }

    dump_page_size(
        translation.page_size
    );

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
