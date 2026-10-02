// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file character_device.h
 * @brief Generic character-device capability contract.
 *
 * A character_device layers stream-style read/write capabilities on an
 * existing generic struct device.
 *
 * The generic device remains responsible for identity, topology, registry
 * publication, and lifetime. The character-device layer does not own or
 * retain the generic device.
 *
 * character_device private_data is independent from device.private_data.
 * Backends may therefore keep class-specific state without consuming the
 * generic device's private storage.
 *
 * The initial contract is synchronous and intentionally small. ioctl, polling,
 * terminal semantics, class registration, and dynamic-removal behavior remain
 * outside this bootstrap slice.
 */

#ifndef MYOS_CORE_DEVICE_CHARACTER_DEVICE_H
#define MYOS_CORE_DEVICE_CHARACTER_DEVICE_H

#include "device.h"

#include <stdbool.h>
#include <stddef.h>

enum character_device_io_result {
    CHARACTER_DEVICE_IO_RESULT_SUCCESS,
    CHARACTER_DEVICE_IO_RESULT_INVALID_ARGUMENT,
    CHARACTER_DEVICE_IO_RESULT_NOT_SUPPORTED,
    CHARACTER_DEVICE_IO_RESULT_RESOURCE_EXHAUSTED,
};

struct character_device;

struct character_device_operations {
    /**
     * Reads up to size bytes from one character device.
     *
     * On success, bytes_read receives a value no greater than size.
     * Failure must not transfer ownership of any resource to the caller.
     */
    enum character_device_io_result (*read)(
        struct character_device *character,
        void *buffer,
        size_t size,
        size_t *bytes_read
    );

    /**
     * Writes up to size bytes to one character device.
     *
     * On success, bytes_written receives a value no greater than size.
     */
    enum character_device_io_result (*write)(
        struct character_device *character,
        const void *buffer,
        size_t size,
        size_t *bytes_written
    );
};

/**
 * Character-device capability attached to one generic device.
 *
 * device is borrowed. The owner must keep both the generic device and this
 * capability object alive while the character interface is reachable.
 *
 * operations is immutable for the lifetime of the initialized capability.
 *
 * private_data belongs entirely to the character-device implementation.
 */
struct character_device {
    struct device *device;

    const struct character_device_operations *operations;

    void *private_data;
};

/**
 * Initializes a character capability over one existing ACTIVE device.
 *
 * At least one of read or write must be implemented.
 *
 * No generic-device reference is acquired. Initialization therefore does not
 * alter the generic object's ownership count.
 */
bool character_device_initialize(
    struct character_device *character,
    struct device *device,
    const struct character_device_operations *operations,
    void *private_data
);

/**
 * Discovers the character-device capability bound to one generic device.
 *
 * The returned pointer is borrowed. The device must be ACTIVE and its primary
 * class must be DEVICE_CLASS_CHARACTER.
 *
 * @return Borrowed character-device interface, or NULL when unavailable.
 */
struct character_device *character_device_from_device(
    struct device *device
);

/**
 * Reports whether character currently exposes a readable ACTIVE device.
 */
bool character_device_can_read(
    const struct character_device *character
);

/**
 * Reports whether character currently exposes a writable ACTIVE device.
 */
bool character_device_can_write(
    const struct character_device *character
);

/**
 * Performs one synchronous character-device read.
 *
 * A supported zero-length read succeeds without invoking the backend and does
 * not require buffer to be non-NULL.
 *
 * bytes_read is modified only on success.
 */
enum character_device_io_result character_device_read(
    struct character_device *character,
    void *buffer,
    size_t size,
    size_t *bytes_read
);

/**
 * Performs one synchronous character-device write.
 *
 * A supported zero-length write succeeds without invoking the backend and does
 * not require buffer to be non-NULL.
 *
 * bytes_written is modified only on success.
 */
enum character_device_io_result character_device_write(
    struct character_device *character,
    const void *buffer,
    size_t size,
    size_t *bytes_written
);

#endif
