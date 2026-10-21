// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#ifndef MYOS_KERNEL_INIT_DISPLAY_H
#define MYOS_KERNEL_INIT_DISPLAY_H

#include <myos/kernel/console/console.h>
#include <myos/kernel/drivers/framebuffer.h>

struct kernel_display {
    struct console console;
};

void display_init(
    struct kernel_display *display,
    struct framebuffer *framebuffer,
    uint64_t scale
);

#endif
