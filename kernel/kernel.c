// SPDX-License-Identifier: GPL-2.0-only

#include <stdint.h>

#include "arch/x86_64/arch.h"
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/paging.h"
#include "arch/x86_64/rtc.h"
#include "arch/x86_64/serial.h"
#include "arch/x86_64/stack.h"
#include "boot/boot.h"
#include "config/boot_config.h"
#include "core/panic.h"
#include "diagnostics/diagnostics.h"
#include "init/boot_banner.h"
#include "init/display.h"
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
#include "tests/boot_config_test.h"
#include "tests/elf64_test.h"
#include "tests/elf64_loader_test.h"
#include "tests/framebuffer_test.h"
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

#if MYOS_RUNTIME_DIAGNOSTICS
    runtime_diagnostics_bootloader_frame_inventory();
#endif

    arch_init();
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
        elf64_test_run();
        elf64_loader_test_run();
        interrupt_wait_test_run();
        framebuffer_test_run();
        kernel_heap_test_run();
        process_memory_test_run();
        process_lifecycle_test_run();
        process_elf_lifecycle_test_run();
        scheduler_slot_test_run();
        user_process_tests_prepare();
    }
#endif

    diagnostics_write("[kernel] Starting scheduler\n");
    scheduler_run();
}
