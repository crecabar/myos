// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file runtime_memory_test.c
 * @brief Freestanding runtime memory primitive regression tests.
 */

#include "runtime_memory_test.h"

#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../runtime/memory.h"

#include <stddef.h>
#include <stdint.h>

// Public functions implementations
void runtime_memory_test_run(void)
{
    uint8_t source[64];
    uint8_t destination[66];

    for (
        size_t index = 0;
        index < sizeof(source);
        ++index
    ) {
        source[index] =
            (uint8_t) (index ^ 0xA5U);
    }

    for (
        size_t index = 0;
        index < sizeof(destination);
        ++index
    ) {
        destination[index] = 0xCC;
    }

    void *result =
        memcpy(
            &destination[1],
            source,
            sizeof(source)
        );

    if (result != &destination[1]) {
        kernel_panic(
            "memcpy returned incorrect destination"
        );
    }

    if (
        destination[0] != 0xCC ||
        destination[
            sizeof(destination) - 1
        ] != 0xCC
    ) {
        kernel_panic(
            "memcpy wrote outside destination range"
        );
    }

    for (
        size_t index = 0;
        index < sizeof(source);
        ++index
    ) {
        if (
            destination[index + 1] !=
            source[index]
        ) {
            kernel_panic(
                "memcpy produced incorrect contents"
            );
        }
    }

    uint8_t zero_size_sentinel = 0x5A;

    if (
        memcpy(
            &zero_size_sentinel,
            source,
            0
        ) != &zero_size_sentinel ||
        zero_size_sentinel != 0x5A
    ) {
        kernel_panic(
            "memcpy zero-size operation is incorrect"
        );
    }

    diagnostics_write(
        "[runtime] memcpy regression test passed\n"
    );
}
