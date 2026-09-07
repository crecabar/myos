// SPDX-License-Identifier: GPL-2.0-only

#ifndef MYOS_INIT_DISPLAY_H
#define MYOS_INIT_DISPLAY_H

#include "../console/console.h"
#include "../drivers/framebuffer.h"

struct kernel_display {
    struct console console;
};

void display_init(
    struct kernel_display *display,
    struct framebuffer *framebuffer
);

#endif
