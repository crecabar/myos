// SPDX-License-Identifier: GPL-2.0-only

#include <stdint.h>

#include "arch/x86_64/serial.h"
#include "arch/x86_64/arch.h"
#include "arch/x86_64/paging.h"
#include "arch/x86_64/rtc.h"
#include "arch/x86_64/timer.h"
#include "arch/x86_64/pic.h"
#include "process/layout.h"
#include "process/memory.h"
#include "process/process.h"
#include "scheduler/scheduler.h"
#include "boot/boot.h"
#include "core/panic.h"
#include "diagnostics/diagnostics.h"
#include "diagnostics/runtime.h"
#include "init/display.h"
#include "memory/memory.h"
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

    /* RUNTIME DIAGNOSTICS */
    diagnostics_write("[kernel] Test and diagnostics\n");
    runtime_diagnostics_dump();
    diagnostics_write("[kernel] Test and diagnostics ended\n");

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
    /* Initial user processes */
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
    static const uint8_t process_1_program[] = {
        /* putc('1') */
        0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00,
        0x48, 0xC7, 0xC7, 0x31, 0x00, 0x00, 0x00,
        0xCD, 0x80,
        /* mov rcx, 0x04000000 */
        0x48, 0xC7, 0xC1, 0x00, 0x00, 0x00, 0x04,
        /* dec rcx */
        0x48, 0xFF, 0xC9,
        /* jnz -5 */
        0x75, 0xFB,
        /* putc('A') */
        0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00,
        0x48, 0xC7, 0xC7, 0x41, 0x00, 0x00, 0x00,
        0xCD, 0x80,
        /* exit(0) */
        0x48, 0xC7, 0xC0, 0x02, 0x00, 0x00, 0x00,
        0x48, 0xC7, 0xC7, 0x00, 0x00, 0x00, 0x00,
        0xCD, 0x80,

        /* unreachable */
        0xEB, 0xFE,
    };
    if (!process_memory_write(
        &process_1_memory,
        process_1_layout.code_base,
        process_1_program,
        sizeof(process_1_program))
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

    if (!process_layout_create(
        &process_2_memory,
        &process_2_layout
    )) {
        kernel_panic("Unable to create PID 2 layout");
    }

    /* User program 2 */
    static const uint8_t process_2_program[] = {
        /* putc('2') */
        0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00,
        0x48, 0xC7, 0xC7, 0x32, 0x00, 0x00, 0x00,
        0xCD, 0x80,
        /* mov rcx, 0x04000000 */
        0x48, 0xC7, 0xC1, 0x00, 0x00, 0x00, 0x04,
        /* dec rcx */
        0x48, 0xFF, 0xC9,
        /* jnz -5 */
        0x75, 0xFB,
        /* putc('B') */
        0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00,
        0x48, 0xC7, 0xC7, 0x42, 0x00, 0x00, 0x00,
        0xCD, 0x80,
        /* exit(0) */
        0x48, 0xC7, 0xC0, 0x02, 0x00, 0x00, 0x00,
        0x48, 0xC7, 0xC7, 0x00, 0x00, 0x00, 0x00,
        0xCD, 0x80,
        /* unreachable */
        0xEB, 0xFE,
    };
    if (!process_memory_write(
        &process_2_memory,
        process_2_layout.code_base,
        process_2_program,
        sizeof(process_2_program))
    ) {
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

    diagnostics_write("[timer] Waiting for 100 ticks\n");

    uint64_t start_ticks = timer_ticks();
    uint64_t last_reported_tick = 0;

    while ((timer_ticks() - start_ticks) < 100) {
        __asm__ volatile ("hlt");

        uint64_t elapsed_ticks = timer_ticks() - start_ticks;

        if (elapsed_ticks != 0 &&
            (elapsed_ticks % 20) == 0 &&
            elapsed_ticks != last_reported_tick) {
            diagnostics_printf(
                "[timer] tick %u\n",
                elapsed_ticks
            );

            last_reported_tick = elapsed_ticks;
        }
    }

    diagnostics_printf(
        "[timer] %u ticks received\n",
        timer_ticks() - start_ticks
    );

    diagnostics_write("[kernel] Starting scheduler\n");
    scheduler_run();

}
