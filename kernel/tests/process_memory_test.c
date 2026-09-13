// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file process_memory_test.c
 * @brief Process address-space memory regression tests.
 */

#include "process_memory_test.h"

#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../process/layout.h"
#include "../process/memory.h"

#include <stddef.h>
#include <stdint.h>

void process_memory_test_run(void)
{
    struct process_memory memory;
    struct process_layout layout;

    static const uint8_t source[] = "Hello from user memory";
    uint8_t destination[sizeof(source)];

    if (!process_memory_create(&memory)) {
        kernel_panic("Unable to create process memory test address space");
    }

    if (!process_layout_create(
        &memory,
        &layout
    )) {
        kernel_panic("Unable to create process memory test layout");
    }

    if (!process_memory_write(
        &memory,
        layout.code_base,
        source,
        sizeof(source)
    )) {
        kernel_panic("Unable to write process memory test");
    }

    if (!process_memory_read(
        &memory,
        layout.code_base,
        destination,
        sizeof(destination)
    )) {
        kernel_panic("Unable to read process memory test");
    }

    for (size_t index = 0; index < sizeof(source); ++index) {
        if (source[index] != destination[index]) {
            kernel_panic("Process memory copy test failed");
        }
    }

    if (!process_layout_destroy(
        &memory,
        &layout
    )) {
        kernel_panic("Unable to destroy process memory test layout");
    }

    if (!process_memory_destroy(&memory)) {
        kernel_panic("Unable to destroy process memory test address space");
    }

    diagnostics_write("[process] User memory copy test passed\n");
}
