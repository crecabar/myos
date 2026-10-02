// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file bus.h
 * @brief Generic kernel bus membership and driver-binding contracts.
 *
 * A bus owns one device reference for every attached device. Publication in
 * the global device registry owns a separate reference.
 *
 * Drivers registered with a bus are non-owning references to caller-owned
 * driver objects.
 *
 * Binding is synchronous and deterministic:
 *
 * - attached devices are offered to registered drivers in registration order;
 * - only unbound devices are probed;
 * - a failed probe must unwind its own partial ownership;
 * - probing continues with later matching drivers after one probe fails;
 * - the first successful probe becomes the bound driver;
 * - an already-bound device is never probed by another driver.
 */

#ifndef MYOS_CORE_DEVICE_BUS_H
#define MYOS_CORE_DEVICE_BUS_H

#include "device.h"
#include "driver.h"
#include "registry.h"

#include <stdbool.h>
#include <stddef.h>

#define BUS_NAME_MAX 63U

#define BUS_DEVICE_CAPACITY 128U
#define BUS_DRIVER_CAPACITY 32U

enum bus_binding_state {
    BUS_BINDING_STATE_UNBOUND,
    BUS_BINDING_STATE_PROBING,
    BUS_BINDING_STATE_BOUND,
    BUS_BINDING_STATE_UNBINDING,
};

/*
 * Prepared here for #235.2. A bus-specific enumerator will use this callback
 * boundary to hand discovered devices to the generic bus core.
 */
typedef bool (*bus_device_discovered_callback)(
    struct device *device,
    void *context
);

struct bus;

struct bus_operations {
    /**
     * Enumerates devices currently discoverable through this bus.
     *
     * For each newly discovered caller-owned ACTIVE device, invoke discovered.
     *
     * discovered() returning true means generic bus attachment and registry
     * publication succeeded. The enumerator still owns its original discovery
     * reference and may release that reference after the callback returns.
     *
     * discovered() returning false means the device was not accepted by the
     * generic bus core. The enumerator remains responsible for unwinding and
     * releasing that device.
     *
     * The enumerator must stop and return false when discovered() rejects a
     * device. Returning false for an independent bus-specific discovery error
     * also causes generic rollback of devices accepted earlier in this
     * enumeration call.
     */
    bool (*enumerate)(
        struct bus *bus,
        bus_device_discovered_callback discovered,
        void *context
    );
};

struct bus_device_entry {
    struct device *device;

    struct driver *driver;

    enum bus_binding_state binding_state;
};

struct bus {
    char name[
        BUS_NAME_MAX + 1U
    ];

    size_t name_length;

    /*
     * Borrowed. The registry must outlive the bus.
     */
    struct device_registry *registry;

    const struct bus_operations *operations;

    void *private_data;

    /*
     * Prevents recursive enumeration through probe/remove callbacks.
     *
     * The initial implementation is synchronous and single-CPU. A future SMP
     * implementation will require synchronization around bus mutation.
     */
    bool enumeration_active;

    struct bus_device_entry devices[
        BUS_DEVICE_CAPACITY
    ];

    size_t device_count;

    /*
     * Driver pointers are borrowed. Each registered driver must remain alive
     * until bus_unregister_driver() completes.
     */
    struct driver *drivers[
        BUS_DRIVER_CAPACITY
    ];

    size_t driver_count;
};

enum bus_driver_register_result {
    BUS_DRIVER_REGISTER_RESULT_REGISTERED,
    BUS_DRIVER_REGISTER_RESULT_INVALID_ARGUMENT,
    BUS_DRIVER_REGISTER_RESULT_ALREADY_REGISTERED,
    BUS_DRIVER_REGISTER_RESULT_DUPLICATE_NAME,
    BUS_DRIVER_REGISTER_RESULT_RESOURCE_EXHAUSTED,
};

enum bus_driver_unregister_result {
    BUS_DRIVER_UNREGISTER_RESULT_UNREGISTERED,
    BUS_DRIVER_UNREGISTER_RESULT_INVALID_ARGUMENT,
    BUS_DRIVER_UNREGISTER_RESULT_NOT_FOUND,
};

enum bus_device_attach_result {
    BUS_DEVICE_ATTACH_RESULT_ATTACHED,
    BUS_DEVICE_ATTACH_RESULT_INVALID_ARGUMENT,
    BUS_DEVICE_ATTACH_RESULT_ALREADY_ATTACHED,
    BUS_DEVICE_ATTACH_RESULT_ALREADY_PUBLISHED,
    BUS_DEVICE_ATTACH_RESULT_DUPLICATE_IDENTIFIER,
    BUS_DEVICE_ATTACH_RESULT_DUPLICATE_NAME,
    BUS_DEVICE_ATTACH_RESULT_RESOURCE_EXHAUSTED,
    BUS_DEVICE_ATTACH_RESULT_REGISTRY_REJECTED,
};

enum bus_device_detach_result {
    BUS_DEVICE_DETACH_RESULT_DETACHED,
    BUS_DEVICE_DETACH_RESULT_INVALID_ARGUMENT,
    BUS_DEVICE_DETACH_RESULT_NOT_FOUND,
    BUS_DEVICE_DETACH_RESULT_REMOVAL_BLOCKED,
    BUS_DEVICE_DETACH_RESULT_REGISTRY_REJECTED,
    BUS_DEVICE_DETACH_RESULT_LIFETIME_ERROR,
};

enum bus_enumeration_result {
    BUS_ENUMERATION_RESULT_COMPLETED,
    BUS_ENUMERATION_RESULT_INVALID_ARGUMENT,
    BUS_ENUMERATION_RESULT_NOT_SUPPORTED,
    BUS_ENUMERATION_RESULT_DISCOVERY_FAILED,
    BUS_ENUMERATION_RESULT_ATTACH_FAILED,
    BUS_ENUMERATION_RESULT_ROLLBACK_FAILED,
};

bool bus_initialize(
    struct bus *bus,
    const char *name,
    size_t name_length,
    struct device_registry *registry,
    const struct bus_operations *operations,
    void *private_data
);

/**
 * Runs one synchronous bus discovery pass.
 *
 * Devices accepted during this invocation are attached, published, and
 * offered to registered drivers through the ordinary bus_attach_device()
 * path.
 *
 * If either the bus-specific enumerator or one discovered-device attachment
 * fails, every device successfully added by this invocation is detached in
 * reverse order. Devices that existed before the enumeration began are never
 * part of that rollback.
 *
 * The bus-specific enumerator owns each device's original discovery reference.
 * The generic callback never consumes that reference.
 */
enum bus_enumeration_result bus_enumerate(
    struct bus *bus
);

/**
 * Registers one caller-owned driver with bus.
 *
 * Existing unbound devices are immediately offered to this driver.
 * Probe failure does not fail driver registration.
 */
enum bus_driver_register_result bus_register_driver(
    struct bus *bus,
    struct driver *driver
);

/**
 * Unregisters one driver.
 *
 * Every device currently bound to driver is synchronously remove()d and left
 * ACTIVE, published, and UNBOUND.
 */
enum bus_driver_unregister_result bus_unregister_driver(
    struct bus *bus,
    struct driver *driver
);

/**
 * Attaches and publishes one ACTIVE device.
 *
 * On success:
 *
 * - bus owns one additional device reference;
 * - registry owns one additional device reference;
 * - device is offered to registered drivers;
 * - caller retains its pre-existing ownership.
 *
 * The caller may release its own reference after successful attachment if
 * another owner no longer needs it.
 */
enum bus_device_attach_result bus_attach_device(
    struct bus *bus,
    struct device *device
);

/**
 * Withdraws and removes one bus device.
 *
 * Visibility is removed first through device_registry_unregister(), placing
 * the device in REMOVING state. A bound driver is then remove()d, removal is
 * completed to GONE, and both registry-transferred and bus-owned references
 * are released.
 *
 * Existing references acquired before detach remain valid until released.
 */
enum bus_device_detach_result bus_detach_device(
    struct bus *bus,
    struct device *device
);

#endif
