// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file device_registry_test.c
 * @brief Device registry publication and lookup regressions.
 */

#include "device_registry_test.h"

#include "../core/device/device.h"
#include "../core/device/registry.h"
#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"

#include <stddef.h>
#include <stdint.h>

// Private functions and helpers declarations
static void device_registry_test_register_lookup_unregister(void);

static void device_registry_test_duplicates(void);

static void device_registry_test_invalid_arguments(void);

static void device_registry_test_parent_removal_blocked(void);

// Public functions implementations
void device_registry_test_run(void)
{
    device_registry_test_register_lookup_unregister();
    device_registry_test_duplicates();
    device_registry_test_parent_removal_blocked();
    device_registry_test_invalid_arguments();

    diagnostics_write(
        "[device] Device registry publication tests passed\n"
    );
}

// Private functions and helpers implementations
static void device_registry_test_register_lookup_unregister(void)
{
    struct device_registry registry;

    struct device object;

    if (
        !device_registry_initialize(
            &registry
        ) ||
        registry.count != 0
    ) {
        kernel_panic(
            "Device registry initialization failed"
        );
    }

    if (!device_initialize(
        &object,
        1000,
        "console",
        sizeof("console") - 1U,
        DEVICE_KIND_PSEUDO,
        NULL,
        NULL
    )) {
        kernel_panic(
            "Device registry fixture initialization failed"
        );
    }

    if (
        device_registry_register(
            &registry,
            &object
        ) !=
            DEVICE_REGISTRY_REGISTER_RESULT_REGISTERED ||
        registry.count != 1 ||
        registry.devices[0] !=
            &object ||
        object.reference_count != 2 ||
        object.state !=
            DEVICE_STATE_ACTIVE
    ) {
        kernel_panic(
            "Device registry registration contract failed"
        );
    }

    struct device *by_identifier =
        NULL;

    if (
        device_registry_lookup_identifier(
            &registry,
            1000,
            &by_identifier
        ) !=
            DEVICE_REGISTRY_LOOKUP_RESULT_FOUND ||
        by_identifier !=
            &object ||
        object.reference_count != 3
    ) {
        kernel_panic(
            "Device registry identifier lookup failed"
        );
    }

    struct device *by_name =
        NULL;

    if (
        device_registry_lookup_name(
            &registry,
            "console",
            sizeof("console") - 1U,
            &by_name
        ) !=
            DEVICE_REGISTRY_LOOKUP_RESULT_FOUND ||
        by_name !=
            &object ||
        object.reference_count != 4
    ) {
        kernel_panic(
            "Device registry name lookup failed"
        );
    }

    /*
     * Existing lookup references survive registry withdrawal.
     */
    struct device *detached =
        NULL;

    if (
        device_registry_unregister(
            &registry,
            &object,
            &detached
        ) !=
            DEVICE_REGISTRY_UNREGISTER_RESULT_UNREGISTERED ||
        detached !=
            &object ||
        registry.count != 0 ||
        registry.devices[0] !=
            NULL ||
        object.state !=
            DEVICE_STATE_REMOVING ||
        object.reference_count != 4
    ) {
        kernel_panic(
            "Device registry unregister contract failed"
        );
    }

    struct device *sentinel =
        (struct device *)
        (uintptr_t) 1U;

    struct device *missing =
        sentinel;

    if (
        device_registry_lookup_identifier(
            &registry,
            1000,
            &missing
        ) !=
            DEVICE_REGISTRY_LOOKUP_RESULT_NOT_FOUND ||
        missing != sentinel ||
        device_registry_lookup_name(
            &registry,
            "console",
            sizeof("console") - 1U,
            &missing
        ) !=
            DEVICE_REGISTRY_LOOKUP_RESULT_NOT_FOUND ||
        missing != sentinel
    ) {
        kernel_panic(
            "Unregistered device remained discoverable"
        );
    }

    if (
        device_retain(
            &object
        )
    ) {
        kernel_panic(
            "Unregistered device accepted new reference"
        );
    }

    if (
        !device_finish_removal(
            &object
        ) ||
        object.state !=
            DEVICE_STATE_GONE
    ) {
        kernel_panic(
            "Unregistered device removal completion failed"
        );
    }

    if (
        !device_release(
            by_identifier
        ) ||
        !device_release(
            by_name
        ) ||
        !device_release(
            detached
        ) ||
        object.reference_count != 1 ||
        !device_release(
            &object
        ) ||
        object.reference_count != 0
    ) {
        kernel_panic(
            "Device registry outstanding-reference teardown failed"
        );
    }
}

static void device_registry_test_duplicates(void)
{
    struct device_registry registry;

    struct device first;
    struct device duplicate_identifier;
    struct device duplicate_name;

    if (
        !device_registry_initialize(
            &registry
        ) ||
        !device_initialize(
            &first,
            1100,
            "first",
            sizeof("first") - 1U,
            DEVICE_KIND_PSEUDO,
            NULL,
            NULL
        ) ||
        !device_initialize(
            &duplicate_identifier,
            1100,
            "second",
            sizeof("second") - 1U,
            DEVICE_KIND_PSEUDO,
            NULL,
            NULL
        ) ||
        !device_initialize(
            &duplicate_name,
            1101,
            "first",
            sizeof("first") - 1U,
            DEVICE_KIND_PSEUDO,
            NULL,
            NULL
        )
    ) {
        kernel_panic(
            "Device registry duplicate fixture initialization failed"
        );
    }

    if (
        device_registry_register(
            &registry,
            &first
        ) !=
            DEVICE_REGISTRY_REGISTER_RESULT_REGISTERED ||
        device_registry_register(
            &registry,
            &first
        ) !=
            DEVICE_REGISTRY_REGISTER_RESULT_ALREADY_REGISTERED ||
        device_registry_register(
            &registry,
            &duplicate_identifier
        ) !=
            DEVICE_REGISTRY_REGISTER_RESULT_DUPLICATE_IDENTIFIER ||
        device_registry_register(
            &registry,
            &duplicate_name
        ) !=
            DEVICE_REGISTRY_REGISTER_RESULT_DUPLICATE_NAME ||
        registry.count != 1 ||
        first.reference_count != 2 ||
        duplicate_identifier.reference_count != 1 ||
        duplicate_name.reference_count != 1
    ) {
        kernel_panic(
            "Device registry duplicate rejection failed"
        );
    }

    struct device *detached =
        NULL;

    if (
        device_registry_unregister(
            &registry,
            &first,
            &detached
        ) !=
            DEVICE_REGISTRY_UNREGISTER_RESULT_UNREGISTERED ||
        detached !=
            &first ||
        first.reference_count != 2 ||
        !device_finish_removal(
            &first
        ) ||
        !device_release(
            detached
        ) ||
        first.reference_count != 1 ||
        !device_release(
            &first
        )
    ) {
        kernel_panic(
            "Device registry duplicate fixture cleanup failed"
        );
    }

    for (
        struct device *object =
            &duplicate_identifier;
        object != NULL;
        object =
            object == &duplicate_identifier
                ? &duplicate_name
                : NULL
    ) {
        if (
            !device_begin_removal(
                object
            ) ||
            !device_finish_removal(
                object
            ) ||
            !device_release(
                object
            )
        ) {
            kernel_panic(
                "Device registry rejected-device cleanup failed"
            );
        }
    }
}

static void device_registry_test_invalid_arguments(void)
{
    struct device_registry registry;

    if (!device_registry_initialize(
        &registry
    )) {
        kernel_panic(
            "Device registry invalid-argument fixture initialization failed"
        );
    }

    struct device object;

    if (!device_initialize(
        &object,
        1200,
        "device",
        sizeof("device") - 1U,
        DEVICE_KIND_VIRTUAL,
        NULL,
        NULL
    )) {
        kernel_panic(
            "Device registry invalid-argument device initialization failed"
        );
    }

    struct device *sentinel =
        (struct device *)
        (uintptr_t) 1U;

    struct device *result =
        sentinel;

    if (
        device_registry_initialize(
            NULL
        ) ||
        device_registry_register(
            NULL,
            &object
        ) !=
            DEVICE_REGISTRY_REGISTER_RESULT_INVALID_ARGUMENT ||
        device_registry_register(
            &registry,
            NULL
        ) !=
            DEVICE_REGISTRY_REGISTER_RESULT_INVALID_ARGUMENT ||
        device_registry_lookup_identifier(
            NULL,
            1200,
            &result
        ) !=
            DEVICE_REGISTRY_LOOKUP_RESULT_INVALID_ARGUMENT ||
        device_registry_lookup_identifier(
            &registry,
            DEVICE_IDENTIFIER_INVALID,
            &result
        ) !=
            DEVICE_REGISTRY_LOOKUP_RESULT_INVALID_ARGUMENT ||
        device_registry_lookup_identifier(
            &registry,
            1200,
            NULL
        ) !=
            DEVICE_REGISTRY_LOOKUP_RESULT_INVALID_ARGUMENT ||
        device_registry_lookup_name(
            &registry,
            NULL,
            1,
            &result
        ) !=
            DEVICE_REGISTRY_LOOKUP_RESULT_INVALID_ARGUMENT ||
        device_registry_lookup_name(
            &registry,
            "",
            0,
            &result
        ) !=
            DEVICE_REGISTRY_LOOKUP_RESULT_INVALID_ARGUMENT ||
        device_registry_lookup_name(
            &registry,
            "dev/x",
            sizeof("dev/x") - 1U,
            &result
        ) !=
            DEVICE_REGISTRY_LOOKUP_RESULT_INVALID_ARGUMENT ||
                device_registry_unregister(
            NULL,
            &object,
            &result
        ) !=
            DEVICE_REGISTRY_UNREGISTER_RESULT_INVALID_ARGUMENT ||
        device_registry_unregister(
            &registry,
            NULL,
            &result
        ) !=
            DEVICE_REGISTRY_UNREGISTER_RESULT_INVALID_ARGUMENT ||
        device_registry_unregister(
            &registry,
            &object,
            NULL
        ) !=
            DEVICE_REGISTRY_UNREGISTER_RESULT_INVALID_ARGUMENT ||
        result != sentinel
    ) {
        kernel_panic(
            "Device registry accepted invalid arguments"
        );
    }

    if (
        !device_begin_removal(
            &object
        ) ||
        !device_finish_removal(
            &object
        ) ||
        !device_release(
            &object
        )
    ) {
        kernel_panic(
            "Device registry invalid-argument fixture cleanup failed"
        );
    }
}

static void device_registry_test_parent_removal_blocked(void)
{
    struct device_registry registry;

    struct device parent;
    struct device child;

    if (
        !device_registry_initialize(
            &registry
        ) ||
        !device_initialize(
            &parent,
            1150,
            "parent",
            sizeof("parent") - 1U,
            DEVICE_KIND_PHYSICAL,
            NULL,
            NULL
        ) ||
        !device_initialize_child(
            &child,
            &parent,
            1151,
            "child",
            sizeof("child") - 1U,
            DEVICE_KIND_VIRTUAL,
            NULL,
            NULL
        )
    ) {
        kernel_panic(
            "Device registry blocked-removal fixture initialization failed"
        );
    }

    /*
     * parent references:
     *
     *   1 original owner
     *   1 child topology ownership
     */
    if (
        parent.reference_count != 2 ||
        child.reference_count != 1
    ) {
        kernel_panic(
            "Device registry blocked-removal initial ownership mismatch"
        );
    }

    if (
        device_registry_register(
            &registry,
            &parent
        ) !=
            DEVICE_REGISTRY_REGISTER_RESULT_REGISTERED ||
        registry.count != 1 ||
        registry.devices[0] !=
            &parent ||
        parent.reference_count != 3
    ) {
        kernel_panic(
            "Device registry blocked-removal registration failed"
        );
    }

    /*
     * An ACTIVE child prevents device_begin_removal(parent), so unregister
     * must be completely non-destructive:
     *
     * - parent remains ACTIVE;
     * - registry entry remains visible;
     * - registry ownership remains held;
     * - reference count does not change.
     */
    struct device *detached =
        NULL;

    if (
        device_registry_unregister(
            &registry,
            &parent,
            &detached
        ) != DEVICE_REGISTRY_UNREGISTER_RESULT_REMOVAL_BLOCKED ||
        detached != NULL ||
        registry.count != 1 ||
        registry.devices[0] !=
            &parent ||
        parent.state !=
            DEVICE_STATE_ACTIVE ||
        parent.reference_count != 3 ||
        parent.first_child !=
            &child
    ) {
        kernel_panic(
            "Blocked device unregister mutated registry state"
        );
    }

    /*
     * Because unregister failed before visibility changed, ordinary discovery
     * must continue to work.
     */
    struct device *lookup =
        NULL;

    if (
        device_registry_lookup_identifier(
            &registry,
            1150,
            &lookup
        ) !=
            DEVICE_REGISTRY_LOOKUP_RESULT_FOUND ||
        lookup !=
            &parent ||
        parent.reference_count != 4
    ) {
        kernel_panic(
            "Blocked unregister removed device from discovery"
        );
    }

    if (
        !device_release(
            lookup
        ) ||
        parent.reference_count != 3
    ) {
        kernel_panic(
            "Blocked-removal lookup reference release failed"
        );
    }

    /*
     * Remove the child first. Final child destruction unlinks it and releases
     * its topology-owned reference to parent.
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
            NULL ||
        parent.reference_count != 2
    ) {
        kernel_panic(
            "Device registry blocked-removal child teardown failed"
        );
    }

    /*
     * With the topology obstruction gone, unregister can now establish the
     * ACTIVE -> REMOVING transition and withdraw publication.
     */
    if (
        device_registry_unregister(
            &registry,
            &parent,
            &detached
        ) !=
            DEVICE_REGISTRY_UNREGISTER_RESULT_UNREGISTERED ||
        detached !=
            &parent ||
        registry.count != 0 ||
        registry.devices[0] !=
            NULL ||
        parent.state !=
            DEVICE_STATE_REMOVING ||
        parent.reference_count != 2
    ) {
        kernel_panic(
            "Device registry unregister failed after child removal"
        );
    }

    if (
        !device_finish_removal(
            &parent
        ) ||
        !device_release(
            detached
        ) ||
        parent.reference_count != 1 ||
        !device_release(
            &parent
        ) ||
        parent.reference_count != 0
    ) {
        kernel_panic(
            "Device registry blocked-removal fixture cleanup failed"
        );
    }
}
