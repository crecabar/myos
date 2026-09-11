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
#if MYOS_RUNTIME_DIAGNOSTICS
#include "diagnostics/runtime.h"
#endif
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

    struct process_memory process_3_memory;
    struct process_layout process_3_layout;
    struct process process_3;

    struct process_memory process_4_memory;
    struct process_layout process_4_layout;
    struct process process_4;

    struct process_memory process_5_memory;
    struct process_layout process_5_layout;
    struct process process_5;

    struct process_memory process_6_memory;
    struct process_layout process_6_layout;
    struct process process_6;

    struct process_memory process_7_memory;
    struct process_layout process_7_layout;
    struct process process_7;

    /* User program 1 */
    if (!process_memory_create(&process_1_memory)) {
        kernel_panic("Unable to create PID 1 address space");
    }

    if (!process_layout_create(&process_1_memory, &process_1_layout)) {
        kernel_panic("Unable to create PID 1 layout");
    }

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

    /* User program 2 */
    if (!process_memory_create(&process_2_memory)) {
        kernel_panic("Unable to create PID 2 address space");
    }

    if (!process_layout_create(&process_2_memory, &process_2_layout)) {
        kernel_panic("Unable to create PID 2 layout");
    }

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

    /* User program 3 */
    if (!process_memory_create(&process_3_memory)) {
        kernel_panic("Unable to create PID 3 address space");
    }
    
    if (!process_layout_create(&process_3_memory, &process_3_layout)) {
        kernel_panic("Unable to create PID 3 layout");
    }

    const struct user_program *program_3 = user_program_malicious_page_fault();
    if (!process_memory_write(
        &process_3_memory,
        process_3_layout.code_base,
        program_3->data,
        program_3->size
    )) {
        kernel_panic("Unable to load PID 3 program");
    }

    if (!process_init(
        &process_3,
        3,
        &process_3_memory,
        &process_3_layout
    )) {
        kernel_panic("Unable to initialize PID 3");
    }

    /* User program 4 */
    if (!process_memory_create(&process_4_memory)) {
        kernel_panic("Unable to create PID 4 address space");
    }
    
    if (!process_layout_create(&process_4_memory, &process_4_layout)) {
        kernel_panic("Unable to create PID 4 layout");
    }

    const struct user_program *program_4 = user_program_malicious_ud2();
    if (!process_memory_write(
        &process_4_memory,
        process_4_layout.code_base,
        program_4->data,
        program_4->size
    )) {
        kernel_panic("Unable to load PID 4 program");
    }

    if (!process_init(
        &process_4,
        4,
        &process_4_memory,
        &process_4_layout
    )) {
        kernel_panic("Unable to initialize PID 4");
    }

    /* User program 5 */
    if (!process_memory_create(&process_5_memory)) {
        kernel_panic("Unable to create PID 5 address space");
    }

    if (!process_layout_create(&process_5_memory, &process_5_layout)) {
        kernel_panic("Unable to create PID 5 layout");
    }

    const struct user_program *program_5 = user_program_malicious_hlt();
    if (!process_memory_write(
        &process_5_memory,
        process_5_layout.code_base,
        program_5->data,
        program_5->size
    )) {
        kernel_panic("Unable to load PID 5 program");
    }

    if (!process_init(
        &process_5,
        5,
        &process_5_memory,
        &process_5_layout
    )) {
        kernel_panic("Unable to initialize PID 5");
    }

    /* User program 6 */
    if (!process_memory_create(&process_6_memory)) {
        kernel_panic("Unable to create PID 6 address space");
    }

    if (!process_layout_create(&process_6_memory, &process_6_layout)) {
        kernel_panic("Unable to create PID 6 layout");
    }

    const struct user_program *program_6 = user_program_survivor();
    if (!process_memory_write(
        &process_6_memory,
        process_6_layout.code_base,
        program_6->data,
        program_6->size
    )) {
        kernel_panic("Unable to load PID 6 program");
    }

    if (!process_init(
        &process_6,
        6,
        &process_6_memory,
        &process_6_layout
    )) {
        kernel_panic("Unable to initialize PID 6");
    }

    /* User program 7 */
    if (!process_memory_create(&process_7_memory)) {
        kernel_panic("Unable to create PID 7 address space");
    }

    if (!process_layout_create(&process_7_memory, &process_7_layout)) {
        kernel_panic("Unable to create PID 7 layout");
    }

    const struct user_program *program_7 = user_program_malicious_x87();
    if (!process_memory_write(
        &process_7_memory,
        process_7_layout.code_base,
        program_7->data,
        program_7->size
    )) {
        kernel_panic("Unable to load PID 7 program");
    }

    if (!process_init(
        &process_7,
        7,
        &process_7_memory,
        &process_7_layout
    )) {
        kernel_panic("Unable to initialize PID 7");
    }

    /* Add user programs to scheduler */
    if (!scheduler_add(&process_1)) {
        kernel_panic("Unable to schedule PID 1");
    }

    if (!scheduler_add(&process_2)) {
        kernel_panic("Unable to schedule PID 2");
    }

    if (!scheduler_add(&process_3)) {
        kernel_panic("Unable to schedule PID 3");
    }

    if (!scheduler_add(&process_4)) {
        kernel_panic("Unable to schedule PID 4");
    }

    if (!scheduler_add(&process_5)) {
        kernel_panic("Unable to schedule PID 5");
    }

    if (!scheduler_add(&process_6)) {
        kernel_panic("Unable to schedule PID 6");
    }

    if (!scheduler_add(&process_7)) {
        kernel_panic("Unable to schedule PID 7");
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
        "  RSP=%x\n"
        "PID 3:\n"
        "  CR3=%x\n"
        "  RIP=%x\n"
        "  RSP=%x\n"
        "PID 4:\n"
        "  CR3=%x\n"
        "  RIP=%x\n"
        "  RSP=%x\n"
        "PID 5:\n"
        "  CR3=%x\n"
        "  RIP=%x\n"
        "  RSP=%x\n"
        "PID 6:\n"
        "  CR3=%x\n"
        "  RIP=%x\n"
        "  RSP=%x\n"
        "PID 7:\n"
        "  CR3=%x\n"
        "  RIP=%x\n"
        "  RSP=%x\n"
        "\n",
        process_1_memory.address_space.pml4_physical,
        process_1_layout.entry_point,
        process_1_layout.stack.stack_top,
        process_2_memory.address_space.pml4_physical,
        process_2_layout.entry_point,
        process_2_layout.stack.stack_top,
        process_3_memory.address_space.pml4_physical,
        process_3_layout.entry_point,
        process_3_layout.stack.stack_top,
        process_4_memory.address_space.pml4_physical,
        process_4_layout.entry_point,
        process_4_layout.stack.stack_top,
        process_5_memory.address_space.pml4_physical,
        process_5_layout.entry_point,
        process_5_layout.stack.stack_top,
        process_6_memory.address_space.pml4_physical,
        process_6_layout.entry_point,
        process_6_layout.stack.stack_top,
        process_7_memory.address_space.pml4_physical,
        process_7_layout.entry_point,
        process_7_layout.stack.stack_top
    );
    /*=======================================================================*/

    diagnostics_write("[kernel] Starting scheduler\n");
    scheduler_run();

}
