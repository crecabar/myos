// SPDX-License-Identifier: GPL-2.0-only

#ifndef MYOS_BOOT_BOOT_H
#define MYOS_BOOT_BOOT_H

#include <stdint.h>

#include "../drivers/framebuffer.h"

struct boot_info {
    uint64_t direct_map_offset;
    struct framebuffer framebuffer;
};

void boot_init(
    struct boot_info *boot_info
);

#endif
