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
 * removal, withdraws the device from discovery, and releases the registry
 * reference. The caller remains responsible for completing device-specific
 * teardown and eventually calling device_finish_removal().
 */

#ifndef MYOS_CORE_DEVICE_REGISTRY_H
#define MYOS_CORE_DEVICE_REGISTRY_H

#include "device.h"

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

struct device_registry {
    struct device *devices[
        DEVICE_REGISTRY_CAPACITY
    ];

    size_t count;
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

#endif
