// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file bus_test.c
 * @brief Generic bus and driver-binding regressions.
 */

#include "bus_test.h"

#include "../core/device/bus.h"
#include "../core/device/device.h"
#include "../core/device/driver.h"
#include "../core/device/registry.h"
#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct bus_test_driver_state {
    uint64_t match_identifier;

    size_t match_count;
    size_t probe_count;
    size_t remove_count;

    bool probe_success;
};

// Private functions and helpers declarations
static bool bus_test_driver_match(
    const struct driver *driver,
    const struct device *device
);

static bool bus_test_driver_probe(
    struct driver *driver,
    struct device *device
);

static void bus_test_driver_remove(
    struct driver *driver,
    struct device *device
);

static void bus_test_driver_before_device(void);

static void bus_test_device_before_driver(void);

static void bus_test_probe_fallback(void);

static void bus_test_blocked_detach_preserves_binding(void);

static void bus_test_driver_single_bus_membership(void);

// Public functions implementations
void bus_test_run(void)
{
    bus_test_driver_before_device();
    bus_test_device_before_driver();
    bus_test_probe_fallback();
    bus_test_blocked_detach_preserves_binding();
    bus_test_driver_single_bus_membership();

    diagnostics_write(
        "[device] Bus membership and driver binding tests passed\n"
    );
}

// Private functions and helpers implementations
static bool bus_test_driver_match(
    const struct driver *driver,
    const struct device *device)
{
    if (
        driver == NULL ||
        device == NULL ||
        driver->private_data == NULL
    ) {
        kernel_panic(
            "Bus test match callback received invalid state"
        );
    }

    struct bus_test_driver_state *state =
        driver->private_data;

    ++state->match_count;

    return
        device->identifier ==
            state->match_identifier;
}

static bool bus_test_driver_probe(
    struct driver *driver,
    struct device *device)
{
    if (
        driver == NULL ||
        device == NULL ||
        driver->private_data == NULL
    ) {
        kernel_panic(
            "Bus test probe callback received invalid state"
        );
    }

    struct bus_test_driver_state *state =
        driver->private_data;

    ++state->probe_count;

    if (!state->probe_success) {
        /*
         * Failed probes must not leave driver ownership behind.
         */
        return false;
    }

    if (
        device->private_data != NULL
    ) {
        kernel_panic(
            "Bus test probe found stale device-private state"
        );
    }

    device->private_data =
        driver;

    return true;
}

static void bus_test_driver_remove(
    struct driver *driver,
    struct device *device)
{
    if (
        driver == NULL ||
        device == NULL ||
        driver->private_data == NULL ||
        device->private_data !=
            driver
    ) {
        kernel_panic(
            "Bus test remove callback observed invalid binding"
        );
    }

    struct bus_test_driver_state *state =
        driver->private_data;

    ++state->remove_count;

    device->private_data =
        NULL;
}

static const struct driver_operations bus_test_driver_operations = {
    .match =
        bus_test_driver_match,
    .probe =
        bus_test_driver_probe,
    .remove =
        bus_test_driver_remove,
};

static void bus_test_driver_before_device(void)
{
    struct device_registry registry;

    struct bus bus;

    struct bus_test_driver_state state = {
        .match_identifier = 5000,
        .match_count = 0,
        .probe_count = 0,
        .remove_count = 0,
        .probe_success = true,
    };

    struct driver driver;

    struct device device;

    if (
        !device_registry_initialize(
            &registry
        ) ||
        !bus_initialize(
            &bus,
            "mock-bus",
            sizeof("mock-bus") - 1U,
            &registry,
            NULL,
            NULL
        ) ||
        !driver_initialize(
            &driver,
            "mock-driver",
            sizeof("mock-driver") - 1U,
            &bus_test_driver_operations,
            &state
        ) ||
        bus_register_driver(
            &bus,
            &driver
        ) !=
            BUS_DRIVER_REGISTER_RESULT_REGISTERED ||
        !device_initialize(
            &device,
            5000,
            "mock-device",
            sizeof("mock-device") - 1U,
            DEVICE_KIND_VIRTUAL,
            NULL,
            NULL
        )
    ) {
        kernel_panic(
            "Driver-before-device fixture initialization failed"
        );
    }

    if (
        bus_attach_device(
            &bus,
            &device
        ) !=
            BUS_DEVICE_ATTACH_RESULT_ATTACHED ||
        bus.device_count != 1 ||
        bus.devices[0].device !=
            &device ||
        bus.devices[0].driver !=
            &driver ||
        bus.devices[0].binding_state !=
            BUS_BINDING_STATE_BOUND ||
        device.reference_count != 3 ||
        device.private_data !=
            &driver ||
        state.match_count != 1 ||
        state.probe_count != 1 ||
        state.remove_count != 0
    ) {
        kernel_panic(
            "Driver-before-device binding failed"
        );
    }

    struct device *lookup =
        NULL;

    if (
        device_registry_lookup_identifier(
            &registry,
            5000,
            &lookup
        ) !=
            DEVICE_REGISTRY_LOOKUP_RESULT_FOUND ||
        lookup !=
            &device ||
        device.reference_count != 4
    ) {
        kernel_panic(
            "Bound bus device was not registry-visible"
        );
    }

    if (
        !device_release(
            lookup
        ) ||
        device.reference_count != 3
    ) {
        kernel_panic(
            "Bound bus device lookup release failed"
        );
    }

    if (
        bus_detach_device(
            &bus,
            &device
        ) !=
            BUS_DEVICE_DETACH_RESULT_DETACHED ||
        bus.device_count != 0 ||
        device.state !=
            DEVICE_STATE_GONE ||
        device.reference_count != 1 ||
        device.private_data !=
            NULL ||
        state.remove_count != 1
    ) {
        kernel_panic(
            "Bound bus device detach failed"
        );
    }

    if (
        !device_release(
            &device
        ) ||
        bus_unregister_driver(
            &bus,
            &driver
        ) !=
            BUS_DRIVER_UNREGISTER_RESULT_UNREGISTERED ||
        driver.bus != NULL
    ) {
        kernel_panic(
            "Driver-before-device fixture cleanup failed"
        );
    }
}

static void bus_test_device_before_driver(void)
{
    struct device_registry registry;

    struct bus bus;

    struct device device;

    struct bus_test_driver_state state = {
        .match_identifier = 5100,
        .match_count = 0,
        .probe_count = 0,
        .remove_count = 0,
        .probe_success = true,
    };

    struct driver driver;

    if (
        !device_registry_initialize(
            &registry
        ) ||
        !bus_initialize(
            &bus,
            "late-driver-bus",
            sizeof("late-driver-bus") - 1U,
            &registry,
            NULL,
            NULL
        ) ||
        !device_initialize(
            &device,
            5100,
            "early-device",
            sizeof("early-device") - 1U,
            DEVICE_KIND_PHYSICAL,
            NULL,
            NULL
        ) ||
        bus_attach_device(
            &bus,
            &device
        ) !=
            BUS_DEVICE_ATTACH_RESULT_ATTACHED
    ) {
        kernel_panic(
            "Device-before-driver fixture initialization failed"
        );
    }

    if (
        bus.devices[0].binding_state !=
            BUS_BINDING_STATE_UNBOUND ||
        bus.devices[0].driver !=
            NULL ||
        device.private_data !=
            NULL
    ) {
        kernel_panic(
            "Device bound without registered driver"
        );
    }

    if (
        !driver_initialize(
            &driver,
            "late-driver",
            sizeof("late-driver") - 1U,
            &bus_test_driver_operations,
            &state
        ) ||
        bus_register_driver(
            &bus,
            &driver
        ) !=
            BUS_DRIVER_REGISTER_RESULT_REGISTERED ||
        bus.devices[0].binding_state !=
            BUS_BINDING_STATE_BOUND ||
        bus.devices[0].driver !=
            &driver ||
        device.private_data !=
            &driver ||
        state.match_count != 1 ||
        state.probe_count != 1
    ) {
        kernel_panic(
            "Late driver failed to bind existing device"
        );
    }

    /*
     * Unregistering a driver unbinds but does not remove or unpublish the
     * device.
     */
    if (
        bus_unregister_driver(
            &bus,
            &driver
        ) !=
            BUS_DRIVER_UNREGISTER_RESULT_UNREGISTERED ||
        bus.devices[0].binding_state !=
            BUS_BINDING_STATE_UNBOUND ||
        bus.devices[0].driver !=
            NULL ||
        device.private_data !=
            NULL ||
        device.state !=
            DEVICE_STATE_ACTIVE ||
        registry.count != 1 ||
        state.remove_count != 1
    ) {
        kernel_panic(
            "Driver unregister did not leave device safely unbound"
        );
    }

    struct device *lookup =
        NULL;

    if (
        device_registry_lookup_identifier(
            &registry,
            5100,
            &lookup
        ) !=
            DEVICE_REGISTRY_LOOKUP_RESULT_FOUND ||
        lookup !=
            &device ||
        !device_release(
            lookup
        )
    ) {
        kernel_panic(
            "Unbound bus device disappeared from registry"
        );
    }

    if (
        bus_detach_device(
            &bus,
            &device
        ) !=
            BUS_DEVICE_DETACH_RESULT_DETACHED ||
        device.state !=
            DEVICE_STATE_GONE ||
        device.reference_count != 1 ||
        !device_release(
            &device
        )
    ) {
        kernel_panic(
            "Device-before-driver fixture cleanup failed"
        );
    }
}

static void bus_test_probe_fallback(void)
{
    struct device_registry registry;

    struct bus bus;

    struct bus_test_driver_state failing_state = {
        .match_identifier = 5200,
        .match_count = 0,
        .probe_count = 0,
        .remove_count = 0,
        .probe_success = false,
    };

    struct bus_test_driver_state succeeding_state = {
        .match_identifier = 5200,
        .match_count = 0,
        .probe_count = 0,
        .remove_count = 0,
        .probe_success = true,
    };

    struct driver failing_driver;
    struct driver succeeding_driver;

    struct device device;

    if (
        !device_registry_initialize(
            &registry
        ) ||
        !bus_initialize(
            &bus,
            "fallback-bus",
            sizeof("fallback-bus") - 1U,
            &registry,
            NULL,
            NULL
        ) ||
        !driver_initialize(
            &failing_driver,
            "failing-driver",
            sizeof("failing-driver") - 1U,
            &bus_test_driver_operations,
            &failing_state
        ) ||
        !driver_initialize(
            &succeeding_driver,
            "succeeding-driver",
            sizeof("succeeding-driver") - 1U,
            &bus_test_driver_operations,
            &succeeding_state
        ) ||
        bus_register_driver(
            &bus,
            &failing_driver
        ) !=
            BUS_DRIVER_REGISTER_RESULT_REGISTERED ||
        bus_register_driver(
            &bus,
            &succeeding_driver
        ) !=
            BUS_DRIVER_REGISTER_RESULT_REGISTERED ||
        !device_initialize(
            &device,
            5200,
            "fallback-device",
            sizeof("fallback-device") - 1U,
            DEVICE_KIND_PHYSICAL,
            NULL,
            NULL
        ) ||
        bus_attach_device(
            &bus,
            &device
        ) !=
            BUS_DEVICE_ATTACH_RESULT_ATTACHED
    ) {
        kernel_panic(
            "Probe-fallback fixture initialization failed"
        );
    }

    if (
        failing_state.match_count != 1 ||
        failing_state.probe_count != 1 ||
        failing_state.remove_count != 0 ||
        succeeding_state.match_count != 1 ||
        succeeding_state.probe_count != 1 ||
        succeeding_state.remove_count != 0 ||
        bus.devices[0].driver !=
            &succeeding_driver ||
        bus.devices[0].binding_state !=
            BUS_BINDING_STATE_BOUND ||
        device.private_data !=
            &succeeding_driver
    ) {
        kernel_panic(
            "Probe failure did not fall through cleanly"
        );
    }

    /*
     * Registering another matching driver must not probe an already-bound
     * device.
     */
    struct bus_test_driver_state ignored_state = {
        .match_identifier = 5200,
        .match_count = 0,
        .probe_count = 0,
        .remove_count = 0,
        .probe_success = true,
    };

    struct driver ignored_driver;

    if (
        !driver_initialize(
            &ignored_driver,
            "ignored-driver",
            sizeof("ignored-driver") - 1U,
            &bus_test_driver_operations,
            &ignored_state
        ) ||
        bus_register_driver(
            &bus,
            &ignored_driver
        ) !=
            BUS_DRIVER_REGISTER_RESULT_REGISTERED ||
        ignored_state.match_count != 0 ||
        ignored_state.probe_count != 0 ||
        bus.devices[0].driver !=
            &succeeding_driver
    ) {
        kernel_panic(
            "Already-bound device was probed again"
        );
    }

    if (
        bus_detach_device(
            &bus,
            &device
        ) !=
            BUS_DEVICE_DETACH_RESULT_DETACHED ||
        succeeding_state.remove_count != 1 ||
        device.reference_count != 1 ||
        !device_release(
            &device
        ) ||
        bus_unregister_driver(
            &bus,
            &ignored_driver
        ) !=
            BUS_DRIVER_UNREGISTER_RESULT_UNREGISTERED ||
        bus_unregister_driver(
            &bus,
            &succeeding_driver
        ) !=
            BUS_DRIVER_UNREGISTER_RESULT_UNREGISTERED ||
        bus_unregister_driver(
            &bus,
            &failing_driver
        ) !=
            BUS_DRIVER_UNREGISTER_RESULT_UNREGISTERED
    ) {
        kernel_panic(
            "Probe-fallback fixture cleanup failed"
        );
    }
}

static void bus_test_blocked_detach_preserves_binding(void)
{
    struct device_registry registry;

    struct bus bus;

    struct bus_test_driver_state state = {
        .match_identifier = 5300,
        .match_count = 0,
        .probe_count = 0,
        .remove_count = 0,
        .probe_success = true,
    };

    struct driver driver;

    struct device parent;
    struct device child;

    if (
        !device_registry_initialize(
            &registry
        ) ||
        !bus_initialize(
            &bus,
            "blocked-detach-bus",
            sizeof("blocked-detach-bus") - 1U,
            &registry,
            NULL,
            NULL
        ) ||
        !driver_initialize(
            &driver,
            "blocked-detach-driver",
            sizeof("blocked-detach-driver") - 1U,
            &bus_test_driver_operations,
            &state
        ) ||
        bus_register_driver(
            &bus,
            &driver
        ) !=
            BUS_DRIVER_REGISTER_RESULT_REGISTERED ||
        !device_initialize(
            &parent,
            5300,
            "parent-device",
            sizeof("parent-device") - 1U,
            DEVICE_KIND_PHYSICAL,
            NULL,
            NULL
        ) ||
        !device_initialize_child(
            &child,
            &parent,
            5301,
            "child-device",
            sizeof("child-device") - 1U,
            DEVICE_KIND_VIRTUAL,
            NULL,
            NULL
        ) ||
        bus_attach_device(
            &bus,
            &parent
        ) !=
            BUS_DEVICE_ATTACH_RESULT_ATTACHED
    ) {
        kernel_panic(
            "Blocked-detach fixture initialization failed"
        );
    }

    if (
        bus.devices[0].binding_state !=
            BUS_BINDING_STATE_BOUND ||
        bus.devices[0].driver !=
            &driver ||
        parent.private_data !=
            &driver ||
        state.match_count != 1 ||
        state.probe_count != 1 ||
        state.remove_count != 0
    ) {
        kernel_panic(
            "Blocked-detach fixture did not bind"
        );
    }

    /*
     * The ACTIVE child prevents device_begin_removal(parent). Since registry
     * withdrawal never succeeds, the bus must not invoke driver.remove() or
     * mutate binding state.
     */
    if (
        bus_detach_device(
            &bus,
            &parent
        ) !=
            BUS_DEVICE_DETACH_RESULT_REMOVAL_BLOCKED ||
        bus.device_count != 1 ||
        bus.devices[0].device !=
            &parent ||
        bus.devices[0].driver !=
            &driver ||
        bus.devices[0].binding_state !=
            BUS_BINDING_STATE_BOUND ||
        parent.state !=
            DEVICE_STATE_ACTIVE ||
        parent.private_data !=
            &driver ||
        registry.count != 1 ||
        state.remove_count != 0
    ) {
        kernel_panic(
            "Blocked detach mutated driver binding"
        );
    }

    /*
     * Registry discovery must also remain intact after the rejected detach.
     */
    struct device *lookup =
        NULL;

    if (
        device_registry_lookup_identifier(
            &registry,
            5300,
            &lookup
        ) !=
            DEVICE_REGISTRY_LOOKUP_RESULT_FOUND ||
        lookup !=
            &parent ||
        !device_release(
            lookup
        )
    ) {
        kernel_panic(
            "Blocked detach changed registry visibility"
        );
    }

    /*
     * Remove the topology obstruction. The child is not bus-published in this
     * fixture, so its ordinary device lifetime is completed directly.
     */
    if (
        !device_begin_removal(
            &child
        ) ||
        !device_finish_removal(
            &child
        ) ||
        !device_release(
            &child
        ) ||
        parent.first_child !=
            NULL
    ) {
        kernel_panic(
            "Blocked-detach child cleanup failed"
        );
    }

    /*
     * The same detach now succeeds and invokes remove exactly once.
     */
    if (
        bus_detach_device(
            &bus,
            &parent
        ) !=
            BUS_DEVICE_DETACH_RESULT_DETACHED ||
        state.remove_count != 1 ||
        parent.private_data !=
            NULL ||
        parent.state !=
            DEVICE_STATE_GONE ||
        parent.reference_count != 1 ||
        registry.count != 0 ||
        bus.device_count != 0
    ) {
        kernel_panic(
            "Detach failed after child removal"
        );
    }

    if (
        !device_release(
            &parent
        ) ||
        bus_unregister_driver(
            &bus,
            &driver
        ) !=
            BUS_DRIVER_UNREGISTER_RESULT_UNREGISTERED ||
        driver.bus != NULL
    ) {
        kernel_panic(
            "Blocked-detach fixture cleanup failed"
        );
    }
}

static void bus_test_driver_single_bus_membership(void)
{
    struct device_registry registry;

    struct bus first_bus;
    struct bus second_bus;

    struct bus_test_driver_state state = {
        .match_identifier = 5400,
        .match_count = 0,
        .probe_count = 0,
        .remove_count = 0,
        .probe_success = true,
    };

    struct driver driver;

    if (
        !device_registry_initialize(
            &registry
        ) ||
        !bus_initialize(
            &first_bus,
            "first-bus",
            sizeof("first-bus") - 1U,
            &registry,
            NULL,
            NULL
        ) ||
        !bus_initialize(
            &second_bus,
            "second-bus",
            sizeof("second-bus") - 1U,
            &registry,
            NULL,
            NULL
        ) ||
        !driver_initialize(
            &driver,
            "single-bus-driver",
            sizeof("single-bus-driver") - 1U,
            &bus_test_driver_operations,
            &state
        )
    ) {
        kernel_panic(
            "Single-bus driver fixture initialization failed"
        );
    }

    if (
        bus_register_driver(
            &first_bus,
            &driver
        ) !=
            BUS_DRIVER_REGISTER_RESULT_REGISTERED ||
        driver.bus !=
            &first_bus ||
        first_bus.driver_count != 1 ||
        first_bus.drivers[0] !=
            &driver
    ) {
        kernel_panic(
            "Driver first-bus registration failed"
        );
    }

    /*
     * One driver object represents one registration lifetime and therefore
     * cannot simultaneously belong to another bus.
     *
     * The rejected operation must leave both buses unchanged.
     */
    if (
        bus_register_driver(
            &second_bus,
            &driver
        ) !=
            BUS_DRIVER_REGISTER_RESULT_INVALID_ARGUMENT ||
        driver.bus !=
            &first_bus ||
        first_bus.driver_count != 1 ||
        first_bus.drivers[0] !=
            &driver ||
        second_bus.driver_count != 0 ||
        second_bus.drivers[0] !=
            NULL
    ) {
        kernel_panic(
            "Driver was accepted by two buses simultaneously"
        );
    }

    /*
     * Verify that the failed second registration did not damage the original
     * registration: the first bus must still be able to bind the driver.
     */
    struct device device;

    if (
        !device_initialize(
            &device,
            5400,
            "single-bus-device",
            sizeof("single-bus-device") - 1U,
            DEVICE_KIND_VIRTUAL,
            NULL,
            NULL
        ) ||
        bus_attach_device(
            &first_bus,
            &device
        ) !=
            BUS_DEVICE_ATTACH_RESULT_ATTACHED ||
        first_bus.devices[0].driver !=
            &driver ||
        first_bus.devices[0].binding_state !=
            BUS_BINDING_STATE_BOUND ||
        state.match_count != 1 ||
        state.probe_count != 1
    ) {
        kernel_panic(
            "Rejected second registration damaged original driver"
        );
    }

    if (
        bus_detach_device(
            &first_bus,
            &device
        ) !=
            BUS_DEVICE_DETACH_RESULT_DETACHED ||
        state.remove_count != 1 ||
        device.reference_count != 1 ||
        !device_release(
            &device
        ) ||
        bus_unregister_driver(
            &first_bus,
            &driver
        ) !=
            BUS_DRIVER_UNREGISTER_RESULT_UNREGISTERED ||
        driver.bus != NULL ||
        first_bus.driver_count != 0
    ) {
        kernel_panic(
            "Single-bus driver fixture cleanup failed"
        );
    }

    /*
     * After the first registration lifetime ends, the same caller-owned
     * driver object may begin a new registration lifetime on another bus.
     */
    if (
        bus_register_driver(
            &second_bus,
            &driver
        ) !=
            BUS_DRIVER_REGISTER_RESULT_REGISTERED ||
        driver.bus !=
            &second_bus ||
        second_bus.driver_count != 1 ||
        bus_unregister_driver(
            &second_bus,
            &driver
        ) !=
            BUS_DRIVER_UNREGISTER_RESULT_UNREGISTERED ||
        driver.bus != NULL
    ) {
        kernel_panic(
            "Driver could not begin a later bus registration lifetime"
        );
    }
}
