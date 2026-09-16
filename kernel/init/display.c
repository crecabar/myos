// SPDX-License-Identifier: GPL-2.0-only

#include "display.h"
#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../drivers/framebuffer.h"

#include <stddef.h>

void display_init(
    struct kernel_display *display,
    struct framebuffer *framebuffer)
{
    if (display == NULL) {
        kernel_panic(
            "display_init received NULL display"
        );
    }

    if (framebuffer == NULL) {
        kernel_panic(
            "display_init received NULL framebuffer"
        );
    }

    uint32_t white =
        framebuffer_make_color(
            framebuffer,
            255,
            255,
            255
        );

    console_init(
        &display->console,
        framebuffer,
        8,
        4,
        white,
        2
    );

    diagnostics_attach_console(
        &display->console
    );
}
