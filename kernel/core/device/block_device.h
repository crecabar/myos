// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file block_device.h
 * @brief Generic read-only block-device capability contract.
 *
 * A block_device layers sector/block-addressed synchronous read access on an
 * existing generic struct device.
 *
 * Identity, topology, registry publication, and lifetime remain owned by the
 * generic device model. The block-device capability does not retain its
 * generic device.
 *
 * The initial contract is deliberately read-only. Write, flush, barrier,
 * discard, asynchronous I/O, DMA constraints, partition policy, VFS
 * publication, and controller-specific behavior remain outside this layer.
 */

#ifndef MYOS_CORE_DEVICE_BLOCK_DEVICE_H
#define MYOS_CORE_DEVICE_BLOCK_DEVICE_H

#include "device.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Result of one generic block-device I/O operation.
 */
enum block_device_io_result {
    BLOCK_DEVICE_IO_RESULT_SUCCESS,
    BLOCK_DEVICE_IO_RESULT_INVALID_ARGUMENT,
    BLOCK_DEVICE_IO_RESULT_OUT_OF_RANGE,
    BLOCK_DEVICE_IO_RESULT_OVERFLOW,
    BLOCK_DEVICE_IO_RESULT_RESOURCE_EXHAUSTED,
    BLOCK_DEVICE_IO_RESULT_IO_ERROR,
};

/**
 * Immutable logical geometry exposed by one block device.
 *
 * logical_block_size is expressed in bytes.
 *
 * block_count is the number of addressable logical blocks.
 *
 * capacity_bytes is the validated product:
 *
 *     logical_block_size * block_count
 *
 * and is guaranteed to fit in uint64_t for every initialized block device.
 */
struct block_device_geometry {
    uint32_t logical_block_size;

    uint64_t block_count;

    uint64_t capacity_bytes;
};

struct block_device;

struct block_device_operations {
    /**
     * Reads exactly block_count logical blocks beginning at first_block.
     *
     * The generic block-device layer validates the complete requested range,
     * buffer address, and addressable transfer size before invoking this
     * callback.
     *
     * block_count is therefore always non-zero when the backend is called.
     * first_block and block_count describe a range fully contained inside the
     * device geometry.
     *
     * Success means the complete requested range was transferred. Partial
     * success is not representable by this initial block contract.
     */
    enum block_device_io_result (*read)(
        struct block_device *block,
        uint64_t first_block,
        uint64_t block_count,
        void *buffer
    );
};

/**
 * Block-device capability attached to one generic device.
 *
 * device is borrowed. The owner must keep both the generic device and this
 * capability alive while the block interface is reachable.
 *
 * geometry and operations are immutable after successful initialization.
 *
 * private_data belongs entirely to the concrete block backend.
 */
struct block_device {
    struct device *device;

    struct block_device_geometry geometry;

    const struct block_device_operations *operations;

    void *private_data;
};

/**
 * Initializes one read-only block-device capability.
 *
 * logical_block_size and block_count must both be non-zero.
 *
 * The resulting byte capacity must fit in uint64_t.
 *
 * operations->read is mandatory because read access is the only public I/O
 * capability exposed by this initial contract.
 *
 * On success the capability is bound to device as DEVICE_CLASS_BLOCK.
 *
 * A device with an already-bound primary class is rejected before the
 * candidate block capability is modified.
 *
 * No generic-device reference is acquired.
 */
bool block_device_initialize(
    struct block_device *block,
    struct device *device,
    uint32_t logical_block_size,
    uint64_t block_count,
    const struct block_device_operations *operations,
    void *private_data
);

/**
 * Discovers the block-device capability bound to one generic device.
 *
 * The returned pointer is borrowed. device must be ACTIVE and its primary
 * class must be DEVICE_CLASS_BLOCK.
 *
 * @return Borrowed block-device interface, or NULL when unavailable.
 */
struct block_device *block_device_from_device(
    struct device *device
);

/**
 * Reports whether block currently exposes readable block I/O.
 */
bool block_device_can_read(
    const struct block_device *block
);

/**
 * Obtains one validated snapshot of the logical device geometry.
 *
 * On success, geometry receives the complete immutable geometry.
 * Failure leaves geometry unchanged.
 */
bool block_device_geometry_get(
    const struct block_device *block,
    struct block_device_geometry *geometry
);

/**
 * Reads complete logical blocks synchronously.
 *
 * Range semantics:
 *
 * - first_block identifies the first logical block;
 * - block_count is the number of complete blocks requested;
 * - block_count == 0 performs no I/O and permits buffer == NULL;
 * - a zero-length range at first_block == device block_count is valid;
 * - first_block beyond the end is OUT_OF_RANGE even for a zero-length read;
 * - non-zero reads must fit completely inside the device.
 *
 * For non-zero requests, the total byte transfer must also fit in size_t so
 * the supplied kernel buffer can describe the complete transfer.
 *
 * The backend is never invoked for a zero-length request or an invalid range.
 */
enum block_device_io_result block_device_read(
    struct block_device *block,
    uint64_t first_block,
    uint64_t block_count,
    void *buffer
);

#endif
