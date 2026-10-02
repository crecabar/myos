// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file block_device_test.c
 * @brief Generic read-only block-device capability regression tests.
 */

#include "block_device_test.h"

#include "../core/device/block_device.h"
#include "../core/device/device.h"
#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"

#include <stddef.h>
#include <stdint.h>

#define BLOCK_DEVICE_TEST_BLOCK_SIZE 4U
#define BLOCK_DEVICE_TEST_BLOCK_COUNT 8ULL
#define BLOCK_DEVICE_TEST_STORAGE_SIZE \
    (BLOCK_DEVICE_TEST_BLOCK_SIZE * BLOCK_DEVICE_TEST_BLOCK_COUNT)

struct block_device_test_state {
    enum block_device_io_result result;

    size_t read_calls;

    uint64_t last_first_block;
    uint64_t last_block_count;

    uint8_t storage[
        BLOCK_DEVICE_TEST_STORAGE_SIZE
    ];
};

// Private functions and helpers declarations
static enum block_device_io_result block_device_test_read(
    struct block_device *block,
    uint64_t first_block,
    uint64_t block_count,
    void *buffer
);

static void block_device_test_remove_generic(
    struct device *device
);

static void block_device_test_state_initialize(
    struct block_device_test_state *state
);

static void block_device_test_initialization(void);

static void block_device_test_invalid_initialization(void);

static void block_device_test_class_discovery(void);

static void block_device_test_read_dispatch(void);

static void block_device_test_range_validation(void);

static void block_device_test_backend_failure(void);

static void block_device_test_inactive_rejection(void);

// Static local variables
static const struct block_device_operations
    block_device_test_operations = {
        .read =
            block_device_test_read,
    };

static const struct block_device_operations
    block_device_test_empty_operations = {
        .read =
            NULL,
    };

// Public functions implementations
void block_device_test_run(void)
{
    block_device_test_initialization();
    block_device_test_invalid_initialization();
    block_device_test_class_discovery();
    block_device_test_read_dispatch();
    block_device_test_range_validation();
    block_device_test_backend_failure();
    block_device_test_inactive_rejection();

    diagnostics_write(
        "[device] Block-device geometry and read contract tests passed\n"
    );
}

// Private functions and helpers implementations
static enum block_device_io_result block_device_test_read(
    struct block_device *block,
    uint64_t first_block,
    uint64_t block_count,
    void *buffer)
{
    if (
        block == NULL ||
        block->private_data == NULL ||
        block_count == 0 ||
        buffer == NULL
    ) {
        kernel_panic(
            "Block-device test backend received invalid request"
        );
    }

    struct block_device_test_state *state =
        block->private_data;

    ++state->read_calls;

    state->last_first_block =
        first_block;

    state->last_block_count =
        block_count;

    if (
        state->result !=
            BLOCK_DEVICE_IO_RESULT_SUCCESS
    ) {
        return
            state->result;
    }

    size_t block_size =
        block->geometry.logical_block_size;

    size_t source_offset =
        (size_t) first_block *
        block_size;

    size_t transfer_size =
        (size_t) block_count *
        block_size;

    uint8_t *destination =
        buffer;

    for (
        size_t index = 0;
        index < transfer_size;
        ++index
    ) {
        destination[index] =
            state->storage[
                source_offset + index
            ];
    }

    return
        BLOCK_DEVICE_IO_RESULT_SUCCESS;
}

static void block_device_test_remove_generic(
    struct device *device)
{
    if (
        device == NULL ||
        !device_begin_removal(
            device
        ) ||
        !device_finish_removal(
            device
        ) ||
        !device_release(
            device
        )
    ) {
        kernel_panic(
            "Block-device generic fixture cleanup failed"
        );
    }
}

static void block_device_test_state_initialize(
    struct block_device_test_state *state)
{
    if (state == NULL) {
        kernel_panic(
            "Block-device test state initialization received NULL"
        );
    }

    state->result =
        BLOCK_DEVICE_IO_RESULT_SUCCESS;

    state->read_calls =
        0;

    state->last_first_block =
        UINT64_MAX;

    state->last_block_count =
        UINT64_MAX;

    for (
        size_t index = 0;
        index < sizeof(state->storage);
        ++index
    ) {
        state->storage[index] =
            (uint8_t) (
                0x20U +
                index
            );
    }
}

static void block_device_test_initialization(void)
{
    struct device generic;

    uint64_t generic_private =
        0x1122334455667788ULL;

    if (!device_initialize(
        &generic,
        6460,
        "block-test",
        sizeof("block-test") - 1U,
        DEVICE_KIND_VIRTUAL,
        NULL,
        &generic_private
    )) {
        kernel_panic(
            "Block-device generic initialization failed"
        );
    }

    struct block_device_test_state state;

    block_device_test_state_initialize(
        &state
    );

    struct block_device block;

    if (!block_device_initialize(
        &block,
        &generic,
        BLOCK_DEVICE_TEST_BLOCK_SIZE,
        BLOCK_DEVICE_TEST_BLOCK_COUNT,
        &block_device_test_operations,
        &state
    )) {
        kernel_panic(
            "Block-device initialization rejected valid capability"
        );
    }

    struct block_device_geometry geometry = {
        .logical_block_size =
            0,
        .block_count =
            0,
        .capacity_bytes =
            0,
    };

    if (
        block.device !=
            &generic ||
        block.operations !=
            &block_device_test_operations ||
        block.private_data !=
            &state ||
        block.geometry.logical_block_size !=
            BLOCK_DEVICE_TEST_BLOCK_SIZE ||
        block.geometry.block_count !=
            BLOCK_DEVICE_TEST_BLOCK_COUNT ||
        block.geometry.capacity_bytes !=
            BLOCK_DEVICE_TEST_STORAGE_SIZE ||
        generic.device_class !=
            DEVICE_CLASS_BLOCK ||
        generic.class_interface !=
            &block ||
        block_device_from_device(
            &generic
        ) !=
            &block ||
        !block_device_can_read(
            &block
        ) ||
        !block_device_geometry_get(
            &block,
            &geometry
        ) ||
        geometry.logical_block_size !=
            BLOCK_DEVICE_TEST_BLOCK_SIZE ||
        geometry.block_count !=
            BLOCK_DEVICE_TEST_BLOCK_COUNT ||
        geometry.capacity_bytes !=
            BLOCK_DEVICE_TEST_STORAGE_SIZE ||
        generic.reference_count != 1 ||
        generic.private_data !=
            &generic_private
    ) {
        kernel_panic(
            "Block-device initialization produced invalid state"
        );
    }

    block_device_test_remove_generic(
        &generic
    );
}

static void block_device_test_invalid_initialization(void)
{
    struct device generic;

    if (!device_initialize(
        &generic,
        6470,
        "block-invalid",
        sizeof("block-invalid") - 1U,
        DEVICE_KIND_VIRTUAL,
        NULL,
        NULL
    )) {
        kernel_panic(
            "Block-device invalid fixture initialization failed"
        );
    }

    struct block_device block;

    struct block_device_test_state state;

    block_device_test_state_initialize(
        &state
    );

    if (
        block_device_initialize(
            NULL,
            &generic,
            BLOCK_DEVICE_TEST_BLOCK_SIZE,
            BLOCK_DEVICE_TEST_BLOCK_COUNT,
            &block_device_test_operations,
            &state
        ) ||
        block_device_initialize(
            &block,
            NULL,
            BLOCK_DEVICE_TEST_BLOCK_SIZE,
            BLOCK_DEVICE_TEST_BLOCK_COUNT,
            &block_device_test_operations,
            &state
        ) ||
        block_device_initialize(
            &block,
            &generic,
            0,
            BLOCK_DEVICE_TEST_BLOCK_COUNT,
            &block_device_test_operations,
            &state
        ) ||
        block_device_initialize(
            &block,
            &generic,
            BLOCK_DEVICE_TEST_BLOCK_SIZE,
            0,
            &block_device_test_operations,
            &state
        ) ||
        block_device_initialize(
            &block,
            &generic,
            BLOCK_DEVICE_TEST_BLOCK_SIZE,
            BLOCK_DEVICE_TEST_BLOCK_COUNT,
            NULL,
            &state
        ) ||
        block_device_initialize(
            &block,
            &generic,
            BLOCK_DEVICE_TEST_BLOCK_SIZE,
            BLOCK_DEVICE_TEST_BLOCK_COUNT,
            &block_device_test_empty_operations,
            &state
        ) ||
        block_device_initialize(
            &block,
            &generic,
            2,
            UINT64_MAX,
            &block_device_test_operations,
            &state
        )
    ) {
        kernel_panic(
            "Block-device initialization accepted invalid input"
        );
    }

    if (
        generic.device_class !=
            DEVICE_CLASS_NONE ||
        generic.class_interface !=
            NULL
    ) {
        kernel_panic(
            "Rejected block-device initialization bound a class"
        );
    }

    if (!device_begin_removal(
        &generic
    )) {
        kernel_panic(
            "Block-device inactive fixture transition failed"
        );
    }

    if (block_device_initialize(
        &block,
        &generic,
        BLOCK_DEVICE_TEST_BLOCK_SIZE,
        BLOCK_DEVICE_TEST_BLOCK_COUNT,
        &block_device_test_operations,
        &state
    )) {
        kernel_panic(
            "Block-device initialization accepted non-ACTIVE device"
        );
    }

    if (
        !device_finish_removal(
            &generic
        ) ||
        !device_release(
            &generic
        )
    ) {
        kernel_panic(
            "Block-device invalid fixture cleanup failed"
        );
    }
}

static void block_device_test_class_discovery(void)
{
    struct device generic;

    if (!device_initialize(
        &generic,
        6480,
        "block-class",
        sizeof("block-class") - 1U,
        DEVICE_KIND_VIRTUAL,
        NULL,
        NULL
    )) {
        kernel_panic(
            "Block-device class fixture initialization failed"
        );
    }

    struct block_device_test_state state;

    block_device_test_state_initialize(
        &state
    );

    struct block_device first;

    if (
        !block_device_initialize(
            &first,
            &generic,
            BLOCK_DEVICE_TEST_BLOCK_SIZE,
            BLOCK_DEVICE_TEST_BLOCK_COUNT,
            &block_device_test_operations,
            &state
        ) ||
        block_device_from_device(
            &generic
        ) !=
            &first
    ) {
        kernel_panic(
            "Block-device class discovery failed"
        );
    }

    /*
     * Reinitializing the currently published interface must fail without
     * changing its geometry, backend, or private state.
     */
    struct block_device_test_state replacement_state;

    block_device_test_state_initialize(
        &replacement_state
    );

    if (
        block_device_initialize(
            &first,
            &generic,
            8,
            4,
            &block_device_test_operations,
            &replacement_state
        ) ||
        first.device !=
            &generic ||
        first.geometry.logical_block_size !=
            BLOCK_DEVICE_TEST_BLOCK_SIZE ||
        first.geometry.block_count !=
            BLOCK_DEVICE_TEST_BLOCK_COUNT ||
        first.geometry.capacity_bytes !=
            BLOCK_DEVICE_TEST_STORAGE_SIZE ||
        first.operations !=
            &block_device_test_operations ||
        first.private_data !=
            &state ||
        block_device_from_device(
            &generic
        ) !=
            &first
    ) {
        kernel_panic(
            "Block-device self-reinitialization corrupted binding"
        );
    }

    /*
     * An unrelated rejected candidate must also remain untouched.
     */
    struct block_device duplicate = {
        .device =
            (struct device *) &state,
        .geometry = {
            .logical_block_size =
                999,
            .block_count =
                999,
            .capacity_bytes =
                999,
        },
        .operations =
            &block_device_test_operations,
        .private_data =
            &generic,
    };

    if (
        block_device_initialize(
            &duplicate,
            &generic,
            BLOCK_DEVICE_TEST_BLOCK_SIZE,
            BLOCK_DEVICE_TEST_BLOCK_COUNT,
            &block_device_test_operations,
            &state
        ) ||
        duplicate.device !=
            (struct device *) &state ||
        duplicate.geometry.logical_block_size != 999 ||
        duplicate.geometry.block_count != 999 ||
        duplicate.geometry.capacity_bytes != 999 ||
        duplicate.operations !=
            &block_device_test_operations ||
        duplicate.private_data !=
            &generic ||
        block_device_from_device(
            &generic
        ) !=
            &first
    ) {
        kernel_panic(
            "Duplicate block-device binding mutated candidate"
        );
    }

    block_device_test_remove_generic(
        &generic
    );
}

static void block_device_test_read_dispatch(void)
{
    struct device generic;

    if (!device_initialize(
        &generic,
        6490,
        "block-read",
        sizeof("block-read") - 1U,
        DEVICE_KIND_VIRTUAL,
        NULL,
        NULL
    )) {
        kernel_panic(
            "Block-device read fixture initialization failed"
        );
    }

    struct block_device_test_state state;

    block_device_test_state_initialize(
        &state
    );

    struct block_device block;

    if (!block_device_initialize(
        &block,
        &generic,
        BLOCK_DEVICE_TEST_BLOCK_SIZE,
        BLOCK_DEVICE_TEST_BLOCK_COUNT,
        &block_device_test_operations,
        &state
    )) {
        kernel_panic(
            "Block-device read capability initialization failed"
        );
    }

    uint8_t buffer[
        BLOCK_DEVICE_TEST_BLOCK_SIZE * 3U
    ] = {0};

    if (
        block_device_read(
            &block,
            2,
            3,
            buffer
        ) !=
            BLOCK_DEVICE_IO_RESULT_SUCCESS ||
        state.read_calls != 1 ||
        state.last_first_block != 2 ||
        state.last_block_count != 3
    ) {
        kernel_panic(
            "Block-device read dispatch failed"
        );
    }

    size_t expected_offset =
        2U *
        BLOCK_DEVICE_TEST_BLOCK_SIZE;

    for (
        size_t index = 0;
        index < sizeof(buffer);
        ++index
    ) {
        if (
            buffer[index] !=
                state.storage[
                    expected_offset + index
                ]
        ) {
            kernel_panic(
                "Block-device read returned incorrect data"
            );
        }
    }

    /*
     * Empty reads are satisfied by the generic layer and never reach the
     * backend. The one-past-end position is valid for an empty range.
     */
    if (
        block_device_read(
            &block,
            BLOCK_DEVICE_TEST_BLOCK_COUNT,
            0,
            NULL
        ) !=
            BLOCK_DEVICE_IO_RESULT_SUCCESS ||
        state.read_calls != 1
    ) {
        kernel_panic(
            "Block-device zero-length read contract failed"
        );
    }

    block_device_test_remove_generic(
        &generic
    );
}

static void block_device_test_range_validation(void)
{
    struct device generic;

    if (!device_initialize(
        &generic,
        6500,
        "block-range",
        sizeof("block-range") - 1U,
        DEVICE_KIND_VIRTUAL,
        NULL,
        NULL
    )) {
        kernel_panic(
            "Block-device range fixture initialization failed"
        );
    }

    struct block_device_test_state state;

    block_device_test_state_initialize(
        &state
    );

    struct block_device block;

    if (!block_device_initialize(
        &block,
        &generic,
        BLOCK_DEVICE_TEST_BLOCK_SIZE,
        BLOCK_DEVICE_TEST_BLOCK_COUNT,
        &block_device_test_operations,
        &state
    )) {
        kernel_panic(
            "Block-device range capability initialization failed"
        );
    }

    uint8_t buffer[
        BLOCK_DEVICE_TEST_STORAGE_SIZE
    ] = {0};

    /*
     * Full-device and final-block reads are both legal.
     */
    if (
        block_device_read(
            &block,
            0,
            BLOCK_DEVICE_TEST_BLOCK_COUNT,
            buffer
        ) !=
            BLOCK_DEVICE_IO_RESULT_SUCCESS ||
        block_device_read(
            &block,
            BLOCK_DEVICE_TEST_BLOCK_COUNT - 1U,
            1,
            buffer
        ) !=
            BLOCK_DEVICE_IO_RESULT_SUCCESS ||
        state.read_calls != 2
    ) {
        kernel_panic(
            "Block-device valid boundary read failed"
        );
    }

    size_t calls_before =
        state.read_calls;

    if (
        block_device_read(
            &block,
            BLOCK_DEVICE_TEST_BLOCK_COUNT,
            1,
            buffer
        ) !=
            BLOCK_DEVICE_IO_RESULT_OUT_OF_RANGE ||
        block_device_read(
            &block,
            BLOCK_DEVICE_TEST_BLOCK_COUNT + 1U,
            0,
            NULL
        ) !=
            BLOCK_DEVICE_IO_RESULT_OUT_OF_RANGE ||
        block_device_read(
            &block,
            BLOCK_DEVICE_TEST_BLOCK_COUNT - 1U,
            2,
            buffer
        ) !=
            BLOCK_DEVICE_IO_RESULT_OUT_OF_RANGE ||
        block_device_read(
            &block,
            UINT64_MAX,
            UINT64_MAX,
            buffer
        ) !=
            BLOCK_DEVICE_IO_RESULT_OUT_OF_RANGE ||
        state.read_calls !=
            calls_before
    ) {
        kernel_panic(
            "Block-device out-of-range request reached backend"
        );
    }

    if (
        block_device_read(
            &block,
            0,
            1,
            NULL
        ) !=
            BLOCK_DEVICE_IO_RESULT_INVALID_ARGUMENT ||
        state.read_calls !=
            calls_before
    ) {
        kernel_panic(
            "Block-device NULL read buffer was not rejected"
        );
    }

    block_device_test_remove_generic(
        &generic
    );
}

static void block_device_test_backend_failure(void)
{
    struct device generic;

    if (!device_initialize(
        &generic,
        6510,
        "block-failure",
        sizeof("block-failure") - 1U,
        DEVICE_KIND_VIRTUAL,
        NULL,
        NULL
    )) {
        kernel_panic(
            "Block-device failure fixture initialization failed"
        );
    }

    struct block_device_test_state state;

    block_device_test_state_initialize(
        &state
    );

    struct block_device block;

    if (!block_device_initialize(
        &block,
        &generic,
        BLOCK_DEVICE_TEST_BLOCK_SIZE,
        BLOCK_DEVICE_TEST_BLOCK_COUNT,
        &block_device_test_operations,
        &state
    )) {
        kernel_panic(
            "Block-device failure capability initialization failed"
        );
    }

    uint8_t buffer[
        BLOCK_DEVICE_TEST_BLOCK_SIZE
    ] = {0};

    state.result =
        BLOCK_DEVICE_IO_RESULT_IO_ERROR;

    if (
        block_device_read(
            &block,
            0,
            1,
            buffer
        ) !=
            BLOCK_DEVICE_IO_RESULT_IO_ERROR ||
        state.read_calls != 1
    ) {
        kernel_panic(
            "Block-device backend I/O failure was not propagated"
        );
    }

    state.result =
        BLOCK_DEVICE_IO_RESULT_RESOURCE_EXHAUSTED;

    if (
        block_device_read(
            &block,
            0,
            1,
            buffer
        ) !=
            BLOCK_DEVICE_IO_RESULT_RESOURCE_EXHAUSTED ||
        state.read_calls != 2
    ) {
        kernel_panic(
            "Block-device backend resource failure was not propagated"
        );
    }

    state.result =
        (enum block_device_io_result) 99;

    if (
        block_device_read(
            &block,
            0,
            1,
            buffer
        ) !=
            BLOCK_DEVICE_IO_RESULT_INVALID_ARGUMENT ||
        state.read_calls != 3
    ) {
        kernel_panic(
            "Block-device invalid backend result was not contained"
        );
    }

    block_device_test_remove_generic(
        &generic
    );
}

static void block_device_test_inactive_rejection(void)
{
    struct device generic;

    if (!device_initialize(
        &generic,
        6520,
        "block-inactive",
        sizeof("block-inactive") - 1U,
        DEVICE_KIND_VIRTUAL,
        NULL,
        NULL
    )) {
        kernel_panic(
            "Block-device inactive fixture initialization failed"
        );
    }

    struct block_device_test_state state;

    block_device_test_state_initialize(
        &state
    );

    struct block_device block;

    if (!block_device_initialize(
        &block,
        &generic,
        BLOCK_DEVICE_TEST_BLOCK_SIZE,
        BLOCK_DEVICE_TEST_BLOCK_COUNT,
        &block_device_test_operations,
        &state
    )) {
        kernel_panic(
            "Block-device inactive capability initialization failed"
        );
    }

    if (!device_begin_removal(
        &generic
    )) {
        kernel_panic(
            "Block-device inactive fixture failed to begin removal"
        );
    }

    struct block_device_geometry geometry = {
        .logical_block_size =
            91,
        .block_count =
            92,
        .capacity_bytes =
            93,
    };

    uint8_t buffer[
        BLOCK_DEVICE_TEST_BLOCK_SIZE
    ] = {0};

    if (
        block_device_from_device(
            &generic
        ) != NULL ||
        block_device_can_read(
            &block
        ) ||
        block_device_geometry_get(
            &block,
            &geometry
        ) ||
        geometry.logical_block_size != 91 ||
        geometry.block_count != 92 ||
        geometry.capacity_bytes != 93 ||
        block_device_read(
            &block,
            0,
            1,
            buffer
        ) !=
            BLOCK_DEVICE_IO_RESULT_INVALID_ARGUMENT ||
        state.read_calls != 0
    ) {
        kernel_panic(
            "Inactive block device remained usable or discoverable"
        );
    }

    if (
        !device_finish_removal(
            &generic
        ) ||
        !device_release(
            &generic
        )
    ) {
        kernel_panic(
            "Block-device inactive fixture cleanup failed"
        );
    }
}
