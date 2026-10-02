// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file bus.c
 * @brief Generic kernel bus membership and driver-binding implementation.
 */

#include "bus.h"

#include <stdbool.h>
#include <stddef.h>

struct bus_enumeration_context {
    struct bus *bus;

    bool attach_failed;
};

// Private functions and helpers declarations
static bool bus_name_valid(
    const char *name,
    size_t name_length
);

static bool bus_names_equal(
    const char *left,
    size_t left_length,
    const char *right,
    size_t right_length
);

static bool bus_driver_valid(
    const struct driver *driver
);

static size_t bus_find_device(
    const struct bus *bus,
    const struct device *device
);

static size_t bus_find_driver(
    const struct bus *bus,
    const struct driver *driver
);

static size_t bus_find_driver_name(
    const struct bus *bus,
    const struct driver *driver
);

static bool bus_try_bind_driver(
    struct bus_device_entry *entry,
    struct driver *driver
);

static void bus_try_bind_registered_drivers(
    struct bus *bus,
    struct bus_device_entry *entry
);

static void bus_unbind_entry(
    struct bus_device_entry *entry
);

static enum bus_device_attach_result bus_attach_registry_result(
    enum device_registry_register_result result
);

static bool bus_enumeration_discovered(
    struct device *device,
    void *context
);

static bool bus_enumeration_rollback(
    struct bus *bus,
    size_t initial_device_count
);

// Public functions implementations
bool bus_initialize(
    struct bus *bus,
    const char *name,
    size_t name_length,
    struct device_registry *registry,
    const struct bus_operations *operations,
    void *private_data)
{
    if (
        bus == NULL ||
        !bus_name_valid(
            name,
            name_length
        ) ||
        registry == NULL
    ) {
        return false;
    }

    /*
     * Validate the supplied registry through its public contract rather than
     * duplicating registry-internal structural validation here.
     */
    struct device_registry_iterator iterator;

    if (!device_registry_iterator_initialize(
        registry,
        &iterator
    )) {
        return false;
    }

    for (
        size_t index = 0;
        index < name_length;
        ++index
    ) {
        bus->name[index] =
            name[index];
    }

    bus->name[
        name_length
    ] = '\0';

    bus->name_length =
        name_length;

    bus->registry =
        registry;

    bus->operations =
        operations;

    bus->private_data =
        private_data;

    bus->enumeration_active =
        false;

    for (
        size_t index = 0;
        index < BUS_DEVICE_CAPACITY;
        ++index
    ) {
        bus->devices[index].device =
            NULL;

        bus->devices[index].driver =
            NULL;

        bus->devices[index].binding_state =
            BUS_BINDING_STATE_UNBOUND;
    }

    bus->device_count =
        0;

    for (
        size_t index = 0;
        index < BUS_DRIVER_CAPACITY;
        ++index
    ) {
        bus->drivers[index] =
            NULL;
    }

    bus->driver_count =
        0;

    return true;
}

enum bus_enumeration_result bus_enumerate(
    struct bus *bus)
{
    if (
        bus == NULL ||
        bus->registry == NULL ||
        bus->device_count >
            BUS_DEVICE_CAPACITY ||
        bus->driver_count >
            BUS_DRIVER_CAPACITY ||
        bus->enumeration_active
    ) {
        return
            BUS_ENUMERATION_RESULT_INVALID_ARGUMENT;
    }

    if (
        bus->operations == NULL ||
        bus->operations->enumerate == NULL
    ) {
        return
            BUS_ENUMERATION_RESULT_NOT_SUPPORTED;
    }

    size_t initial_device_count =
        bus->device_count;

    struct bus_enumeration_context context = {
        .bus =
            bus,
        .attach_failed =
            false,
    };

    bus->enumeration_active =
        true;

    bool discovered =
        bus->operations->enumerate(
            bus,
            bus_enumeration_discovered,
            &context
        );

    /*
     * A callback rejection wins over a misleading true result from a buggy
     * enumerator. Once attachment failed, this discovery pass failed.
     */
    if (
        discovered &&
        !context.attach_failed
    ) {
        bus->enumeration_active =
            false;

        return
            BUS_ENUMERATION_RESULT_COMPLETED;
    }

    /*
     * Keep enumeration_active set throughout rollback. Driver remove()
     * callbacks invoked by detach must not recursively start another
     * enumeration pass against a partially unwound bus.
     */
    bool rollback_succeeded =
        bus_enumeration_rollback(
            bus,
            initial_device_count
        );

    bus->enumeration_active =
        false;

    if (!rollback_succeeded) {
        return
            BUS_ENUMERATION_RESULT_ROLLBACK_FAILED;
    }

    if (context.attach_failed) {
        return
            BUS_ENUMERATION_RESULT_ATTACH_FAILED;
    }

    return
        BUS_ENUMERATION_RESULT_DISCOVERY_FAILED;
}

enum bus_driver_register_result bus_register_driver(
    struct bus *bus,
    struct driver *driver)
{
    if (
        bus == NULL ||
        bus->registry == NULL ||
        !bus_driver_valid(
            driver
        )
    ) {
        return
            BUS_DRIVER_REGISTER_RESULT_INVALID_ARGUMENT;
    }

    if (
        driver->bus != NULL
    ) {
        if (
            driver->bus == bus &&
            bus_find_driver(
                bus,
                driver
            ) != SIZE_MAX
        ) {
            return
                BUS_DRIVER_REGISTER_RESULT_ALREADY_REGISTERED;
        }

        return
            BUS_DRIVER_REGISTER_RESULT_INVALID_ARGUMENT;
    }

    if (
        bus_find_driver(
            bus,
            driver
        ) != SIZE_MAX
    ) {
        return
            BUS_DRIVER_REGISTER_RESULT_ALREADY_REGISTERED;
    }

    if (
        bus_find_driver_name(
            bus,
            driver
        ) != SIZE_MAX
    ) {
        return
            BUS_DRIVER_REGISTER_RESULT_DUPLICATE_NAME;
    }

    if (
        bus->driver_count >=
            BUS_DRIVER_CAPACITY
    ) {
        return
            BUS_DRIVER_REGISTER_RESULT_RESOURCE_EXHAUSTED;
    }

    bus->drivers[
        bus->driver_count
    ] = driver;

    ++bus->driver_count;

    driver->bus =
        bus;

    /*
     * Devices that arrived before this driver are offered only to the newly
     * registered driver. Existing drivers have already had their opportunity.
     */
    for (
        size_t index = 0;
        index < bus->device_count;
        ++index
    ) {
        struct bus_device_entry *entry =
            &bus->devices[index];

        if (
            entry->binding_state ==
                BUS_BINDING_STATE_UNBOUND
        ) {
            (void) bus_try_bind_driver(
                entry,
                driver
            );
        }
    }

    return
        BUS_DRIVER_REGISTER_RESULT_REGISTERED;
}

enum bus_driver_unregister_result bus_unregister_driver(
    struct bus *bus,
    struct driver *driver)
{
    if (
        bus == NULL ||
        driver == NULL
    ) {
        return
            BUS_DRIVER_UNREGISTER_RESULT_INVALID_ARGUMENT;
    }

    size_t driver_index =
        bus_find_driver(
            bus,
            driver
        );

    if (
        driver_index == SIZE_MAX ||
        driver->bus != bus
    ) {
        return
            BUS_DRIVER_UNREGISTER_RESULT_NOT_FOUND;
    }

    /*
     * Driver removal does not remove devices. Devices remain ACTIVE and
     * registry-visible but become UNBOUND.
     */
    for (
        size_t index = 0;
        index < bus->device_count;
        ++index
    ) {
        struct bus_device_entry *entry =
            &bus->devices[index];

        if (
            entry->driver ==
                driver &&
            entry->binding_state ==
                BUS_BINDING_STATE_BOUND
        ) {
            bus_unbind_entry(
                entry
            );
        }
    }

    for (
        size_t move = driver_index;
        move + 1U < bus->driver_count;
        ++move
    ) {
        bus->drivers[move] =
            bus->drivers[
                move + 1U
            ];
    }

    --bus->driver_count;

    bus->drivers[
        bus->driver_count
    ] = NULL;

    driver->bus =
        NULL;

    return
        BUS_DRIVER_UNREGISTER_RESULT_UNREGISTERED;
}

enum bus_device_attach_result bus_attach_device(
    struct bus *bus,
    struct device *device)
{
    if (
        bus == NULL ||
        bus->registry == NULL ||
        device == NULL ||
        device->reference_count == 0 ||
        device->state !=
            DEVICE_STATE_ACTIVE
    ) {
        return
            BUS_DEVICE_ATTACH_RESULT_INVALID_ARGUMENT;
    }

    if (
        bus_find_device(
            bus,
            device
        ) != SIZE_MAX
    ) {
        return
            BUS_DEVICE_ATTACH_RESULT_ALREADY_ATTACHED;
    }

    if (
        bus->device_count >=
            BUS_DEVICE_CAPACITY
    ) {
        return
            BUS_DEVICE_ATTACH_RESULT_RESOURCE_EXHAUSTED;
    }

    /*
     * The bus owns one reference independently of publication ownership.
     */
    if (!device_retain(
        device
    )) {
        return
            BUS_DEVICE_ATTACH_RESULT_INVALID_ARGUMENT;
    }

    enum device_registry_register_result registry_result =
        device_registry_register(
            bus->registry,
            device
        );

    if (
        registry_result !=
            DEVICE_REGISTRY_REGISTER_RESULT_REGISTERED
    ) {
        if (!device_release(
            device
        )) {
            return
                BUS_DEVICE_ATTACH_RESULT_REGISTRY_REJECTED;
        }

        return
            bus_attach_registry_result(
                registry_result
            );
    }

    struct bus_device_entry *entry =
        &bus->devices[
            bus->device_count
        ];

    entry->device =
        device;

    entry->driver =
        NULL;

    entry->binding_state =
        BUS_BINDING_STATE_UNBOUND;

    ++bus->device_count;

    bus_try_bind_registered_drivers(
        bus,
        entry
    );

    return
        BUS_DEVICE_ATTACH_RESULT_ATTACHED;
}

enum bus_device_detach_result bus_detach_device(
    struct bus *bus,
    struct device *device)
{
    if (
        bus == NULL ||
        bus->registry == NULL ||
        device == NULL
    ) {
        return
            BUS_DEVICE_DETACH_RESULT_INVALID_ARGUMENT;
    }

    size_t index =
        bus_find_device(
            bus,
            device
        );

    if (index == SIZE_MAX) {
        return
            BUS_DEVICE_DETACH_RESULT_NOT_FOUND;
    }

    struct bus_device_entry *entry =
        &bus->devices[index];

    if (
        entry->binding_state ==
            BUS_BINDING_STATE_PROBING ||
        entry->binding_state ==
            BUS_BINDING_STATE_UNBINDING
    ) {
        return
            BUS_DEVICE_DETACH_RESULT_INVALID_ARGUMENT;
    }

    struct device *detached =
        NULL;

    enum device_registry_unregister_result registry_result =
        device_registry_unregister(
            bus->registry,
            device,
            &detached
        );

    switch (registry_result) {
        case DEVICE_REGISTRY_UNREGISTER_RESULT_UNREGISTERED:
            break;

        case DEVICE_REGISTRY_UNREGISTER_RESULT_REMOVAL_BLOCKED:
            return
                BUS_DEVICE_DETACH_RESULT_REMOVAL_BLOCKED;

        case DEVICE_REGISTRY_UNREGISTER_RESULT_INVALID_ARGUMENT:
        case DEVICE_REGISTRY_UNREGISTER_RESULT_NOT_FOUND:
            return
                BUS_DEVICE_DETACH_RESULT_REGISTRY_REJECTED;
    }

    /*
     * New discovery references are impossible from this point onward because
     * registry visibility has already been withdrawn.
     */
    if (
        entry->binding_state ==
            BUS_BINDING_STATE_BOUND
    ) {
        bus_unbind_entry(
            entry
        );
    }

    if (!device_finish_removal(
        device
    )) {
        /*
         * device_registry_unregister() established REMOVING and transferred
         * its reference. Keep the bus-owned reference intact so this
         * impossible core-invariant failure cannot free the object.
         */
        (void) device_release(
            detached
        );

        return
            BUS_DEVICE_DETACH_RESULT_LIFETIME_ERROR;
    }

    /*
     * Remove bus membership before releasing either ownership reference.
     */
    for (
        size_t move = index;
        move + 1U < bus->device_count;
        ++move
    ) {
        bus->devices[move] =
            bus->devices[
                move + 1U
            ];
    }

    --bus->device_count;

    bus->devices[
        bus->device_count
    ].device = NULL;

    bus->devices[
        bus->device_count
    ].driver = NULL;

    bus->devices[
        bus->device_count
    ].binding_state =
        BUS_BINDING_STATE_UNBOUND;

    /*
     * The transferred registry reference is released first. The bus reference
     * remains alive while that happens, so detached cannot disappear here.
     */
    if (!device_release(
        detached
    )) {
        return
            BUS_DEVICE_DETACH_RESULT_LIFETIME_ERROR;
    }

    /*
     * This release may be the final object reference. Do not dereference
     * device afterward.
     */
    if (!device_release(
        device
    )) {
        return
            BUS_DEVICE_DETACH_RESULT_LIFETIME_ERROR;
    }

    return
        BUS_DEVICE_DETACH_RESULT_DETACHED;
}

// Private functions and helpers implementations
static bool bus_name_valid(
    const char *name,
    size_t name_length)
{
    if (
        name == NULL ||
        name_length == 0 ||
        name_length >
            BUS_NAME_MAX
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

static bool bus_names_equal(
    const char *left,
    size_t left_length,
    const char *right,
    size_t right_length)
{
    if (
        left == NULL ||
        right == NULL ||
        left_length !=
            right_length
    ) {
        return false;
    }

    for (
        size_t index = 0;
        index < left_length;
        ++index
    ) {
        if (
            left[index] !=
                right[index]
        ) {
            return false;
        }
    }

    return true;
}

static bool bus_driver_valid(
    const struct driver *driver)
{
    return
        driver != NULL &&
        driver->name_length != 0 &&
        driver->name_length <=
            DRIVER_NAME_MAX &&
        driver->operations != NULL &&
        driver->operations->match != NULL &&
        driver->operations->probe != NULL &&
        driver->operations->remove != NULL;
}

static size_t bus_find_device(
    const struct bus *bus,
    const struct device *device)
{
    if (
        bus == NULL ||
        device == NULL
    ) {
        return SIZE_MAX;
    }

    for (
        size_t index = 0;
        index < bus->device_count;
        ++index
    ) {
        if (
            bus->devices[index].device ==
                device
        ) {
            return index;
        }
    }

    return SIZE_MAX;
}

static size_t bus_find_driver(
    const struct bus *bus,
    const struct driver *driver)
{
    if (
        bus == NULL ||
        driver == NULL
    ) {
        return SIZE_MAX;
    }

    for (
        size_t index = 0;
        index < bus->driver_count;
        ++index
    ) {
        if (
            bus->drivers[index] ==
                driver
        ) {
            return index;
        }
    }

    return SIZE_MAX;
}

static size_t bus_find_driver_name(
    const struct bus *bus,
    const struct driver *driver)
{
    if (
        bus == NULL ||
        driver == NULL
    ) {
        return SIZE_MAX;
    }

    for (
        size_t index = 0;
        index < bus->driver_count;
        ++index
    ) {
        struct driver *registered =
            bus->drivers[index];

        if (
            registered != NULL &&
            bus_names_equal(
                registered->name,
                registered->name_length,
                driver->name,
                driver->name_length
            )
        ) {
            return index;
        }
    }

    return SIZE_MAX;
}

static bool bus_try_bind_driver(
    struct bus_device_entry *entry,
    struct driver *driver)
{
    if (
        entry == NULL ||
        entry->device == NULL ||
        driver == NULL ||
        entry->binding_state !=
            BUS_BINDING_STATE_UNBOUND ||
        entry->driver != NULL ||
        entry->device->state !=
            DEVICE_STATE_ACTIVE ||
        !bus_driver_valid(
            driver
        )
    ) {
        return false;
    }

    if (!driver->operations->match(
        driver,
        entry->device
    )) {
        return false;
    }

    entry->binding_state =
        BUS_BINDING_STATE_PROBING;

    if (!driver->operations->probe(
        driver,
        entry->device
    )) {
        /*
         * Probe owns rollback of any partial driver-specific resources.
         */
        entry->binding_state =
            BUS_BINDING_STATE_UNBOUND;

        return false;
    }

    entry->driver =
        driver;

    entry->binding_state =
        BUS_BINDING_STATE_BOUND;

    return true;
}

static void bus_try_bind_registered_drivers(
    struct bus *bus,
    struct bus_device_entry *entry)
{
    if (
        bus == NULL ||
        entry == NULL
    ) {
        return;
    }

    for (
        size_t index = 0;
        index < bus->driver_count;
        ++index
    ) {
        if (bus_try_bind_driver(
            entry,
            bus->drivers[index]
        )) {
            return;
        }
    }
}

static void bus_unbind_entry(
    struct bus_device_entry *entry)
{
    if (
        entry == NULL ||
        entry->device == NULL ||
        entry->driver == NULL ||
        entry->binding_state !=
            BUS_BINDING_STATE_BOUND
    ) {
        return;
    }

    struct driver *driver =
        entry->driver;

    entry->binding_state =
        BUS_BINDING_STATE_UNBINDING;

    driver->operations->remove(
        driver,
        entry->device
    );

    entry->driver =
        NULL;

    entry->binding_state =
        BUS_BINDING_STATE_UNBOUND;
}

static enum bus_device_attach_result bus_attach_registry_result(
    enum device_registry_register_result result)
{
    switch (result) {
        case DEVICE_REGISTRY_REGISTER_RESULT_REGISTERED:
            return
                BUS_DEVICE_ATTACH_RESULT_ATTACHED;

        case DEVICE_REGISTRY_REGISTER_RESULT_INVALID_ARGUMENT:
            return
                BUS_DEVICE_ATTACH_RESULT_REGISTRY_REJECTED;

        case DEVICE_REGISTRY_REGISTER_RESULT_ALREADY_REGISTERED:
            return
                BUS_DEVICE_ATTACH_RESULT_ALREADY_PUBLISHED;

        case DEVICE_REGISTRY_REGISTER_RESULT_DUPLICATE_IDENTIFIER:
            return
                BUS_DEVICE_ATTACH_RESULT_DUPLICATE_IDENTIFIER;

        case DEVICE_REGISTRY_REGISTER_RESULT_DUPLICATE_NAME:
            return
                BUS_DEVICE_ATTACH_RESULT_DUPLICATE_NAME;

        case DEVICE_REGISTRY_REGISTER_RESULT_RESOURCE_EXHAUSTED:
            return
                BUS_DEVICE_ATTACH_RESULT_RESOURCE_EXHAUSTED;
    }

    return
        BUS_DEVICE_ATTACH_RESULT_REGISTRY_REJECTED;
}

static bool bus_enumeration_discovered(
    struct device *device,
    void *context)
{
    if (
        context == NULL
    ) {
        return false;
    }

    struct bus_enumeration_context *enumeration =
        context;

    if (
        enumeration->bus == NULL ||
        enumeration->attach_failed
    ) {
        return false;
    }

    enum bus_device_attach_result result =
        bus_attach_device(
            enumeration->bus,
            device
        );

    if (
        result !=
            BUS_DEVICE_ATTACH_RESULT_ATTACHED
    ) {
        enumeration->attach_failed =
            true;

        return false;
    }

    return true;
}

static bool bus_enumeration_rollback(
    struct bus *bus,
    size_t initial_device_count)
{
    if (
        bus == NULL ||
        initial_device_count >
            bus->device_count
    ) {
        return false;
    }

    /*
     * Detach in reverse discovery order.
     *
     * Besides preserving deterministic rollback, this is the correct ordering
     * for future parent-before-child discovery: children discovered later are
     * removed before their parents.
     */
    while (
        bus->device_count >
            initial_device_count
    ) {
        struct device *device =
            bus->devices[
                bus->device_count - 1U
            ].device;

        if (
            device == NULL ||
            bus_detach_device(
                bus,
                device
            ) !=
                BUS_DEVICE_DETACH_RESULT_DETACHED
        ) {
            return false;
        }
    }

    return true;
}
