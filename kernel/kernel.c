// SPDX-License-Identifier: GPL-2.0-only

#include <stdint.h>

#include "arch/x86_64/arch.h"
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/paging.h"
#include "arch/x86_64/rtc.h"
#include "arch/x86_64/serial.h"
#include "boot/boot.h"
#include "config/boot_config.h"
#include "core/panic.h"
#include "diagnostics/diagnostics.h"
#include "init/boot_banner.h"
#include "init/display.h"
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


_Noreturn void kernel_main(void)
{
    serial_init();
    diagnostics_init();

    diagnostics_write("\x1b[2J\x1b[H");

    struct boot_info boot_info;
    boot_init(&boot_info);

    struct kernel_boot_config boot_config;

    boot_config_parse(
        boot_info.command_line,
        &boot_config
    );

    memory_init(
        boot_info.direct_map_offset,
        boot_info.memory_regions,
        boot_info.memory_region_count
    );

    if (!paging_init()) {
        kernel_panic("Unable to initialize paging");
    }

    if (!kernel_mapping_install()) {
        kernel_panic(
            "Unable to install kernel-owned mappings"
        );
    }

    if (!kernel_heap_init()) {
        kernel_panic("Unable to initialize kernel heap");
    }

    struct kernel_display display;
    display_init(&display, &boot_info.framebuffer);

    struct cpu_info cpu;
    cpu_info_read(&cpu);

    struct smbios_system_info system;

    smbios_system_info_read(
        boot_info.smbios_entry_32,
        boot_info.smbios_entry_64,
        &system
    );

    enum boot_mode boot_mode =
    boot_config.mode == KERNEL_BOOT_MODE_TEST
        ? BOOT_MODE_TEST
        : BOOT_MODE_NORMAL;

    boot_banner_print(
        &cpu,
        &system,
        boot_mode
    );

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
    runtime_diagnostics_run();
#endif

#if MYOS_KERNEL_TESTS
    if (
        boot_config.mode ==
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
