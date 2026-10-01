// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

 /**
 * @file registry.h
 * @brief Generic kernel device publication registry.
 *
 * The registry owns discovery visibility, not device storage. Every registered
 * device contributes one registry-owned device reference for as long as it is
 * published.
 *
 * Registration requires an ACTIVE device. Unregistration begins logical
 * removal, withdraws the device from discovery, and transfers the
 * registry-owned reference to the detach caller. The caller then owns the
 * REMOVING device while device-specific teardown completes.
 *
 * Enumeration is deterministic while the registry remains unchanged.
 * Successful publication mutations invalidate existing iterators rather than
 * allowing index compaction or insertion to cause silent skips or duplicates.
 */

#ifndef MYOS_CORE_DEVICE_REGISTRY_H
#define MYOS_CORE_DEVICE_REGISTRY_H

#include "device.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DEVICE_REGISTRY_CAPACITY 128U

enum device_registry_register_result {
    DEVICE_REGISTRY_REGISTER_RESULT_REGISTERED,
    DEVICE_REGISTRY_REGISTER_RESULT_INVALID_ARGUMENT,
    DEVICE_REGISTRY_REGISTER_RESULT_ALREADY_REGISTERED,
    DEVICE_REGISTRY_REGISTER_RESULT_DUPLICATE_IDENTIFIER,
    DEVICE_REGISTRY_REGISTER_RESULT_DUPLICATE_NAME,
    DEVICE_REGISTRY_REGISTER_RESULT_RESOURCE_EXHAUSTED,
};

enum device_registry_lookup_result {
    DEVICE_REGISTRY_LOOKUP_RESULT_FOUND,
    DEVICE_REGISTRY_LOOKUP_RESULT_NOT_FOUND,
    DEVICE_REGISTRY_LOOKUP_RESULT_INVALID_ARGUMENT,
};

enum device_registry_unregister_result {
    DEVICE_REGISTRY_UNREGISTER_RESULT_UNREGISTERED,
    DEVICE_REGISTRY_UNREGISTER_RESULT_INVALID_ARGUMENT,
    DEVICE_REGISTRY_UNREGISTER_RESULT_NOT_FOUND,
    DEVICE_REGISTRY_UNREGISTER_RESULT_REMOVAL_BLOCKED,
};

enum device_registry_iteration_result {
    DEVICE_REGISTRY_ITERATION_RESULT_DEVICE,
    DEVICE_REGISTRY_ITERATION_RESULT_END,
    DEVICE_REGISTRY_ITERATION_RESULT_INVALIDATED,
    DEVICE_REGISTRY_ITERATION_RESULT_INVALID_ARGUMENT,
};

struct device_registry {
    struct device *devices[
        DEVICE_REGISTRY_CAPACITY
    ];

    size_t count;

    /*
     * Mutation generation used to invalidate deterministic iterators.
     * Zero is reserved for uninitialized/invalid registry state.
     */
    uint64_t generation;
};

/**
 * Cursor over one stable registry generation.
 *
 * iterator does not own registry. The registry object must therefore outlive
 * the iterator.
 *
 * Each successful next operation returns a separate owned device reference.
 */
struct device_registry_iterator {
    const struct device_registry *registry;

    uint64_t generation;

    size_t index;
};

/**
 * Initializes an empty caller-owned registry.
 */
bool device_registry_initialize(
    struct device_registry *registry
);

/**
 * Publishes one ACTIVE device.
 *
 * Identifier and internal name must both be unique within registry.
 * On success, registry owns one additional device reference.
 */
enum device_registry_register_result device_registry_register(
    struct device_registry *registry,
    struct device *device
);

/**
 * Looks up a registered device by opaque identifier.
 *
 * On success, result receives one owned device reference that the caller must
 * release. Failure leaves result unchanged.
 */
enum device_registry_lookup_result device_registry_lookup_identifier(
    const struct device_registry *registry,
    uint64_t identifier,
    struct device **result
);

/**
 * Looks up a registered device by exact internal name.
 *
 * name is an explicit byte sequence and need not be NUL-terminated.
 *
 * On success, result receives one owned device reference that the caller must
 * release. Failure leaves result unchanged.
 */
enum device_registry_lookup_result device_registry_lookup_name(
    const struct device_registry *registry,
    const char *name,
    size_t name_length,
    struct device **result
);

/**
 * Withdraws one registered device.
 *
 * The operation:
 *
 * 1. begins ACTIVE -> REMOVING;
 * 2. removes the device from registry discovery;
 * 3. transfers the registry-owned reference to the caller.
 *
 * On success, detached receives one owned device reference. The caller must
 * use that ownership to complete device-specific teardown, eventually call
 * device_finish_removal(), and release the transferred reference.
 *
 * The device deliberately remains REMOVING after success. Existing references
 * remain valid, but no new device_retain() or registry lookup can acquire the
 * device.
 *
 * Failure leaves detached unchanged.
 */
enum device_registry_unregister_result device_registry_unregister(
    struct device_registry *registry,
    struct device *device,
    struct device **detached
);

/**
 * Initializes an iterator over the registry's current deterministic order.
 *
 * Devices are visited in registration order among the devices that remain
 * registered. Unregistration preserves the relative order of survivors.
 *
 * Any successful register or unregister after initialization invalidates this
 * iterator.
 */
bool device_registry_iterator_initialize(
    const struct device_registry *registry,
    struct device_registry_iterator *iterator
);

/**
 * Acquires the next device from one registry iteration.
 *
 * On DEVICE, result receives one owned reference that the caller must release.
 *
 * END means the captured generation was consumed completely. result remains
 * unchanged.
 *
 * INVALIDATED means the registry changed since iterator initialization.
 * result and iterator index remain unchanged.
 *
 * INVALID_ARGUMENT also leaves result and iterator index unchanged.
 */
enum device_registry_iteration_result device_registry_iterator_next(
    struct device_registry_iterator *iterator,
    struct device **result
);

#endif
