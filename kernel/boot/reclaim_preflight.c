// SPDX-License-Identifier: GPL-2.0-only

#include "reclaim_preflight.h"

#include "../arch/x86_64/arch.h"
#include "../arch/x86_64/paging.h"
#include "../memory/boot_paging.h"
#include "../memory/direct_mapping.h"
#include "../memory/kernel_mapping.h"
#include "../memory/memory.h"

#include <stdint.h>

extern char __bss_start[];
extern char __bss_end[];

bool boot_reclaim_preflight(
    const struct boot_info *boot_info,
    struct boot_reclaim_preflight_report *report)
{
    if (
        boot_info == NULL ||
        report == NULL
    ) {
        return false;
    }

    /*
     * The CPU must no longer depend on bootloader-installed descriptor
     * tables when general bootloader-memory reclamation begins.
     */
    if (!arch_boot_reclaim_ready()) {
        return false;
    }

    /*
     * All Limine response structures must have been consumed before
     * the general boot-memory preflight can proceed.
     *
     * This checks persistent protocol references. The transient pointers
     * in boot_info are checked separately below.
     */
    if (!boot_protocol_snapshot_complete()) {
        return false;
    }

    /*
     * The kernel must not retain these transient bootloader-provided
     * pointers in its persistent boot information.
     */
    if (
        boot_info->command_line != NULL ||
        boot_info->smbios_entry_32 != NULL ||
        boot_info->smbios_entry_64 != NULL
    ) {
        return false;
    }

    /*
     * The memory map has already been copied into kernel-owned storage
     * by memory_init(). Check that the boot snapshot remains consistent
     * with the direct-map configuration used by the allocator.
     */
    if (
        boot_info->memory_region_count == 0 ||
        boot_info->memory_region_count >
            BOOT_MEMORY_REGION_MAX ||
        boot_info->direct_map_offset !=
            memory_direct_map_base()
    ) {
        return false;
    }

    /*
     * Never inspect the historical paging hierarchy here.
     * Its physical frames may already have been reused.
     */
    if (!boot_paging_reclamation_complete()) {
        return false;
    }

    struct paging_address_space *active =
        paging_kernel_address_space();

    if (active == NULL) {
        return false;
    }

    uint64_t active_cr3 =
        paging_read_cr3() &
        PAGE_ADDRESS_MASK_4K;

    if (active_cr3 != active->pml4_physical) {
        return false;
    }

    uint64_t inherited_kernel_entry;
    uint64_t inherited_direct_map_entry;

    if (
        !kernel_mapping_replaced_branch_entry(
            &inherited_kernel_entry
        ) ||
        !direct_mapping_replaced_branch_entry(
            &inherited_direct_map_entry
        )
    ) {
        return false;
    }

    if (
        (inherited_kernel_entry &
         PAGE_ENTRY_PRESENT) == 0 ||
        (inherited_direct_map_entry &
         PAGE_ENTRY_PRESENT) == 0
    ) {
        return false;
    }

    /*
     * This preflight is intended for the early bootstrap cutover.
     * Its own execution stack must already be the kernel runtime
     * stack in the kernel's .bss section, not a bootloader stack.
     */
    uint64_t stack_pointer;

    __asm__ volatile (
        "mov %%rsp, %0"
        : "=r" (stack_pointer)
    );

    if (
        stack_pointer <
            (uint64_t) __bss_start ||
        stack_pointer >=
            (uint64_t) __bss_end
    ) {
        return false;
    }

    /*
     * The framebuffer must have an active mapping. Its first mapped
     * physical frame must not be pending bootloader reclamation.
     */
    if (boot_info->framebuffer.address == NULL) {
        return false;
    }

    struct paging_translation framebuffer_translation;

    if (
        !paging_translate(
            (uint64_t) boot_info->framebuffer.address,
            &framebuffer_translation
        )
    ) {
        return false;
    }

    uint64_t framebuffer_frame =
        framebuffer_translation.physical_address &
        PAGE_ADDRESS_MASK_4K;

    if (
        memory_bootloader_frame_reclaim_pending(
            framebuffer_frame
        )
    ) {
        return false;
    }

    uint64_t free_before =
        physical_free_frame_count();

    struct memory_bootloader_frame_inventory inventory;

    if (
        !memory_bootloader_frame_inventory_collect(
            &inventory
        )
    ) {
        return false;
    }

    /*
     * Check the partition without allowing an overflowing sum to
     * make inconsistent counters appear valid.
     */
    if (
        inventory.pending_frames >
        inventory.eligible_frames
    ) {
        return false;
    }

    uint64_t transferred_frames =
        inventory.eligible_frames -
        inventory.pending_frames;

    if (
        inventory.transferred_free_frames >
        transferred_frames ||
        inventory.transferred_used_frames !=
            transferred_frames -
            inventory.transferred_free_frames
    ) {
        return false;
    }

    uint64_t free_after =
        physical_free_frame_count();

    if (free_after != free_before) {
        return false;
    }

    report->inventory = inventory;
    report->free_frames = free_after;

    return true;
}
