// SPDX-License-Identifier: GPL-2.0-only

#include <stdint.h>
#include <stddef.h>

#include "arch/x86_64/serial.h"
#include "arch/x86_64/arch.h"
#include "arch/x86_64/paging.h"
#include "arch/x86_64/rtc.h"
#include "process/layout.h"
#include "process/memory.h"
#include "process/process.h"
#include "process/programs.h"
#include "scheduler/scheduler.h"
#include "boot/boot.h"
#include "core/panic.h"
#include "diagnostics/diagnostics.h"
#include "diagnostics/runtime.h"
#include "init/display.h"
#include "memory/memory.h"
#include "version.h"
#include "config.h"


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

#if MYOS_RUNTIME_DIAGNOSTICS
    diagnostics_write("[kernel] Test and diagnostics\n");
    runtime_diagnostics_dump();
    diagnostics_write("[kernel] Test and diagnostics ended\n");
#endif

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

    /*=======================================================================*/
    // process memory write and read test
    struct process_memory memory;
    struct process_layout layout;
    static const uint8_t source[] = "Hello from user memory";
    uint8_t destination[sizeof(source)];

    if (!process_memory_create(&memory)) {
        kernel_panic("Unable to create PID address space");
    }
    if (!process_layout_create(&memory, &layout)) {
        kernel_panic("Unable to create PID layout");
    }

    if (!process_memory_write(
        &memory,
        layout.code_base,
        source,
        sizeof(source)
    )) {
        kernel_panic("Unable to write user memory test");
    }

    if (!process_memory_read(
        &memory,
        layout.code_base,
        destination,
        sizeof(destination)
    )) {
        kernel_panic("Unable to read user memory test");
    }

    for (size_t index = 0; index < sizeof(source); ++index) {
        if (source[index] != destination[index]) {
            kernel_panic("User memory copy test failed");
        }
    }

    if (!process_layout_destroy(&memory, &layout)) {
        kernel_panic("Unable to destroy user memory test layout");
    }

    if (!process_memory_destroy(&memory)) {
        kernel_panic("Unable to destroy user memory test address space");
    }
    diagnostics_write("[process] User memory copy test passed\n");
    // process memory write and read test end.

    /* test user processes */
    struct process_memory process_1_memory;
    struct process_layout process_1_layout;
    struct process process_1;

    struct process_memory process_2_memory;
    struct process_layout process_2_layout;
    struct process process_2;

    if (!process_memory_create(&process_1_memory)) {
        kernel_panic("Unable to create PID 1 address space");
    }

    if (!process_layout_create(&process_1_memory, &process_1_layout)) {
        kernel_panic("Unable to create PID 1 layout");
    }

    /* User program 1 */
    const struct user_program *program_1 = user_program_hello();
    if (!process_memory_write(
        &process_1_memory,
        process_1_layout.code_base,
        program_1->data,
        program_1->size)
    ) {
        kernel_panic("Unable to load PID 1 program");
    }

    if (!process_init(
        &process_1,
        1,
        &process_1_memory,
        &process_1_layout
    )) {
        kernel_panic("Unable to initialize PID 1");
    }

    if (!process_memory_create(&process_2_memory)) {
        kernel_panic("Unable to create PID 2 address space");
    }

    if (!process_layout_create(&process_2_memory, &process_2_layout)) {
        kernel_panic("Unable to create PID 2 layout");
    }

    /* User program 2 */
    const struct user_program *program_2 = user_program_counter();
    if (!process_memory_write(
        &process_2_memory,
        process_2_layout.code_base,
        program_2->data,
        program_2->size))
    {
        kernel_panic("Unable to load PID 2 program");
    }

    if (!process_init(
        &process_2,
        2,
        &process_2_memory,
        &process_2_layout
    )) {
        kernel_panic("Unable to initialize PID 2");
    }

    if (!scheduler_add(&process_1)) {
        kernel_panic("Unable to schedule PID 1");
    }

    if (!scheduler_add(&process_2)) {
        kernel_panic("Unable to schedule PID 2");
    }

    diagnostics_printf(
        "\n--- Initial processes ---\n"
        "PID 1:\n"
        "  CR3=%x\n"
        "  RIP=%x\n"
        "  RSP=%x\n"
        "PID 2:\n"
        "  CR3=%x\n"
        "  RIP=%x\n"
        "  RSP=%x\n",
        process_1_memory.address_space.pml4_physical,
        process_1_layout.entry_point,
        process_1_layout.stack.stack_top,
        process_2_memory.address_space.pml4_physical,
        process_2_layout.entry_point,
        process_2_layout.stack.stack_top
    );
    /*=======================================================================*/

    diagnostics_write("[kernel] Starting scheduler\n");
    scheduler_run();

}
