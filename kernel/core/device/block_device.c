// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file block_device.c
 * @brief Generic read-only block-device capability implementation.
 */

#include "block_device.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Private functions and helpers declarations
static bool block_device_geometry_compute(
    uint32_t logical_block_size,
    uint64_t block_count,
    uint64_t *capacity_bytes
);

static bool block_device_geometry_valid(
    const struct block_device *block
);

static bool block_device_valid(
    const struct block_device *block
);

static bool block_device_io_result_valid(
    enum block_device_io_result result
);

// Public functions implementations
bool block_device_initialize(
    struct block_device *block,
    struct device *device,
    uint32_t logical_block_size,
    uint64_t block_count,
    const struct block_device_operations *operations,
    void *private_data)
{
    uint64_t capacity_bytes =
        0;

    if (
        block == NULL ||
        device == NULL ||
        device->reference_count == 0 ||
        device->state !=
            DEVICE_STATE_ACTIVE ||
        operations == NULL ||
        operations->read == NULL ||
        !block_device_geometry_compute(
            logical_block_size,
            block_count,
            &capacity_bytes
        )
    ) {
        return false;
    }

    /*
     * Fully prepare the class object before making it discoverable from the
     * generic device.
     */
    block->device =
        device;

    block->geometry.logical_block_size =
        logical_block_size;

    block->geometry.block_count =
        block_count;

    block->geometry.capacity_bytes =
        capacity_bytes;

    block->operations =
        operations;

    block->private_data =
        private_data;

    if (!device_class_bind(
        device,
        DEVICE_CLASS_BLOCK,
        block
    )) {
        /*
         * Leave a failed candidate in a neutral state. An already-bound class
         * on device remains untouched by device_class_bind().
         */
        block->device =
            NULL;

        block->geometry.logical_block_size =
            0;

        block->geometry.block_count =
            0;

        block->geometry.capacity_bytes =
            0;

        block->operations =
            NULL;

        block->private_data =
            NULL;

        return false;
    }

    return true;
}

struct block_device *block_device_from_device(
    struct device *device)
{
    struct block_device *block =
        device_class_interface(
            device,
            DEVICE_CLASS_BLOCK
        );

    if (
        block == NULL ||
        block->device !=
            device ||
        block->operations == NULL ||
        block->operations->read == NULL ||
        !block_device_geometry_valid(
            block
        )
    ) {
        return NULL;
    }

    return
        block;
}

bool block_device_can_read(
    const struct block_device *block)
{
    return
        block_device_valid(
            block
        ) &&
        block->operations->read !=
            NULL;
}

bool block_device_geometry_get(
    const struct block_device *block,
    struct block_device_geometry *geometry)
{
    if (
        geometry == NULL ||
        !block_device_valid(
            block
        )
    ) {
        return false;
    }

    *geometry =
        block->geometry;

    return true;
}

enum block_device_io_result block_device_read(
    struct block_device *block,
    uint64_t first_block,
    uint64_t block_count,
    void *buffer)
{
    if (!block_device_valid(
        block
    )) {
        return
            BLOCK_DEVICE_IO_RESULT_INVALID_ARGUMENT;
    }

    uint64_t device_block_count =
        block->geometry.block_count;

    /*
     * Validate using subtraction rather than first_block + block_count so an
     * overflowing request cannot wrap into an apparently valid range.
     */
    if (first_block > device_block_count) {
        return
            BLOCK_DEVICE_IO_RESULT_OUT_OF_RANGE;
    }

    /*
     * The empty range immediately after the final block is valid.
     *
     * Capability and range validation deliberately occur before this shortcut.
     */
    if (block_count == 0) {
        return
            BLOCK_DEVICE_IO_RESULT_SUCCESS;
    }

    if (
        first_block ==
            device_block_count ||
        block_count >
            device_block_count -
                first_block
    ) {
        return
            BLOCK_DEVICE_IO_RESULT_OUT_OF_RANGE;
    }

    /*
     * A legal block range can still describe more memory than one kernel
     * address-space object can represent on an architecture where size_t is
     * narrower than the storage geometry.
     */
    if (
        block_count >
            (uint64_t) (
                SIZE_MAX /
                block->geometry.logical_block_size
            )
    ) {
        return
            BLOCK_DEVICE_IO_RESULT_OVERFLOW;
    }

    if (buffer == NULL) {
        return
            BLOCK_DEVICE_IO_RESULT_INVALID_ARGUMENT;
    }

    enum block_device_io_result result =
        block->operations->read(
            block,
            first_block,
            block_count,
            buffer
        );

    if (!block_device_io_result_valid(
        result
    )) {
        return
            BLOCK_DEVICE_IO_RESULT_INVALID_ARGUMENT;
    }

    return
        result;
}

// Private functions and helpers implementations
static bool block_device_geometry_compute(
    uint32_t logical_block_size,
    uint64_t block_count,
    uint64_t *capacity_bytes)
{
    if (
        logical_block_size == 0 ||
        block_count == 0 ||
        capacity_bytes == NULL
    ) {
        return false;
    }

    uint64_t block_size =
        (uint64_t) logical_block_size;

    if (
        block_count >
            UINT64_MAX /
            block_size
    ) {
        return false;
    }

    *capacity_bytes =
        block_count *
        block_size;

    return true;
}

static bool block_device_geometry_valid(
    const struct block_device *block)
{
    if (block == NULL) {
        return false;
    }

    uint64_t capacity_bytes =
        0;

    return
        block_device_geometry_compute(
            block->geometry.logical_block_size,
            block->geometry.block_count,
            &capacity_bytes
        ) &&
        block->geometry.capacity_bytes ==
            capacity_bytes;
}

static bool block_device_valid(
    const struct block_device *block)
{
    if (
        block == NULL ||
        block->device == NULL ||
        block->operations == NULL ||
        block->operations->read == NULL ||
        block->device->reference_count == 0 ||
        block->device->state !=
            DEVICE_STATE_ACTIVE ||
        !block_device_geometry_valid(
            block
        )
    ) {
        return false;
    }

    return
        block_device_from_device(
            block->device
        ) == block;
}

static bool block_device_io_result_valid(
    enum block_device_io_result result)
{
    switch (result) {
        case BLOCK_DEVICE_IO_RESULT_SUCCESS:
        case BLOCK_DEVICE_IO_RESULT_INVALID_ARGUMENT:
        case BLOCK_DEVICE_IO_RESULT_OUT_OF_RANGE:
        case BLOCK_DEVICE_IO_RESULT_OVERFLOW:
        case BLOCK_DEVICE_IO_RESULT_RESOURCE_EXHAUSTED:
        case BLOCK_DEVICE_IO_RESULT_IO_ERROR:
            return true;
    }

    return false;
}
