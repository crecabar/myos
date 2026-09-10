// SPDX-License-Identifier: GPL-2.0-only

#include <stdint.h>

#include "arch/x86_64/serial.h"
#include "arch/x86_64/arch.h"
#include "arch/x86_64/paging.h"
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

    diagnostics_write("[kernel] Initialization complete\n");

    /* RUNTIME DIAGNOSTICS */
    diagnostics_write("[kernel] Test and diagnostics\n");
    runtime_diagnostics_dump();
    diagnostics_write("[kernel] Test and diagnostics ended\n");

    kernel_halt();
}
