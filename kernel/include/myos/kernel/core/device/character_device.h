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

#ifndef MYOS_KERNEL_CORE_DEVICE_CHARACTER_DEVICE_H
#define MYOS_KERNEL_CORE_DEVICE_CHARACTER_DEVICE_H

#include <myos/kernel/core/device/device.h>
#include <myos/kernel/core/wait_queue.h>

#include <stdbool.h>
#include <stddef.h>

enum character_device_io_result {
    CHARACTER_DEVICE_IO_RESULT_SUCCESS,
    CHARACTER_DEVICE_IO_RESULT_INVALID_ARGUMENT,
    CHARACTER_DEVICE_IO_RESULT_NOT_SUPPORTED,
    CHARACTER_DEVICE_IO_RESULT_RESOURCE_EXHAUSTED,
    CHARACTER_DEVICE_IO_RESULT_WOULD_BLOCK,
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

    /**
     * Queries whether a positive-length read can currently
     * transfer at least one byte without waiting.
     *
     * Optional capability. Must not consume input.
     * On failure, ready remains unchanged.
     */
    bool (*read_ready)(
        struct character_device *character,
        bool *ready
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

    /**
     * Registrations waiting for a read-availability event.
     *
     * The device does not own registration storage.
     * Registrations must be cancelled before their
     * owners or this character device are destroyed.
     */
    struct wait_queue read_waiters;
};

/**
 * Initializes a character capability over one existing ACTIVE device.
 *
 * At least one of read or write must be implemented.
 *
 * No generic-device reference is acquired. Initialization therefore does not
 * alter the generic object's ownership count.
 *
 * A device with an already-bound primary class is rejected before the
 * candidate character capability is modified.
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
 * Queries current read availability.
 *
 * Returns false if the capability is unsupported,
 * the device is invalid, or ready is NULL.
 *
 * On success, ready receives the availability state.
 */
bool character_device_read_ready(
    struct character_device *character,
    bool *ready
);

/**
 * Registers interest in a future read-availability event.
 *
 * Requires a readable device implementing read_ready.
 * Does not query readiness or invoke the callback
 * immediately.
 *
 * The caller must register and then recheck readiness
 * before blocking, under the same serialization regime
 * used by event producers.
 *
 * Registrations are one-shot. Cancellation uses
 * wait_queue_cancel().
 */
bool character_device_read_wait_register(
    struct character_device *character,
    struct wait_queue_entry *entry,
    wait_queue_notification_fn notification,
    void *context
);

/**
 * Delivers a read-availability event to all registered
 * readers and removes their one-shot registrations.
 *
 * Must execute outside IRQ context and under the
 * device's serialized event-processing regime.
 *
 * Returns the number of readers notified.
 */
size_t character_device_read_wait_notify(
    struct character_device *character
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
