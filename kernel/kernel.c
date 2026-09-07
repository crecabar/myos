// SPDX-License-Identifier: GPL-2.0-only

#include <stdint.h>

#include "arch/x86_64/serial.h"
#include "arch/x86_64/arch.h"
#include "boot/boot.h"
#include "core/panic.h"
#include "diagnostics/diagnostics.h"
#include "diagnostics/runtime.h"
#include "init/display.h"
#include "memory/memory.h"
#include "version.h"

_Noreturn void kernel_main(void)
{
    /*
     * Phase 1: Early diagnostics.
     *
     * Serial is our only reliable output channel at this point.
     */
    serial_init();
    diagnostics_init();

    diagnostics_write("\x1b[2J\x1b[H");
    diagnostics_write(MYOS_VERSION_STRING"\n\n");

    /*
     * Phase 2: Boot environment.
     */
    struct boot_info boot_info;
    boot_init(&boot_info);
    memory_init(
        boot_info.direct_map_offset,
        boot_info.memory_regions,
        boot_info.memory_region_count
        );

    diagnostics_write("Framebuffer ready\n");

    /*
     * Phase 3: Graphical diagnostics.
     */
    struct kernel_display display;
    display_init(&display, &boot_info.framebuffer);

    diagnostics_write(MYOS_VERSION_STRING "\n\n");

    /*
     * Phase 4: Initialize CPU architecture facilities.
     *
     * From here on, exceptions handled by our IDT can also
     * report through the graphical console.
     */
    arch_init();
    diagnostics_write("Architecture initialized\n");

    /*
     * Phase 5: Runtime diagnostics.
     */
    runtime_diagnostics_dump();

    kernel_halt();
}
