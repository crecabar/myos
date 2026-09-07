// SPDX-License-Identifier: GPL-2.0-only

#ifndef MYOS_MEMORY_MEMORY_H
#define MYOS_MEMORY_MEMORY_H

#include <stdint.h>

void memory_init(uint64_t direct_map_offset);

void *memory_physical_to_virtual(
    uint64_t physical_address
);

#endif
