// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file registry.c
 * @brief Generic kernel device publication registry implementation.
 */

#include "registry.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Private functions and helpers declarations
static bool device_registry_valid(
    const struct device_registry *registry
);

static bool device_registry_name_valid(
    const char *name,
    size_t name_length
);

static bool device_registry_names_equal(
    const struct device *device,
    const char *name,
    size_t name_length
);

static size_t device_registry_find_pointer(
    const struct device_registry *registry,
    const struct device *device
);

static size_t device_registry_find_identifier(
    const struct device_registry *registry,
    uint64_t identifier
);

static size_t device_registry_find_name(
    const struct device_registry *registry,
    const char *name,
    size_t name_length
);

// Public functions implementations
bool device_registry_initialize(
    struct device_registry *registry)
{
    if (registry == NULL) {
        return false;
    }

    for (
        size_t index = 0;
        index < DEVICE_REGISTRY_CAPACITY;
        ++index
    ) {
        registry->devices[index] =
            NULL;
    }

    registry->count =
        0;

    return true;
}

enum device_registry_register_result device_registry_register(
    struct device_registry *registry,
    struct device *device)
{
    if (
        !device_registry_valid(
            registry
        ) ||
        device == NULL ||
        device->reference_count == 0 ||
        device->state !=
            DEVICE_STATE_ACTIVE
    ) {
        return
            DEVICE_REGISTRY_REGISTER_RESULT_INVALID_ARGUMENT;
    }

    if (
        device_registry_find_pointer(
            registry,
            device
        ) != SIZE_MAX
    ) {
        return
            DEVICE_REGISTRY_REGISTER_RESULT_ALREADY_REGISTERED;
    }

    if (
        device_registry_find_identifier(
            registry,
            device->identifier
        ) != SIZE_MAX
    ) {
        return
            DEVICE_REGISTRY_REGISTER_RESULT_DUPLICATE_IDENTIFIER;
    }

    if (
        device_registry_find_name(
            registry,
            device->name,
            device->name_length
        ) != SIZE_MAX
    ) {
        return
            DEVICE_REGISTRY_REGISTER_RESULT_DUPLICATE_NAME;
    }

    if (
        registry->count >=
        DEVICE_REGISTRY_CAPACITY
    ) {
        return
            DEVICE_REGISTRY_REGISTER_RESULT_RESOURCE_EXHAUSTED;
    }

    if (!device_retain(
        device
    )) {
        return
            DEVICE_REGISTRY_REGISTER_RESULT_INVALID_ARGUMENT;
    }

    registry->devices[
        registry->count
    ] = device;

    ++registry->count;

    return
        DEVICE_REGISTRY_REGISTER_RESULT_REGISTERED;
}

enum device_registry_lookup_result device_registry_lookup_identifier(
    const struct device_registry *registry,
    uint64_t identifier,
    struct device **result)
{
    if (
        !device_registry_valid(
            registry
        ) ||
        identifier ==
            DEVICE_IDENTIFIER_INVALID ||
        result == NULL
    ) {
        return
            DEVICE_REGISTRY_LOOKUP_RESULT_INVALID_ARGUMENT;
    }

    size_t index =
        device_registry_find_identifier(
            registry,
            identifier
        );

    if (index == SIZE_MAX) {
        return
            DEVICE_REGISTRY_LOOKUP_RESULT_NOT_FOUND;
    }

    struct device *device =
        registry->devices[index];

    if (
        device == NULL ||
        !device_retain(
            device
        )
    ) {
        /*
         * Registered entries are required to remain ACTIVE. Failure here means
         * the object is no longer discoverable through the registry contract.
         */
        return
            DEVICE_REGISTRY_LOOKUP_RESULT_NOT_FOUND;
    }

    *result =
        device;

    return
        DEVICE_REGISTRY_LOOKUP_RESULT_FOUND;
}

enum device_registry_lookup_result device_registry_lookup_name(
    const struct device_registry *registry,
    const char *name,
    size_t name_length,
    struct device **result)
{
    if (
        !device_registry_valid(
            registry
        ) ||
        !device_registry_name_valid(
            name,
            name_length
        ) ||
        result == NULL
    ) {
        return
            DEVICE_REGISTRY_LOOKUP_RESULT_INVALID_ARGUMENT;
    }

    size_t index =
        device_registry_find_name(
            registry,
            name,
            name_length
        );

    if (index == SIZE_MAX) {
        return
            DEVICE_REGISTRY_LOOKUP_RESULT_NOT_FOUND;
    }

    struct device *device =
        registry->devices[index];

    if (
        device == NULL ||
        !device_retain(
            device
        )
    ) {
        return
            DEVICE_REGISTRY_LOOKUP_RESULT_NOT_FOUND;
    }

    *result =
        device;

    return
        DEVICE_REGISTRY_LOOKUP_RESULT_FOUND;
}

enum device_registry_unregister_result device_registry_unregister(
    struct device_registry *registry,
    struct device *device,
    struct device **detached)
{
    if (
        !device_registry_valid(
            registry
        ) ||
        device == NULL ||
        detached == NULL
    ) {
        return
            DEVICE_REGISTRY_UNREGISTER_RESULT_INVALID_ARGUMENT;
    }

    size_t index =
        device_registry_find_pointer(
            registry,
            device
        );

    if (index == SIZE_MAX) {
        return
            DEVICE_REGISTRY_UNREGISTER_RESULT_NOT_FOUND;
    }

    if (
        device->state !=
            DEVICE_STATE_ACTIVE
    ) {
        return
            DEVICE_REGISTRY_UNREGISTER_RESULT_INVALID_ARGUMENT;
    }

    /*
     * Establish the logical removal transition before changing discovery.
     * A blocked transition must leave the registry completely unchanged.
     */
    if (!device_begin_removal(
        device
    )) {
        return
            DEVICE_REGISTRY_UNREGISTER_RESULT_REMOVAL_BLOCKED;
    }

    /*
     * Compact the publication table first. From this point onward no new
     * registry lookup can discover the device.
     */
    for (
        size_t move = index;
        move + 1U < registry->count;
        ++move
    ) {
        registry->devices[move] =
            registry->devices[
                move + 1U
            ];
    }

    --registry->count;

    registry->devices[
        registry->count
    ] = NULL;

    /*
     * Registration acquired one reference. Ownership of that exact reference
     * now moves from the registry to the detach caller.
     *
     * Do not call device_release() here: REMOVING deliberately represents the
     * interval in which device-specific teardown may still need the object.
     */
    *detached =
        device;

    return
        DEVICE_REGISTRY_UNREGISTER_RESULT_UNREGISTERED;
}

// Private functions and helpers implementations
static bool device_registry_valid(
    const struct device_registry *registry)
{
    if (
        registry == NULL ||
        registry->count >
            DEVICE_REGISTRY_CAPACITY
    ) {
        return false;
    }

    for (
        size_t index = 0;
        index < registry->count;
        ++index
    ) {
        if (
            registry->devices[index] ==
            NULL
        ) {
            return false;
        }
    }

    for (
        size_t index = registry->count;
        index < DEVICE_REGISTRY_CAPACITY;
        ++index
    ) {
        if (
            registry->devices[index] !=
            NULL
        ) {
            return false;
        }
    }

    return true;
}

static bool device_registry_name_valid(
    const char *name,
    size_t name_length)
{
    if (
        name == NULL ||
        name_length == 0 ||
        name_length >
            DEVICE_NAME_MAX
    ) {
        return false;
    }

    for (
        size_t index = 0;
        index < name_length;
        ++index
    ) {
        if (
            name[index] == '\0' ||
            name[index] == '/'
        ) {
            return false;
        }
    }

    return true;
}

static bool device_registry_names_equal(
    const struct device *device,
    const char *name,
    size_t name_length)
{
    if (
        device == NULL ||
        name == NULL ||
        device->name_length !=
            name_length
    ) {
        return false;
    }

    for (
        size_t index = 0;
        index < name_length;
        ++index
    ) {
        if (
            device->name[index] !=
            name[index]
        ) {
            return false;
        }
    }

    return true;
}

static size_t device_registry_find_pointer(
    const struct device_registry *registry,
    const struct device *device)
{
    if (
        registry == NULL ||
        device == NULL
    ) {
        return SIZE_MAX;
    }

    for (
        size_t index = 0;
        index < registry->count;
        ++index
    ) {
        if (
            registry->devices[index] ==
            device
        ) {
            return index;
        }
    }

    return SIZE_MAX;
}

static size_t device_registry_find_identifier(
    const struct device_registry *registry,
    uint64_t identifier)
{
    if (
        registry == NULL ||
        identifier ==
            DEVICE_IDENTIFIER_INVALID
    ) {
        return SIZE_MAX;
    }

    for (
        size_t index = 0;
        index < registry->count;
        ++index
    ) {
        struct device *device =
            registry->devices[index];

        if (
            device != NULL &&
            device->identifier ==
                identifier
        ) {
            return index;
        }
    }

    return SIZE_MAX;
}

static size_t device_registry_find_name(
    const struct device_registry *registry,
    const char *name,
    size_t name_length)
{
    if (
        registry == NULL ||
        !device_registry_name_valid(
            name,
            name_length
        )
    ) {
        return SIZE_MAX;
    }

    for (
        size_t index = 0;
        index < registry->count;
        ++index
    ) {
        if (device_registry_names_equal(
            registry->devices[index],
            name,
            name_length
        )) {
            return index;
        }
    }

    return SIZE_MAX;
}
