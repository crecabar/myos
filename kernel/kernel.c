// SPDX-License-Identifier: GPL-2.0-only

#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/arch.h"
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/paging.h"
#include "arch/x86_64/rtc.h"
#include "arch/x86_64/serial.h"
#include "arch/x86_64/stack.h"
#include "boot/active_paging_audit.h"
#include "boot/boot.h"
#include "boot/memory_reclaim.h"
#include "boot/physical_range_audit.h"
#include "boot/reclaim_preflight.h"
#include "config/boot_config.h"
#include "core/panic.h"
#include "diagnostics/diagnostics.h"
#include "init/boot_banner.h"
#include "init/display.h"
#include "input/input.h"
#include "memory/boot_paging.h"
#include "memory/device_mapping.h"
#include "memory/direct_mapping.h"
#include "memory/heap.h"
#include "memory/kernel_mapping.h"
#include "memory/memory.h"
#include "scheduler/scheduler.h"

#if MYOS_RUNTIME_DIAGNOSTICS
#include "tests/runtime_diagnostics.h"
#endif

#if MYOS_KERNEL_TESTS
#include "arch/x86_64/tests/interrupt_wait_test.h"
#include "tests/acpi_test.h"
#include "tests/boot_config_test.h"
#include "tests/elf64_test.h"
#include "tests/elf64_loader_test.h"
#include "tests/framebuffer_test.h"
#include "tests/input_test.h"
#include "tests/kernel_heap_test.h"
#include "tests/process_elf_lifecycle_test.h"
#include "tests/process_lifecycle_test.h"
#include "tests/process_memory_test.h"
#include "tests/scheduler_slot_test.h"
#include "tests/user_processes.h"
#endif

#define KERNEL_RUNTIME_STACK_SIZE 32768

static struct boot_info kernel_boot_info;
static struct kernel_boot_config kernel_boot_config;
static struct kernel_display kernel_display;

static uint8_t kernel_runtime_stack[
    KERNEL_RUNTIME_STACK_SIZE
] __attribute__((aligned(16)));

static void kernel_input_system_action(
    enum input_system_action action
);

static _Noreturn void kernel_main_continue(void);

_Noreturn void kernel_main(void)
{
    serial_init();
    diagnostics_init();

    diagnostics_write("\x1b[2J\x1b[H");

    boot_init(
        &kernel_boot_info
    );

    boot_config_parse(
        kernel_boot_info.command_line,
        &kernel_boot_config
    );

    /*
     * The command line has been converted into kernel-owned configuration.
     * Do not retain its bootloader-provided character buffer.
     */
    kernel_boot_info.command_line = NULL;

    memory_init(
        kernel_boot_info.direct_map_offset,
        kernel_boot_info.memory_regions,
        kernel_boot_info.memory_region_count
    );

    if (!paging_init()) {
        kernel_panic("Unable to initialize paging");
    }

    if (!kernel_mapping_install()) {
        kernel_panic(
            "Unable to install kernel-owned mappings"
        );
    }

    if (!device_mapping_map_framebuffer(
        &kernel_boot_info.framebuffer
    )) {
        kernel_panic(
            "Unable to install kernel framebuffer mapping"
        );
    }

    arch_stack_enter(
        (uint64_t) &kernel_runtime_stack[
            KERNEL_RUNTIME_STACK_SIZE
        ],
        kernel_main_continue
    );
}

static void kernel_input_system_action(
    enum input_system_action action)
{
    switch (action) {
        case INPUT_SYSTEM_ACTION_RESET:
            diagnostics_write(
                "[input] Ctrl+Alt+Delete: resetting system\n"
            );

            arch_reset();

        default:
            kernel_panic(
                "Unknown kernel input system action"
            );
    }
}

static _Noreturn void kernel_main_continue(void)
{
    if (!direct_mapping_install()) {
        kernel_panic(
            "Unable to install MyOS-owned direct map"
        );
    }

    if (!kernel_heap_init()) {
        kernel_panic("Unable to initialize kernel heap");
    }

    display_init(
        &kernel_display,
        &kernel_boot_info.framebuffer
    );

    struct cpu_info cpu;
    cpu_info_read(&cpu);

    struct smbios_system_info system;

    smbios_system_info_read(
        kernel_boot_info.smbios_entry_32,
        kernel_boot_info.smbios_entry_64,
        &system
    );

    /*
     * System identification has been copied into kernel-owned storage.
     * The original SMBIOS entry-point pointers are no longer needed.
     */
    kernel_boot_info.smbios_entry_32 = NULL;
    kernel_boot_info.smbios_entry_64 = NULL;

    enum boot_mode boot_mode =
        kernel_boot_config.mode == KERNEL_BOOT_MODE_TEST
            ? BOOT_MODE_TEST
            : BOOT_MODE_NORMAL;

    boot_banner_print(
        &cpu,
        &system,
        boot_mode
    );

    /*
     * The bootloader-provided command line and SMBIOS entry points
     * must not remain reachable through the persistent boot_info.
     */
    if (
        kernel_boot_info.command_line != NULL ||
        kernel_boot_info.smbios_entry_32 != NULL ||
        kernel_boot_info.smbios_entry_64 != NULL
    ) {
        kernel_panic(
            "Transient boot information remains referenced"
        );
    }

    #if MYOS_RUNTIME_DIAGNOSTICS
    diagnostics_write(
        "[boot] Transient boot-info references released\n"
    );
#endif

    /*
     * Install kernel-owned GDT, TSS and IDT before returning any
     * bootloader-reclaimable frame to the physical allocator.
     *
     * Interrupt controllers and maskable interrupts remain disabled
     * until arch_init(), after general memory reclamation.
     */
    arch_early_init();

    struct boot_paging_inventory boot_inventory;

    if (!boot_paging_reclaim_preflight(
        &boot_inventory
    )) {
        kernel_panic(
            "Inherited page-table reclaim preflight failed"
        );
    }

    uint64_t inherited_table_frames =
        boot_inventory.pml4_frames +
        boot_inventory.pdpt_frames +
        boot_inventory.pd_frames +
        boot_inventory.pt_frames;

    diagnostics_printf(
        "[boot-paging] Reclaim preflight passed: "
        "%u inherited table frames remain reserved\n",
        inherited_table_frames
    );

#if MYOS_RUNTIME_DIAGNOSTICS
    runtime_diagnostics_pre_reclaim();

    struct boot_reclaim_preflight_report premature_report;

    if (
        boot_reclaim_preflight(
            &kernel_boot_info,
            &premature_report
        )
    ) {
        kernel_panic(
            "General boot-memory preflight accepted inherited page tables"
        );
    }

    diagnostics_write(
        "[boot-memory] Premature preflight rejected\n"
    );
#endif

    uint64_t reclaimed_table_frames = 0;

    if (!boot_paging_reclaim(
        &reclaimed_table_frames
    )) {
        kernel_panic(
            "Unable to reclaim inherited page tables"
        );
    }

    if (
        reclaimed_table_frames !=
        inherited_table_frames
    ) {
        kernel_panic(
            "Boot page-table reclaim result differs from preflight"
        );
    }

#if MYOS_RUNTIME_DIAGNOSTICS
    runtime_diagnostics_reclaimed_frame_reuse();
#endif

    diagnostics_printf(
        "[boot-paging] Reclaimed %u inherited page-table frames\n",
        reclaimed_table_frames
    );

    struct boot_reclaim_preflight_report reclaim_report;

    if (
        !boot_reclaim_preflight(
            &kernel_boot_info,
            &reclaim_report
        )
    ) {
        kernel_panic(
            "General boot-memory preflight failed"
        );
    }

    diagnostics_printf(
        "[boot-memory] Read-only preflight passed: "
        "pending=%u free=%u\n",
        reclaim_report.inventory.pending_frames,
        reclaim_report.free_frames
    );

    struct boot_physical_range_audit_report range_audit;

    if (
        !boot_physical_range_audit(
            &kernel_boot_info,
            &reclaim_report,
            &range_audit
        )
    ) {
        kernel_panic(
            "Bootloader physical-range audit failed"
        );
    }

    diagnostics_printf(
        "[boot-memory] Physical-range audit passed: "
        "regions=%u pending=%u "
        "kernel-pages=%u framebuffer-pages=%u\n",
        range_audit.reclaimable_regions,
        range_audit.pending_frames,
        range_audit.kernel_image_pages_checked,
        range_audit.framebuffer_pages_checked
    );

    struct boot_active_paging_audit_report paging_audit;

    if (
        !boot_active_paging_audit(
            &kernel_boot_info,
            &paging_audit
        )
    ) {
        kernel_panic(
            "Active paging-structure audit failed"
        );
    }

    if (paging_audit.pml4_tables != 1) {
        kernel_panic(
            "Active paging audit found an unexpected PML4 count"
        );
    }

    diagnostics_printf(
        "[boot-memory] Active paging audit passed: "
        "PML4=%u PDPT=%u PD=%u PT=%u "
        "1G-leaves=%u 2M-leaves=%u free=%u\n",
        paging_audit.pml4_tables,
        paging_audit.pdpt_tables,
        paging_audit.pd_tables,
        paging_audit.pt_tables,
        paging_audit.large_1g_mappings,
        paging_audit.large_2m_mappings,
        paging_audit.free_frames
    );

#if MYOS_RUNTIME_DIAGNOSTICS
    diagnostics_write(
        "\n--- Bootloader-reclaimable physical ranges ---\n"
    );

    for (
        size_t index = 0;
        index < kernel_boot_info.memory_region_count;
        ++index
    ) {
        const struct memory_region *region =
            &kernel_boot_info.memory_regions[index];

        if (
            region->type !=
            MEMORY_REGION_BOOTLOADER_RECLAIMABLE
        ) {
            continue;
        }

        diagnostics_printf(
            "  [%x, %x)\n",
            region->base,
            region->base + region->length
        );
    }

    runtime_diagnostics_bootloader_frame_inventory(
        &kernel_boot_info
    );

    runtime_diagnostics_general_reclaim_rejections(
        &kernel_boot_info
    );
#endif

    /*
     * All bootloader-data consumers have finished, the kernel owns
     * its runtime mappings and stack, and the read-only audits have
     * completed. Transfer the remaining eligible frames before arch_init().
     */
    struct boot_memory_reclaim_result general_reclaim;

    if (
        !boot_memory_reclaim_remaining(
            &kernel_boot_info,
            &general_reclaim
        )
    ) {
        kernel_panic(
            "General bootloader memory reclamation rejected"
        );
    }

    diagnostics_printf(
        "[boot-memory] Reclaimed %u remaining frames: "
        "free before=%u free after=%u\n",
        general_reclaim.reclaimed_frames,
        general_reclaim.free_before,
        general_reclaim.free_after
    );

#if MYOS_RUNTIME_DIAGNOSTICS
    /*
     * Reclamation must be one-shot. A rejected second call must not
     * change the physical allocator's accounting.
     */
    struct boot_memory_reclaim_result repeated_reclaim;

    if (
        boot_memory_reclaim_remaining(
            &kernel_boot_info,
            &repeated_reclaim
        ) ||
        physical_free_frame_count() !=
            general_reclaim.free_after
    ) {
        kernel_panic(
            "General bootloader reclaim accepted a repeated transfer"
        );
    }

    /*
     * Exercise a frame that was actually recovered by this operation,
     * rather than relying on the allocator's normal allocation order.
     */
    if (general_reclaim.reclaimed_frames != 0) {
        uint64_t probe_frame =
            general_reclaim.first_reclaimed_frame;

        if (
            memory_bootloader_frame_reclaim_pending(
                probe_frame
            ) ||
            !physical_alloc_frame_at(
                probe_frame
            )
        ) {
            kernel_panic(
                "Unable to allocate a generally reclaimed frame"
            );
        }

        volatile uint64_t *probe =
            memory_physical_to_virtual(
                probe_frame
            );

        probe[0] =
            0x1122334455667788ULL;

        if (
            probe[0] !=
            0x1122334455667788ULL
        ) {
            kernel_panic(
                "Generally reclaimed frame readback failed"
            );
        }

        if (
            !physical_free_frame(
                probe_frame
            ) ||
            physical_free_frame_count() !=
                general_reclaim.free_after
        ) {
            kernel_panic(
                "Generally reclaimed frame reuse leaked ownership"
            );
        }
    }

    diagnostics_write(
        "[tests] General bootloader reclaim and frame reuse passed\n"
    );
#endif

    input_init();

    arch_init(
        kernel_boot_info.rsdp_snapshot,
        kernel_boot_info.rsdp_snapshot_size
    );

    diagnostics_write("[arch] x86-64 initialized\n");

    scheduler_init();
    diagnostics_write("[scheduler] Initialized\n");

    diagnostics_write("[kernel] Initialization complete\n");

    /* RTC */
    struct rtc_time time;
    if (rtc_read_time(&time)) {
        diagnostics_printf(
            "[clock] %u:%u:%u UTC\n",
            (uint64_t) time.hours,
            (uint64_t) time.minutes,
            (uint64_t) time.seconds
        );
    } else {
        diagnostics_write("[clock] RTC unavailable\n");
    }

#if MYOS_RUNTIME_DIAGNOSTICS
    diagnostics_write(
        "\n--- kernel runtime test suite ---\n"
    );
    runtime_diagnostics_run(
        &kernel_boot_info.framebuffer
    );
#endif

#if MYOS_KERNEL_TESTS
    if (
        kernel_boot_config.mode ==
        KERNEL_BOOT_MODE_TEST
    ) {
        diagnostics_write(
            "\n--- kernel test suite ---\n"
        );

        boot_config_test_run();

        acpi_test_run(
            kernel_boot_info.rsdp_snapshot,
            kernel_boot_info.rsdp_snapshot_size
        );

        elf64_test_run();
        elf64_loader_test_run();
        interrupt_wait_test_run();
        framebuffer_test_run();
        input_test_run();
        kernel_heap_test_run();
        process_memory_test_run();
        process_lifecycle_test_run();
        process_elf_lifecycle_test_run();
        scheduler_slot_test_run();
        user_process_tests_prepare();
    }
#endif

    input_system_action_handler_set(
        kernel_input_system_action
    );

    /*
     * A chord may have been recognized before the immediate handler
     * became active. Honor one pending action before entering the
     * scheduler.
     */
    enum input_system_action pending_action;

    if (
        input_system_action_take(
            &pending_action
        )
    ) {
        kernel_input_system_action(
            pending_action
        );
    }

    diagnostics_write("[kernel] Starting scheduler\n");
    scheduler_run();
}
