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

#define BUS_TEST_REBIND_CYCLES 8U

#define BUS_TEST_DRIVER_CAPACITY_NAME_LENGTH 9U
#define BUS_TEST_DRIVER_CAPACITY_NAME_SIZE 10U

struct bus_test_driver_state {
    uint64_t match_identifier;

    size_t match_count;
    size_t probe_count;
    size_t remove_count;

    bool probe_success;
};

struct bus_test_enumeration_state {
    struct device **devices;

    size_t device_count;

    size_t callback_count;

    bool fail_after_enabled;

    size_t fail_after;

    /*
     * Test-only behavior for a deliberately broken enumerator that keeps
     * discovering devices after the generic callback rejects one.
     */
    bool continue_after_rejection;
};

static struct driver bus_test_capacity_drivers[
    BUS_DRIVER_CAPACITY + 1U
];

static struct bus_test_driver_state bus_test_capacity_driver_states[
    BUS_DRIVER_CAPACITY + 1U
];

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

static bool bus_test_enumerate(
    struct bus *bus,
    bus_device_discovered_callback discovered,
    void *context
);

static void bus_test_enumeration_success(void);

static void bus_test_enumeration_discovery_failure(void);

static void bus_test_enumeration_attach_failure(void);

static void bus_test_enumeration_rejection_latches_failure(void);

static void bus_test_driver_capacity_name(
    size_t index,
    char name[
        BUS_TEST_DRIVER_CAPACITY_NAME_SIZE
    ]
);

static void bus_test_duplicate_driver_name(void);

static void bus_test_driver_capacity(void);

static void bus_test_repeated_binding(void);

static void bus_test_parent_child_enumeration_rollback(void);

static void bus_test_invalid_structure(void);

// Public functions implementations
void bus_test_run(void)
{
    bus_test_driver_before_device();
    bus_test_device_before_driver();
    bus_test_probe_fallback();
    bus_test_blocked_detach_preserves_binding();
    bus_test_driver_single_bus_membership();
    bus_test_enumeration_success();
    bus_test_enumeration_discovery_failure();
    bus_test_enumeration_attach_failure();
    bus_test_enumeration_rejection_latches_failure();
    bus_test_duplicate_driver_name();
    bus_test_driver_capacity();
    bus_test_repeated_binding();
    bus_test_parent_child_enumeration_rollback();
    bus_test_invalid_structure();

    diagnostics_write(
        "[device] Bus discovery, membership and driver binding tests passed\n"
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

static bool bus_test_enumerate(
    struct bus *bus,
    bus_device_discovered_callback discovered,
    void *context)
{
    if (
        bus == NULL ||
        discovered == NULL ||
        context == NULL ||
        bus->private_data == NULL
    ) {
        kernel_panic(
            "Mock bus enumerator received invalid state"
        );
    }

    struct bus_test_enumeration_state *state =
        bus->private_data;

    for (
        size_t index = 0;
        index < state->device_count;
        ++index
    ) {
        struct device *device =
            state->devices[index];

        if (device == NULL) {
            kernel_panic(
                "Mock bus enumerator contains NULL device"
            );
        }

        ++state->callback_count;

        if (!discovered(
            device,
            context
        )) {
            /*
             * The generic bus core rejected this device and therefore owns no
             * persistent reference to it. Dispose of the discovery-owned
             * lifetime before either stopping or deliberately continuing this
             * test enumerator.
             */
            if (
                device->state ==
                    DEVICE_STATE_ACTIVE &&
                device->reference_count ==
                    1
            ) {
                if (
                    !device_begin_removal(
                        device
                    ) ||
                    !device_finish_removal(
                        device
                    ) ||
                    !device_release(
                        device
                    )
                ) {
                    kernel_panic(
                        "Mock enumerator rejected-device cleanup failed"
                    );
                }
            }

            if (!state->continue_after_rejection) {
                return false;
            }

            /*
             * Deliberately violate the enumeration contract for regression
             * coverage. The generic bus core must reject every later callback
             * after the first attach failure.
             */
            continue;
        }

        /*
         * bus_attach_device() established independent bus and registry
         * ownership. The mock enumerator no longer needs its discovery
         * reference.
         */
        if (!device_release(
            device
        )) {
            kernel_panic(
                "Mock enumerator discovery-reference release failed"
            );
        }

        if (
            state->fail_after_enabled &&
            state->callback_count ==
                state->fail_after
        ) {
            return false;
        }
    }

    return true;
}

static const struct bus_operations bus_test_operations = {
    .enumerate =
        bus_test_enumerate,
};

static void bus_test_enumeration_success(void)
{
    struct device_registry registry;

    struct device first;
    struct device second;
    struct device third;

    if (
        !device_initialize(
            &first,
            5500,
            "enum-first",
            sizeof("enum-first") - 1U,
            DEVICE_KIND_PHYSICAL,
            NULL,
            NULL
        ) ||
        !device_initialize(
            &second,
            5501,
            "enum-second",
            sizeof("enum-second") - 1U,
            DEVICE_KIND_PHYSICAL,
            NULL,
            NULL
        ) ||
        !device_initialize(
            &third,
            5502,
            "enum-third",
            sizeof("enum-third") - 1U,
            DEVICE_KIND_PHYSICAL,
            NULL,
            NULL
        )
    ) {
        kernel_panic(
            "Successful enumeration device initialization failed"
        );
    }

    struct device *devices[] = {
        &first,
        &second,
        &third,
    };

    struct bus_test_enumeration_state enumeration = {
        .devices =
            devices,
        .device_count =
            3,
        .callback_count =
            0,
        .fail_after_enabled =
            false,
        .fail_after =
            0,
    };

    struct bus bus;

    struct bus_test_driver_state driver_state = {
        .match_identifier = 5501,
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
            "enumeration-bus",
            sizeof("enumeration-bus") - 1U,
            &registry,
            &bus_test_operations,
            &enumeration
        ) ||
        !driver_initialize(
            &driver,
            "enumeration-driver",
            sizeof("enumeration-driver") - 1U,
            &bus_test_driver_operations,
            &driver_state
        ) ||
        bus_register_driver(
            &bus,
            &driver
        ) !=
            BUS_DRIVER_REGISTER_RESULT_REGISTERED
    ) {
        kernel_panic(
            "Successful enumeration fixture initialization failed"
        );
    }

    if (
        bus_enumerate(
            &bus
        ) !=
            BUS_ENUMERATION_RESULT_COMPLETED ||
        enumeration.callback_count != 3 ||
        bus.enumeration_active ||
        bus.device_count != 3 ||
        registry.count != 3 ||
        first.reference_count != 2 ||
        second.reference_count != 2 ||
        third.reference_count != 2 ||
        bus.devices[0].device !=
            &first ||
        bus.devices[1].device !=
            &second ||
        bus.devices[2].device !=
            &third ||
        bus.devices[0].binding_state !=
            BUS_BINDING_STATE_UNBOUND ||
        bus.devices[1].binding_state !=
            BUS_BINDING_STATE_BOUND ||
        bus.devices[1].driver !=
            &driver ||
        bus.devices[2].binding_state !=
            BUS_BINDING_STATE_UNBOUND ||
        driver_state.match_count != 3 ||
        driver_state.probe_count != 1 ||
        driver_state.remove_count != 0
    ) {
        kernel_panic(
            "Successful bus enumeration contract failed"
        );
    }

    /*
     * The discovery references were released by the enumerator. Bus and
     * registry are now the only owners of each device.
     */
    for (
        size_t remaining = 3;
        remaining != 0;
        --remaining
    ) {
        struct device *device =
            devices[
                remaining - 1U
            ];

        if (
            bus_detach_device(
                &bus,
                device
            ) !=
                BUS_DEVICE_DETACH_RESULT_DETACHED ||
            device->state !=
                DEVICE_STATE_GONE ||
            device->reference_count != 0
        ) {
            kernel_panic(
                "Successful enumeration cleanup failed"
            );
        }
    }

    if (
        registry.count != 0 ||
        bus.device_count != 0 ||
        driver_state.remove_count != 1 ||
        bus_unregister_driver(
            &bus,
            &driver
        ) !=
            BUS_DRIVER_UNREGISTER_RESULT_UNREGISTERED
    ) {
        kernel_panic(
            "Successful enumeration final cleanup failed"
        );
    }
}

static void bus_test_enumeration_discovery_failure(void)
{
    struct device_registry registry;

    struct device first;
    struct device second;

    if (
        !device_initialize(
            &first,
            5600,
            "partial-first",
            sizeof("partial-first") - 1U,
            DEVICE_KIND_PHYSICAL,
            NULL,
            NULL
        ) ||
        !device_initialize(
            &second,
            5601,
            "partial-second",
            sizeof("partial-second") - 1U,
            DEVICE_KIND_PHYSICAL,
            NULL,
            NULL
        )
    ) {
        kernel_panic(
            "Partial enumeration device initialization failed"
        );
    }

    struct device *devices[] = {
        &first,
        &second,
    };

    struct bus_test_enumeration_state enumeration = {
        .devices =
            devices,
        .device_count =
            2,
        .callback_count =
            0,
        .fail_after_enabled =
            true,
        .fail_after =
            2,
    };

    struct bus bus;

    struct bus_test_driver_state driver_state = {
        .match_identifier = 5600,
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
            "partial-bus",
            sizeof("partial-bus") - 1U,
            &registry,
            &bus_test_operations,
            &enumeration
        ) ||
        !driver_initialize(
            &driver,
            "partial-driver",
            sizeof("partial-driver") - 1U,
            &bus_test_driver_operations,
            &driver_state
        ) ||
        bus_register_driver(
            &bus,
            &driver
        ) !=
            BUS_DRIVER_REGISTER_RESULT_REGISTERED
    ) {
        kernel_panic(
            "Partial enumeration fixture initialization failed"
        );
    }

    struct device preexisting;

    if (
        !device_initialize(
            &preexisting,
            5599,
            "preexisting",
            sizeof("preexisting") - 1U,
            DEVICE_KIND_VIRTUAL,
            NULL,
            NULL
        ) ||
        bus_attach_device(
            &bus,
            &preexisting
        ) !=
            BUS_DEVICE_ATTACH_RESULT_ATTACHED
    ) {
        kernel_panic(
            "Partial enumeration preexisting device setup failed"
        );
    }

    /*
     * preexisting was legitimately offered to the already-registered driver.
     * It does not match, so no probe occurred. Reset the match counter here so
     * the assertions below describe only the enumeration transaction under
     * test.
     */
    if (
        driver_state.match_count != 1 ||
        driver_state.probe_count != 0 ||
        driver_state.remove_count != 0
    ) {
        kernel_panic(
            "Preexisting device unexpectedly changed driver state"
        );
    }

    driver_state.match_count =
        0;

    /*
     * The enumerator returns failure only after both devices were accepted.
     * Generic bus rollback must therefore remove both in reverse order.
     */
    if (
        bus_enumerate(
            &bus
        ) !=
            BUS_ENUMERATION_RESULT_DISCOVERY_FAILED ||
        enumeration.callback_count != 2 ||
        bus.enumeration_active ||
        bus.device_count != 1 ||
        registry.count != 1 ||
        bus.devices[0].device !=
            &preexisting ||
        preexisting.state !=
            DEVICE_STATE_ACTIVE ||
        first.state !=
            DEVICE_STATE_GONE ||
        second.state !=
            DEVICE_STATE_GONE ||
        first.reference_count != 0 ||
        second.reference_count != 0 ||
        driver_state.match_count != 2 ||
        driver_state.probe_count != 1 ||
        driver_state.remove_count != 1
    ) {
        kernel_panic(
            "Partial discovery failure did not roll back cleanly"
        );
    }

    if (
        bus_detach_device(
            &bus,
            &preexisting
        ) !=
            BUS_DEVICE_DETACH_RESULT_DETACHED ||
        preexisting.reference_count != 1 ||
        !device_release(
            &preexisting
        )
    ) {
        kernel_panic(
            "Partial enumeration preexisting device cleanup failed"
        );
    }

    if (
        bus_unregister_driver(
            &bus,
            &driver
        ) !=
            BUS_DRIVER_UNREGISTER_RESULT_UNREGISTERED
    ) {
        kernel_panic(
            "Partial enumeration driver cleanup failed"
        );
    }
}

static void bus_test_enumeration_attach_failure(void)
{
    struct device_registry registry;

    struct device first;
    struct device second;
    struct device duplicate;

    if (
        !device_initialize(
            &first,
            5700,
            "attach-first",
            sizeof("attach-first") - 1U,
            DEVICE_KIND_PHYSICAL,
            NULL,
            NULL
        ) ||
        !device_initialize(
            &second,
            5701,
            "attach-second",
            sizeof("attach-second") - 1U,
            DEVICE_KIND_PHYSICAL,
            NULL,
            NULL
        ) ||
        !device_initialize(
            &duplicate,
            5700,
            "attach-duplicate",
            sizeof("attach-duplicate") - 1U,
            DEVICE_KIND_PHYSICAL,
            NULL,
            NULL
        )
    ) {
        kernel_panic(
            "Attach-failure enumeration device initialization failed"
        );
    }

    struct device *devices[] = {
        &first,
        &second,
        &duplicate,
    };

    struct bus_test_enumeration_state enumeration = {
        .devices =
            devices,
        .device_count =
            3,
        .callback_count =
            0,
        .fail_after_enabled =
            false,
        .fail_after =
            0,
    };

    struct bus bus;

    if (
        !device_registry_initialize(
            &registry
        ) ||
        !bus_initialize(
            &bus,
            "attach-failure-bus",
            sizeof("attach-failure-bus") - 1U,
            &registry,
            &bus_test_operations,
            &enumeration
        )
    ) {
        kernel_panic(
            "Attach-failure enumeration fixture initialization failed"
        );
    }

    /*
     * first and second attach successfully. duplicate is rejected because
     * first already occupies identifier 5700.
     *
     * The mock enumerator owns and destroys the rejected duplicate. Generic
     * rollback owns removal of first and second.
     */
    if (
        bus_enumerate(
            &bus
        ) !=
            BUS_ENUMERATION_RESULT_ATTACH_FAILED ||
        enumeration.callback_count != 3 ||
        bus.enumeration_active ||
        bus.device_count != 0 ||
        registry.count != 0 ||
        first.state !=
            DEVICE_STATE_GONE ||
        second.state !=
            DEVICE_STATE_GONE ||
        duplicate.state !=
            DEVICE_STATE_GONE ||
        first.reference_count != 0 ||
        second.reference_count != 0 ||
        duplicate.reference_count != 0
    ) {
        kernel_panic(
            "Enumeration attach failure did not unwind cleanly"
        );
    }
}

static void bus_test_enumeration_rejection_latches_failure(void)
{
    struct device_registry registry;

    struct device first;
    struct device second;
    struct device duplicate;
    struct device trailing;

    if (
        !device_initialize(
            &first,
            5800,
            "latched-first",
            sizeof("latched-first") - 1U,
            DEVICE_KIND_PHYSICAL,
            NULL,
            NULL
        ) ||
        !device_initialize(
            &second,
            5801,
            "latched-second",
            sizeof("latched-second") - 1U,
            DEVICE_KIND_PHYSICAL,
            NULL,
            NULL
        ) ||
        !device_initialize(
            &duplicate,
            5800,
            "latched-duplicate",
            sizeof("latched-duplicate") - 1U,
            DEVICE_KIND_PHYSICAL,
            NULL,
            NULL
        ) ||
        !device_initialize(
            &trailing,
            5802,
            "latched-trailing",
            sizeof("latched-trailing") - 1U,
            DEVICE_KIND_PHYSICAL,
            NULL,
            NULL
        )
    ) {
        kernel_panic(
            "Latched enumeration fixture device initialization failed"
        );
    }

    struct device *devices[] = {
        &first,
        &second,
        &duplicate,
        &trailing,
    };

    struct bus_test_enumeration_state enumeration = {
        .devices =
            devices,
        .device_count =
            4,
        .callback_count =
            0,
        .fail_after_enabled =
            false,
        .fail_after =
            0,
        .continue_after_rejection =
            true,
    };

    struct bus bus;

    if (
        !device_registry_initialize(
            &registry
        ) ||
        !bus_initialize(
            &bus,
            "latched-failure-bus",
            sizeof("latched-failure-bus") - 1U,
            &registry,
            &bus_test_operations,
            &enumeration
        )
    ) {
        kernel_panic(
            "Latched enumeration fixture initialization failed"
        );
    }

    uint64_t initial_generation =
        registry.generation;

    /*
     * first and second attach normally.
     *
     * duplicate is then rejected because identifier 5800 is already
     * published. The intentionally broken enumerator ignores that false
     * callback result and continues with trailing.
     *
     * The generic enumeration context must remember the first failure:
     * trailing must also be rejected without ever reaching bus_attach_device().
     *
     * The enumerator then incorrectly returns true after processing all four
     * devices. bus_enumerate() must still report ATTACH_FAILED and roll back
     * only first and second.
     */
    if (
        bus_enumerate(
            &bus
        ) !=
            BUS_ENUMERATION_RESULT_ATTACH_FAILED ||
        enumeration.callback_count != 4 ||
        bus.enumeration_active ||
        bus.device_count != 0 ||
        registry.count != 0 ||
        first.state !=
            DEVICE_STATE_GONE ||
        second.state !=
            DEVICE_STATE_GONE ||
        duplicate.state !=
            DEVICE_STATE_GONE ||
        trailing.state !=
            DEVICE_STATE_GONE ||
        first.reference_count != 0 ||
        second.reference_count != 0 ||
        duplicate.reference_count != 0 ||
        trailing.reference_count != 0
    ) {
        kernel_panic(
            "Enumeration failure was not latched after callback rejection"
        );
    }

    /*
     * Only first and second were ever published:
     *
     *   +2 generations for registration
     *   +2 generations for rollback unregistration
     *
     * duplicate and trailing must never have mutated registry publication.
     */
    if (
        registry.generation !=
            initial_generation + 4ULL
    ) {
        kernel_panic(
            "Rejected trailing discovery mutated registry state"
        );
    }
}

static void bus_test_driver_capacity_name(
    size_t index,
    char name[
        BUS_TEST_DRIVER_CAPACITY_NAME_SIZE
    ])
{
    if (
        name == NULL ||
        index >
            BUS_DRIVER_CAPACITY
    ) {
        kernel_panic(
            "Bus driver capacity-name arguments invalid"
        );
    }

    name[0] = 'c';
    name[1] = 'a';
    name[2] = 'p';
    name[3] = 'd';
    name[4] = 'r';
    name[5] = 'v';
    name[6] = '-';

    name[7] =
        (char) (
            '0' +
            ((index / 10U) % 10U)
        );

    name[8] =
        (char) (
            '0' +
            (index % 10U)
        );

    name[9] =
        '\0';
}

static void bus_test_duplicate_driver_name(void)
{
    struct device_registry registry;

    struct bus bus;

    struct bus_test_driver_state first_state = {
        .match_identifier = 6000,
        .match_count = 0,
        .probe_count = 0,
        .remove_count = 0,
        .probe_success = true,
    };

    struct bus_test_driver_state second_state = {
        .match_identifier = 6001,
        .match_count = 0,
        .probe_count = 0,
        .remove_count = 0,
        .probe_success = true,
    };

    struct driver first;
    struct driver second;

    if (
        !device_registry_initialize(
            &registry
        ) ||
        !bus_initialize(
            &bus,
            "duplicate-driver-bus",
            sizeof("duplicate-driver-bus") - 1U,
            &registry,
            NULL,
            NULL
        ) ||
        !driver_initialize(
            &first,
            "duplicate-driver",
            sizeof("duplicate-driver") - 1U,
            &bus_test_driver_operations,
            &first_state
        ) ||
        !driver_initialize(
            &second,
            "duplicate-driver",
            sizeof("duplicate-driver") - 1U,
            &bus_test_driver_operations,
            &second_state
        )
    ) {
        kernel_panic(
            "Duplicate-driver fixture initialization failed"
        );
    }

    if (
        bus_register_driver(
            &bus,
            &first
        ) !=
            BUS_DRIVER_REGISTER_RESULT_REGISTERED ||
        bus_register_driver(
            &bus,
            &second
        ) !=
            BUS_DRIVER_REGISTER_RESULT_DUPLICATE_NAME ||
        bus.driver_count != 1 ||
        bus.drivers[0] !=
            &first ||
        first.bus !=
            &bus ||
        second.bus !=
            NULL
    ) {
        kernel_panic(
            "Duplicate driver name rejection failed"
        );
    }

    if (
        bus_unregister_driver(
            &bus,
            &first
        ) !=
            BUS_DRIVER_UNREGISTER_RESULT_UNREGISTERED
    ) {
        kernel_panic(
            "Duplicate-driver fixture cleanup failed"
        );
    }
}

static void bus_test_driver_capacity(void)
{
    struct device_registry registry;

    struct bus bus;

    if (
        !device_registry_initialize(
            &registry
        ) ||
        !bus_initialize(
            &bus,
            "driver-capacity-bus",
            sizeof("driver-capacity-bus") - 1U,
            &registry,
            NULL,
            NULL
        )
    ) {
        kernel_panic(
            "Driver-capacity fixture initialization failed"
        );
    }

    for (
        size_t index = 0;
        index < BUS_DRIVER_CAPACITY + 1U;
        ++index
    ) {
        char name[
            BUS_TEST_DRIVER_CAPACITY_NAME_SIZE
        ];

        bus_test_driver_capacity_name(
            index,
            name
        );

        bus_test_capacity_driver_states[index].match_identifier =
            DEVICE_IDENTIFIER_INVALID;

        bus_test_capacity_driver_states[index].match_count =
            0;

        bus_test_capacity_driver_states[index].probe_count =
            0;

        bus_test_capacity_driver_states[index].remove_count =
            0;

        bus_test_capacity_driver_states[index].probe_success =
            true;

        if (!driver_initialize(
            &bus_test_capacity_drivers[index],
            name,
            BUS_TEST_DRIVER_CAPACITY_NAME_LENGTH,
            &bus_test_driver_operations,
            &bus_test_capacity_driver_states[index]
        )) {
            kernel_panic(
                "Driver-capacity driver initialization failed"
            );
        }
    }

    for (
        size_t index = 0;
        index < BUS_DRIVER_CAPACITY;
        ++index
    ) {
        if (
            bus_register_driver(
                &bus,
                &bus_test_capacity_drivers[index]
            ) !=
                BUS_DRIVER_REGISTER_RESULT_REGISTERED ||
            bus.driver_count !=
                index + 1U
        ) {
            kernel_panic(
                "Bus driver capacity fill failed"
            );
        }
    }

    if (
        bus_register_driver(
            &bus,
            &bus_test_capacity_drivers[
                BUS_DRIVER_CAPACITY
            ]
        ) !=
            BUS_DRIVER_REGISTER_RESULT_RESOURCE_EXHAUSTED ||
        bus.driver_count !=
            BUS_DRIVER_CAPACITY ||
        bus_test_capacity_drivers[
            BUS_DRIVER_CAPACITY
        ].bus !=
            NULL
    ) {
        kernel_panic(
            "Bus driver capacity exhaustion contract failed"
        );
    }

    for (
        size_t remaining =
            BUS_DRIVER_CAPACITY;
        remaining != 0;
        --remaining
    ) {
        size_t index =
            remaining - 1U;

        if (
            bus_unregister_driver(
                &bus,
                &bus_test_capacity_drivers[index]
            ) !=
                BUS_DRIVER_UNREGISTER_RESULT_UNREGISTERED
        ) {
            kernel_panic(
                "Driver-capacity fixture cleanup failed"
            );
        }
    }

    if (
        bus.driver_count != 0
    ) {
        kernel_panic(
            "Driver-capacity bus did not return to empty state"
        );
    }
}

static void bus_test_repeated_binding(void)
{
    struct device_registry registry;

    struct bus bus;

    struct device device;

    struct bus_test_driver_state state = {
        .match_identifier = 6100,
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
            "rebind-bus",
            sizeof("rebind-bus") - 1U,
            &registry,
            NULL,
            NULL
        ) ||
        !device_initialize(
            &device,
            6100,
            "rebind-device",
            sizeof("rebind-device") - 1U,
            DEVICE_KIND_VIRTUAL,
            NULL,
            NULL
        ) ||
        bus_attach_device(
            &bus,
            &device
        ) !=
            BUS_DEVICE_ATTACH_RESULT_ATTACHED ||
        !driver_initialize(
            &driver,
            "rebind-driver",
            sizeof("rebind-driver") - 1U,
            &bus_test_driver_operations,
            &state
        )
    ) {
        kernel_panic(
            "Repeated-binding fixture initialization failed"
        );
    }

    for (
        size_t cycle = 0;
        cycle <
            BUS_TEST_REBIND_CYCLES;
        ++cycle
    ) {
        if (
            bus_register_driver(
                &bus,
                &driver
            ) !=
                BUS_DRIVER_REGISTER_RESULT_REGISTERED ||
            driver.bus !=
                &bus ||
            bus.devices[0].driver !=
                &driver ||
            bus.devices[0].binding_state !=
                BUS_BINDING_STATE_BOUND ||
            device.private_data !=
                &driver ||
            state.match_count !=
                cycle + 1U ||
            state.probe_count !=
                cycle + 1U ||
            state.remove_count !=
                cycle
        ) {
            kernel_panic(
                "Repeated driver bind failed"
            );
        }

        if (
            bus_unregister_driver(
                &bus,
                &driver
            ) !=
                BUS_DRIVER_UNREGISTER_RESULT_UNREGISTERED ||
            driver.bus !=
                NULL ||
            bus.devices[0].driver !=
                NULL ||
            bus.devices[0].binding_state !=
                BUS_BINDING_STATE_UNBOUND ||
            device.private_data !=
                NULL ||
            device.state !=
                DEVICE_STATE_ACTIVE ||
            registry.count != 1 ||
            state.remove_count !=
                cycle + 1U
        ) {
            kernel_panic(
                "Repeated driver unbind failed"
            );
        }
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
            "Repeated-binding fixture cleanup failed"
        );
    }
}

static void bus_test_parent_child_enumeration_rollback(void)
{
    struct device_registry registry;

    struct device parent;
    struct device child;

    if (
        !device_initialize(
            &parent,
            6200,
            "enumerated-parent",
            sizeof("enumerated-parent") - 1U,
            DEVICE_KIND_PHYSICAL,
            NULL,
            NULL
        ) ||
        !device_initialize_child(
            &child,
            &parent,
            6201,
            "enumerated-child",
            sizeof("enumerated-child") - 1U,
            DEVICE_KIND_VIRTUAL,
            NULL,
            NULL
        )
    ) {
        kernel_panic(
            "Parent-child enumeration fixture initialization failed"
        );
    }

    struct device *devices[] = {
        &parent,
        &child,
    };

    struct bus_test_enumeration_state enumeration = {
        .devices =
            devices,
        .device_count =
            2,
        .callback_count =
            0,
        .fail_after_enabled =
            true,
        .fail_after =
            2,
        .continue_after_rejection =
            false,
    };

    struct bus bus;

    if (
        !device_registry_initialize(
            &registry
        ) ||
        !bus_initialize(
            &bus,
            "topology-enumeration-bus",
            sizeof("topology-enumeration-bus") - 1U,
            &registry,
            &bus_test_operations,
            &enumeration
        )
    ) {
        kernel_panic(
            "Parent-child enumeration bus initialization failed"
        );
    }

    /*
     * Both devices are accepted before the synthetic discovery failure.
     *
     * Rollback must detach the child first. Doing parent first would be
     * rejected by device_begin_removal() because the ACTIVE child still owns
     * a topology relationship to it.
     */
    if (
        bus_enumerate(
            &bus
        ) !=
            BUS_ENUMERATION_RESULT_DISCOVERY_FAILED ||
        enumeration.callback_count != 2 ||
        bus.device_count != 0 ||
        registry.count != 0 ||
        child.state !=
            DEVICE_STATE_GONE ||
        child.reference_count != 0 ||
        parent.state !=
            DEVICE_STATE_GONE ||
        parent.reference_count != 0 ||
        parent.first_child !=
            NULL
    ) {
        kernel_panic(
            "Parent-child enumeration rollback ordering failed"
        );
    }
}

static void bus_test_invalid_structure(void)
{
    struct device_registry registry;

    struct bus bus;

    struct bus_test_driver_state state = {
        .match_identifier = 6300,
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
            "invalid-structure-bus",
            sizeof("invalid-structure-bus") - 1U,
            &registry,
            NULL,
            NULL
        ) ||
        !driver_initialize(
            &driver,
            "invalid-structure-driver",
            sizeof("invalid-structure-driver") - 1U,
            &bus_test_driver_operations,
            &state
        ) ||
        !device_initialize(
            &device,
            6300,
            "invalid-structure-device",
            sizeof("invalid-structure-device") - 1U,
            DEVICE_KIND_VIRTUAL,
            NULL,
            NULL
        )
    ) {
        kernel_panic(
            "Invalid bus structure fixture initialization failed"
        );
    }

    if (
        bus_enumerate(
            NULL
        ) !=
            BUS_ENUMERATION_RESULT_INVALID_ARGUMENT ||
        bus_enumerate(
            &bus
        ) !=
            BUS_ENUMERATION_RESULT_NOT_SUPPORTED ||
        bus_register_driver(
            NULL,
            &driver
        ) !=
            BUS_DRIVER_REGISTER_RESULT_INVALID_ARGUMENT ||
        bus_register_driver(
            &bus,
            NULL
        ) !=
            BUS_DRIVER_REGISTER_RESULT_INVALID_ARGUMENT ||
        bus_unregister_driver(
            NULL,
            &driver
        ) !=
            BUS_DRIVER_UNREGISTER_RESULT_INVALID_ARGUMENT ||
        bus_attach_device(
            NULL,
            &device
        ) !=
            BUS_DEVICE_ATTACH_RESULT_INVALID_ARGUMENT ||
        bus_attach_device(
            &bus,
            NULL
        ) !=
            BUS_DEVICE_ATTACH_RESULT_INVALID_ARGUMENT ||
        bus_detach_device(
            NULL,
            &device
        ) !=
            BUS_DEVICE_DETACH_RESULT_INVALID_ARGUMENT ||
        bus_detach_device(
            &bus,
            NULL
        ) !=
            BUS_DEVICE_DETACH_RESULT_INVALID_ARGUMENT
    ) {
        kernel_panic(
            "Bus core accepted invalid arguments"
        );
    }

    /*
     * Impossible counts must be rejected before any table traversal.
     */
    bus.driver_count =
        BUS_DRIVER_CAPACITY + 1U;

    if (
        bus_register_driver(
            &bus,
            &driver
        ) !=
            BUS_DRIVER_REGISTER_RESULT_INVALID_ARGUMENT
    ) {
        kernel_panic(
            "Bus core accepted impossible driver count"
        );
    }

    bus.driver_count =
        0;

    bus.device_count =
        BUS_DEVICE_CAPACITY + 1U;

    if (
        bus_attach_device(
            &bus,
            &device
        ) !=
            BUS_DEVICE_ATTACH_RESULT_INVALID_ARGUMENT
    ) {
        kernel_panic(
            "Bus core accepted impossible device count"
        );
    }

    bus.device_count =
        0;

    /*
     * Hidden entries beyond the declared compact prefix are also corruption.
     */
    bus.drivers[0] =
        &driver;

    if (
        bus_register_driver(
            &bus,
            &driver
        ) !=
            BUS_DRIVER_REGISTER_RESULT_INVALID_ARGUMENT
    ) {
        kernel_panic(
            "Bus core accepted hidden driver entry"
        );
    }

    bus.drivers[0] =
        NULL;

    bus.devices[0].device =
        &device;

    if (
        bus_attach_device(
            &bus,
            &device
        ) !=
            BUS_DEVICE_ATTACH_RESULT_INVALID_ARGUMENT
    ) {
        kernel_panic(
            "Bus core accepted hidden device entry"
        );
    }

    bus.devices[0].device =
        NULL;

    /*
     * Restore the canonical empty-slot representation before disposing of the
     * independent fixture objects.
     */
    bus.devices[0].driver =
        NULL;

    bus.devices[0].binding_state =
        BUS_BINDING_STATE_UNBOUND;

    if (
        !device_begin_removal(
            &device
        ) ||
        !device_finish_removal(
            &device
        ) ||
        !device_release(
            &device
        )
    ) {
        kernel_panic(
            "Invalid bus structure fixture cleanup failed"
        );
    }
}
