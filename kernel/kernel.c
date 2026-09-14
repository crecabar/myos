// SPDX-License-Identifier: GPL-2.0-only

#include <stdint.h>

#include "arch/x86_64/arch.h"
#include "arch/x86_64/paging.h"
#include "arch/x86_64/rtc.h"
#include "arch/x86_64/serial.h"
#include "boot/boot.h"
#include "core/panic.h"
#include "diagnostics/diagnostics.h"

#include "init/display.h"
#include "memory/memory.h"
#include "scheduler/scheduler.h"

#if MYOS_RUNTIME_DIAGNOSTICS
#include "tests/runtime_diagnostics.h"
#endif

#if MYOS_KERNEL_TESTS
#include "arch/x86_64/tests/interrupt_wait_test.h"
#include "tests/framebuffer_test.h"
#include "tests/process_lifecycle_test.h"
#include "tests/process_memory_test.h"
#include "tests/scheduler_slot_test.h"
#include "tests/user_processes.h"
#endif

#include "version.h"


_Noreturn void kernel_main(void)
{
    serial_init();
    diagnostics_init();

    diagnostics_write("\x1b[2J\x1b[H");
    diagnostics_write(MYOS_VERSION_STRING"\n\n");

    struct boot_info boot_info;
    boot_init(&boot_info);

    diagnostics_write("[boot] Environment initialized\n");

    memory_init(
        boot_info.direct_map_offset,
        boot_info.memory_regions,
        boot_info.memory_region_count
    );

    memory_dump_summary();

    if (!paging_init()) {
        kernel_panic("Unable to initialize paging");
    }

    diagnostics_write("[paging] MyOS address space active\n");

    struct kernel_display display;
    display_init(&display, &boot_info.framebuffer);

    diagnostics_write("[display] Console initialized\n");

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
    runtime_diagnostics_run();
    #endif

    #if MYOS_KERNEL_TESTS
    interrupt_wait_test_run();
    framebuffer_test_run();
    process_memory_test_run();
    process_lifecycle_test_run();
    scheduler_slot_test_run();
    user_process_tests_prepare();
    #endif

    diagnostics_write("[kernel] Starting scheduler\n");
    scheduler_run();

}
