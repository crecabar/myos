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

    uint8_t fill_destination[66];

    for (
        size_t index = 0;
        index < sizeof(fill_destination);
        ++index
    ) {
        fill_destination[index] =
            0xCC;
    }

    result =
        memset(
            &fill_destination[1],
            0xA5,
            sizeof(fill_destination) - 2
        );

    if (result != &fill_destination[1]) {
        kernel_panic(
            "memset returned incorrect destination"
        );
    }

    if (
        fill_destination[0] != 0xCC ||
        fill_destination[
            sizeof(fill_destination) - 1
        ] != 0xCC
    ) {
        kernel_panic(
            "memset wrote outside destination range"
        );
    }

    for (
        size_t index = 1;
        index < sizeof(fill_destination) - 1;
        ++index
    ) {
        if (fill_destination[index] != 0xA5) {
            kernel_panic(
                "memset produced incorrect contents"
            );
        }
    }

    /*
     * memset uses the low eight bits of value.
     */
    uint8_t truncated_value = 0;

    if (
        memset(
            &truncated_value,
            0x1234,
            1
        ) != &truncated_value ||
        truncated_value != 0x34
    ) {
        kernel_panic(
            "memset value truncation is incorrect"
        );
    }

    uint8_t memset_zero_size_sentinel =
        0x5A;

    if (
        memset(
            &memset_zero_size_sentinel,
            0,
            0
        ) != &memset_zero_size_sentinel ||
        memset_zero_size_sentinel != 0x5A
    ) {
        kernel_panic(
            "memset zero-size operation is incorrect"
        );
    }

    diagnostics_write(
        "[runtime] memcpy/memset regression tests passed\n"
    );
}
