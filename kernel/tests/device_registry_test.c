// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

 /**
 * @file device_registry_test.c
 * @brief Device registry publication, iteration, and lifetime regressions.
 */

#include "device_registry_test.h"

#include "../core/device/device.h"
#include "../core/device/registry.h"
#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"

#include <stddef.h>
#include <stdint.h>

#define DEVICE_REGISTRY_TEST_REATTACH_CYCLES 8U

#define DEVICE_REGISTRY_TEST_CAPACITY_NAME_LENGTH 7U
#define DEVICE_REGISTRY_TEST_CAPACITY_NAME_SIZE 8U

_Static_assert(
    DEVICE_REGISTRY_CAPACITY <= 1000U,
    "Capacity test names support at most 1000 registry entries"
);

static size_t device_registry_test_destroy_count;

// Private functions and helpers declarations
static void device_registry_test_register_lookup_unregister(void);

static void device_registry_test_duplicates(void);

static void device_registry_test_invalid_arguments(void);

static void device_registry_test_parent_removal_blocked(void);

static void device_registry_test_iteration_order(void);

static void device_registry_test_iteration_invalidation(void);

static void device_registry_test_destroy(
    struct device *device
);

static void device_registry_test_capacity_name(
    size_t index,
    char name[
        DEVICE_REGISTRY_TEST_CAPACITY_NAME_SIZE
    ]
);

static void device_registry_test_capacity(void);

static void device_registry_test_invalid_structure(void);

static void device_registry_test_repeated_attach_detach(void);

// Public functions implementations
void device_registry_test_run(void)
{
    device_registry_test_register_lookup_unregister();
    device_registry_test_duplicates();
    device_registry_test_parent_removal_blocked();
    device_registry_test_invalid_arguments();
    device_registry_test_iteration_order();
    device_registry_test_iteration_invalidation();
    device_registry_test_capacity();
    device_registry_test_invalid_structure();
    device_registry_test_repeated_attach_detach();

    diagnostics_write(
        "[device] Device registry publication, iteration and lifecycle "
        "tests passed\n"
    );
}

// Private functions and helpers implementations
static void device_registry_test_destroy(
    struct device *device)
{
    if (
        device == NULL ||
        device->reference_count != 0 ||
        device->state !=
            DEVICE_STATE_GONE
    ) {
        kernel_panic(
            "Device registry destroy callback observed invalid lifetime"
        );
    }

    ++device_registry_test_destroy_count;
}

static void device_registry_test_capacity_name(
    size_t index,
    char name[
        DEVICE_REGISTRY_TEST_CAPACITY_NAME_SIZE
    ])
{
    if (
        name == NULL ||
        index >=
            DEVICE_REGISTRY_CAPACITY
    ) {
        kernel_panic(
            "Device registry capacity-name arguments invalid"
        );
    }

    name[0] = 'c';
    name[1] = 'a';
    name[2] = 'p';
    name[3] = '-';

    name[4] =
        (char) (
            '0' +
            ((index / 100U) % 10U)
        );

    name[5] =
        (char) (
            '0' +
            ((index / 10U) % 10U)
        );

    name[6] =
        (char) (
            '0' +
            (index % 10U)
        );

    name[7] =
        '\0';
}

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

    struct device_registry_iterator iterator;

    if (!device_registry_iterator_initialize(
        &registry,
        &iterator
    )) {
        kernel_panic(
            "Device registry invalid-argument iterator initialization failed"
        );
    }

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
        device_registry_iterator_initialize(
            NULL,
            &iterator
        ) ||
        device_registry_iterator_initialize(
            &registry,
            NULL
        ) ||
        device_registry_iterator_next(
            NULL,
            &result
        ) !=
            DEVICE_REGISTRY_ITERATION_RESULT_INVALID_ARGUMENT ||
        device_registry_iterator_next(
            &iterator,
            NULL
        ) !=
            DEVICE_REGISTRY_ITERATION_RESULT_INVALID_ARGUMENT ||
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

    uint64_t stable_generation =
        registry.generation;

    struct device_registry_iterator iterator;

    if (!device_registry_iterator_initialize(
        &registry,
        &iterator
    )) {
        kernel_panic(
            "Device registry blocked-removal iterator initialization failed"
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
        ) !=
            DEVICE_REGISTRY_UNREGISTER_RESULT_REMOVAL_BLOCKED ||
        detached != NULL ||
        registry.count != 1 ||
        registry.devices[0] !=
            &parent ||
        registry.generation !=
            stable_generation ||
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
     * A rejected detach is not a registry mutation. An iterator that existed
     * before the failed operation must therefore remain valid.
     */
    struct device *iterated =
        NULL;

    if (
        device_registry_iterator_next(
            &iterator,
            &iterated
        ) !=
            DEVICE_REGISTRY_ITERATION_RESULT_DEVICE ||
        iterated !=
            &parent ||
        iterator.index != 1 ||
        parent.reference_count != 4
    ) {
        kernel_panic(
            "Blocked unregister invalidated registry iterator"
        );
    }

    if (
        !device_release(
            iterated
        ) ||
        parent.reference_count != 3
    ) {
        kernel_panic(
            "Blocked-removal iterator reference release failed"
        );
    }

    struct device *sentinel =
        (struct device *)
        (uintptr_t) 1U;

    iterated =
        sentinel;

    if (
        device_registry_iterator_next(
            &iterator,
            &iterated
        ) !=
            DEVICE_REGISTRY_ITERATION_RESULT_END ||
        iterated != sentinel ||
        iterator.index != 1
    ) {
        kernel_panic(
            "Blocked unregister corrupted iterator completion"
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

static void device_registry_test_iteration_order(void)
{
    struct device_registry registry;

    struct device first;
    struct device second;
    struct device third;

    if (
        !device_registry_initialize(
            &registry
        ) ||
        registry.generation == 0 ||
        !device_initialize(
            &first,
            1300,
            "first-enumerated",
            sizeof("first-enumerated") - 1U,
            DEVICE_KIND_PHYSICAL,
            NULL,
            NULL
        ) ||
        !device_initialize(
            &second,
            1301,
            "second-enumerated",
            sizeof("second-enumerated") - 1U,
            DEVICE_KIND_VIRTUAL,
            NULL,
            NULL
        ) ||
        !device_initialize(
            &third,
            1302,
            "third-enumerated",
            sizeof("third-enumerated") - 1U,
            DEVICE_KIND_PSEUDO,
            NULL,
            NULL
        )
    ) {
        kernel_panic(
            "Device registry iteration fixture initialization failed"
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
            &second
        ) !=
            DEVICE_REGISTRY_REGISTER_RESULT_REGISTERED ||
        device_registry_register(
            &registry,
            &third
        ) !=
            DEVICE_REGISTRY_REGISTER_RESULT_REGISTERED ||
        registry.count != 3
    ) {
        kernel_panic(
            "Device registry iteration fixture registration failed"
        );
    }

    struct device_registry_iterator iterator;

    if (!device_registry_iterator_initialize(
        &registry,
        &iterator
    )) {
        kernel_panic(
            "Device registry iterator initialization failed"
        );
    }

    struct device *result =
        NULL;

    if (
        device_registry_iterator_next(
            &iterator,
            &result
        ) !=
            DEVICE_REGISTRY_ITERATION_RESULT_DEVICE ||
        result !=
            &first ||
        first.reference_count != 3
    ) {
        kernel_panic(
            "Device registry iterator first result mismatch"
        );
    }

    if (!device_release(
        result
    )) {
        kernel_panic(
            "Device registry iterator first result release failed"
        );
    }

    result =
        NULL;

    if (
        device_registry_iterator_next(
            &iterator,
            &result
        ) !=
            DEVICE_REGISTRY_ITERATION_RESULT_DEVICE ||
        result !=
            &second ||
        second.reference_count != 3
    ) {
        kernel_panic(
            "Device registry iterator second result mismatch"
        );
    }

    if (!device_release(
        result
    )) {
        kernel_panic(
            "Device registry iterator second result release failed"
        );
    }

    result =
        NULL;

    if (
        device_registry_iterator_next(
            &iterator,
            &result
        ) !=
            DEVICE_REGISTRY_ITERATION_RESULT_DEVICE ||
        result !=
            &third ||
        third.reference_count != 3
    ) {
        kernel_panic(
            "Device registry iterator third result mismatch"
        );
    }

    if (!device_release(
        result
    )) {
        kernel_panic(
            "Device registry iterator third result release failed"
        );
    }

    struct device *sentinel =
        (struct device *)
        (uintptr_t) 1U;

    result =
        sentinel;

    if (
        device_registry_iterator_next(
            &iterator,
            &result
        ) !=
            DEVICE_REGISTRY_ITERATION_RESULT_END ||
        result != sentinel ||
        iterator.index != 3
    ) {
        kernel_panic(
            "Device registry iterator end contract failed"
        );
    }

    /*
     * END is stable while the captured generation remains unchanged.
     */
    if (
        device_registry_iterator_next(
            &iterator,
            &result
        ) !=
            DEVICE_REGISTRY_ITERATION_RESULT_END ||
        result != sentinel ||
        iterator.index != 3
    ) {
        kernel_panic(
            "Device registry iterator repeated END changed state"
        );
    }

    struct device *detached_first =
        NULL;

    struct device *detached_second =
        NULL;

    struct device *detached_third =
        NULL;

    if (
        device_registry_unregister(
            &registry,
            &first,
            &detached_first
        ) !=
            DEVICE_REGISTRY_UNREGISTER_RESULT_UNREGISTERED ||
        device_registry_unregister(
            &registry,
            &second,
            &detached_second
        ) !=
            DEVICE_REGISTRY_UNREGISTER_RESULT_UNREGISTERED ||
        device_registry_unregister(
            &registry,
            &third,
            &detached_third
        ) !=
            DEVICE_REGISTRY_UNREGISTER_RESULT_UNREGISTERED
    ) {
        kernel_panic(
            "Device registry iteration fixture detach failed"
        );
    }

    struct device *objects[] = {
        &first,
        &second,
        &third,
    };

    struct device *detached[] = {
        detached_first,
        detached_second,
        detached_third,
    };

    for (
        size_t index = 0;
        index < 3;
        ++index
    ) {
        if (
            detached[index] !=
                objects[index] ||
            !device_finish_removal(
                objects[index]
            ) ||
            !device_release(
                detached[index]
            ) ||
            objects[index]->reference_count != 1 ||
            !device_release(
                objects[index]
            )
        ) {
            kernel_panic(
                "Device registry iteration fixture cleanup failed"
            );
        }
    }
}

static void device_registry_test_iteration_invalidation(void)
{
    struct device_registry registry;

    struct device first;
    struct device second;
    struct device third;
    struct device fourth;
    struct device duplicate;

    if (
        !device_registry_initialize(
            &registry
        ) ||
        !device_initialize(
            &first,
            1400,
            "iter-a",
            sizeof("iter-a") - 1U,
            DEVICE_KIND_VIRTUAL,
            NULL,
            NULL
        ) ||
        !device_initialize(
            &second,
            1401,
            "iter-b",
            sizeof("iter-b") - 1U,
            DEVICE_KIND_VIRTUAL,
            NULL,
            NULL
        ) ||
        !device_initialize(
            &third,
            1402,
            "iter-c",
            sizeof("iter-c") - 1U,
            DEVICE_KIND_VIRTUAL,
            NULL,
            NULL
        ) ||
        !device_initialize(
            &fourth,
            1403,
            "iter-d",
            sizeof("iter-d") - 1U,
            DEVICE_KIND_VIRTUAL,
            NULL,
            NULL
        ) ||
        !device_initialize(
            &duplicate,
            1400,
            "duplicate-id",
            sizeof("duplicate-id") - 1U,
            DEVICE_KIND_VIRTUAL,
            NULL,
            NULL
        )
    ) {
        kernel_panic(
            "Device registry invalidation fixture initialization failed"
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
            &second
        ) !=
            DEVICE_REGISTRY_REGISTER_RESULT_REGISTERED ||
        device_registry_register(
            &registry,
            &third
        ) !=
            DEVICE_REGISTRY_REGISTER_RESULT_REGISTERED
    ) {
        kernel_panic(
            "Device registry invalidation fixture registration failed"
        );
    }

    struct device_registry_iterator iterator;

    if (!device_registry_iterator_initialize(
        &registry,
        &iterator
    )) {
        kernel_panic(
            "Device registry invalidation iterator initialization failed"
        );
    }

    struct device *result =
        NULL;

    if (
        device_registry_iterator_next(
            &iterator,
            &result
        ) !=
            DEVICE_REGISTRY_ITERATION_RESULT_DEVICE ||
        result !=
            &first ||
        !device_release(
            result
        )
    ) {
        kernel_panic(
            "Device registry invalidation initial iteration failed"
        );
    }

    uint64_t stable_generation =
        registry.generation;

    /*
     * A rejected publication attempt must not invalidate an iterator because
     * registry contents did not change.
     */
    if (
        device_registry_register(
            &registry,
            &duplicate
        ) !=
            DEVICE_REGISTRY_REGISTER_RESULT_DUPLICATE_IDENTIFIER ||
        registry.generation !=
            stable_generation
    ) {
        kernel_panic(
            "Rejected device registration changed generation"
        );
    }

    result =
        NULL;

    if (
        device_registry_iterator_next(
            &iterator,
            &result
        ) !=
            DEVICE_REGISTRY_ITERATION_RESULT_DEVICE ||
        result !=
            &second ||
        !device_release(
            result
        )
    ) {
        kernel_panic(
            "Rejected mutation invalidated registry iterator"
        );
    }

    /*
     * A successful append changes publication and must invalidate the
     * pre-existing cursor.
     */
    if (
        device_registry_register(
            &registry,
            &fourth
        ) !=
            DEVICE_REGISTRY_REGISTER_RESULT_REGISTERED ||
        registry.generation ==
            stable_generation
    ) {
        kernel_panic(
            "Successful device registration did not change generation"
        );
    }

    struct device *sentinel =
        (struct device *)
        (uintptr_t) 1U;

    result =
        sentinel;

    size_t stale_index =
        iterator.index;

    if (
        device_registry_iterator_next(
            &iterator,
            &result
        ) !=
            DEVICE_REGISTRY_ITERATION_RESULT_INVALIDATED ||
        result != sentinel ||
        iterator.index !=
            stale_index
    ) {
        kernel_panic(
            "Registry mutation did not invalidate iterator safely"
        );
    }

    /*
     * A fresh iterator sees the complete current registration order.
     */
    struct device_registry_iterator current;

    if (!device_registry_iterator_initialize(
        &registry,
        &current
    )) {
        kernel_panic(
            "Device registry fresh iterator initialization failed"
        );
    }

    struct device *expected[] = {
        &first,
        &second,
        &third,
        &fourth,
    };

    for (
        size_t index = 0;
        index < 4;
        ++index
    ) {
        result =
            NULL;

        if (
            device_registry_iterator_next(
                &current,
                &result
            ) !=
                DEVICE_REGISTRY_ITERATION_RESULT_DEVICE ||
            result !=
                expected[index] ||
            !device_release(
                result
            )
        ) {
            kernel_panic(
                "Device registry fresh iteration order mismatch"
            );
        }
    }

    if (
        device_registry_iterator_next(
            &current,
            &result
        ) !=
            DEVICE_REGISTRY_ITERATION_RESULT_END
    ) {
        kernel_panic(
            "Device registry fresh iteration failed to end"
        );
    }

    /*
     * Unregistering the middle element compacts the registry while preserving
     * survivor order. Any iterator from before the compaction becomes stale.
     */
    struct device_registry_iterator before_detach;

    if (!device_registry_iterator_initialize(
        &registry,
        &before_detach
    )) {
        kernel_panic(
            "Device registry pre-detach iterator initialization failed"
        );
    }

    struct device *detached_second =
        NULL;

    if (
        device_registry_unregister(
            &registry,
            &second,
            &detached_second
        ) !=
            DEVICE_REGISTRY_UNREGISTER_RESULT_UNREGISTERED ||
        detached_second !=
            &second ||
        registry.count != 3 ||
        registry.devices[0] !=
            &first ||
        registry.devices[1] !=
            &third ||
        registry.devices[2] !=
            &fourth
    ) {
        kernel_panic(
            "Device registry deterministic compaction failed"
        );
    }

    result =
        sentinel;

    if (
        device_registry_iterator_next(
            &before_detach,
            &result
        ) !=
            DEVICE_REGISTRY_ITERATION_RESULT_INVALIDATED ||
        result != sentinel ||
        before_detach.index != 0
    ) {
        kernel_panic(
            "Device unregister failed to invalidate old iterator"
        );
    }

    /*
     * New iteration after compaction observes A, C, D exactly once and in
     * preserved relative order.
     */
    struct device_registry_iterator compacted;

    if (!device_registry_iterator_initialize(
        &registry,
        &compacted
    )) {
        kernel_panic(
            "Device registry compacted iterator initialization failed"
        );
    }

    struct device *compacted_expected[] = {
        &first,
        &third,
        &fourth,
    };

    for (
        size_t index = 0;
        index < 3;
        ++index
    ) {
        result =
            NULL;

        if (
            device_registry_iterator_next(
                &compacted,
                &result
            ) !=
                DEVICE_REGISTRY_ITERATION_RESULT_DEVICE ||
            result !=
                compacted_expected[index] ||
            !device_release(
                result
            )
        ) {
            kernel_panic(
                "Device registry compacted iteration order mismatch"
            );
        }
    }

    if (
        device_registry_iterator_next(
            &compacted,
            &result
        ) !=
            DEVICE_REGISTRY_ITERATION_RESULT_END
    ) {
        kernel_panic(
            "Device registry compacted iteration failed to end"
        );
    }

    /*
     * Finish and release B, which was already withdrawn.
     */
    if (
        !device_finish_removal(
            &second
        ) ||
        !device_release(
            detached_second
        ) ||
        second.reference_count != 1 ||
        !device_release(
            &second
        )
    ) {
        kernel_panic(
            "Device registry detached iteration fixture cleanup failed"
        );
    }

    struct device *remaining[] = {
        &first,
        &third,
        &fourth,
    };

    for (
        size_t index = 0;
        index < 3;
        ++index
    ) {
        struct device *detached =
            NULL;

        if (
            device_registry_unregister(
                &registry,
                remaining[index],
                &detached
            ) !=
                DEVICE_REGISTRY_UNREGISTER_RESULT_UNREGISTERED ||
            detached !=
                remaining[index] ||
            !device_finish_removal(
                remaining[index]
            ) ||
            !device_release(
                detached
            ) ||
            remaining[index]->reference_count != 1 ||
            !device_release(
                remaining[index]
            )
        ) {
            kernel_panic(
                "Device registry invalidation fixture cleanup failed"
            );
        }
    }

    if (
        !device_begin_removal(
            &duplicate
        ) ||
        !device_finish_removal(
            &duplicate
        ) ||
        !device_release(
            &duplicate
        )
    ) {
        kernel_panic(
            "Device registry duplicate invalidation fixture cleanup failed"
        );
    }
}

static void device_registry_test_capacity(void)
{
    struct device_registry registry;

    /*
     * Intentionally local. Keeping this fixture off static storage avoids
     * permanently enlarging the normal kernel image for a test-only array.
     *
     * The current kernel runtime stack is sufficient for this bounded
     * DEVICE_REGISTRY_CAPACITY fixture.
     */
    struct device devices[
        DEVICE_REGISTRY_CAPACITY
    ];

    if (!device_registry_initialize(
        &registry
    )) {
        kernel_panic(
            "Device registry capacity fixture initialization failed"
        );
    }

    uint64_t initial_generation =
        registry.generation;

    for (
        size_t index = 0;
        index < DEVICE_REGISTRY_CAPACITY;
        ++index
    ) {
        char name[
            DEVICE_REGISTRY_TEST_CAPACITY_NAME_SIZE
        ];

        device_registry_test_capacity_name(
            index,
            name
        );

        if (
            !device_initialize(
                &devices[index],
                2000ULL +
                    (uint64_t) index,
                name,
                DEVICE_REGISTRY_TEST_CAPACITY_NAME_LENGTH,
                DEVICE_KIND_VIRTUAL,
                NULL,
                NULL
            ) ||
            device_registry_register(
                &registry,
                &devices[index]
            ) !=
                DEVICE_REGISTRY_REGISTER_RESULT_REGISTERED ||
            registry.count !=
                index + 1U ||
            devices[index].reference_count != 2 ||
            registry.generation !=
                initial_generation +
                    (uint64_t) index +
                    1ULL
        ) {
            kernel_panic(
                "Device registry capacity fill failed"
            );
        }
    }

    if (
        registry.count !=
            DEVICE_REGISTRY_CAPACITY
    ) {
        kernel_panic(
            "Device registry did not reach declared capacity"
        );
    }

    /*
     * Capture an iterator before the rejected registration. RESOURCE_EXHAUSTED
     * is not a mutation and therefore must not invalidate it.
     */
    struct device_registry_iterator iterator;

    if (!device_registry_iterator_initialize(
        &registry,
        &iterator
    )) {
        kernel_panic(
            "Device registry full-capacity iterator initialization failed"
        );
    }

    uint64_t full_generation =
        registry.generation;

    struct device overflow;

    if (!device_initialize(
        &overflow,
        9999,
        "overflow",
        sizeof("overflow") - 1U,
        DEVICE_KIND_VIRTUAL,
        NULL,
        NULL
    )) {
        kernel_panic(
            "Device registry overflow fixture initialization failed"
        );
    }

    if (
        device_registry_register(
            &registry,
            &overflow
        ) !=
            DEVICE_REGISTRY_REGISTER_RESULT_RESOURCE_EXHAUSTED ||
        registry.count !=
            DEVICE_REGISTRY_CAPACITY ||
        registry.generation !=
            full_generation ||
        overflow.reference_count != 1
    ) {
        kernel_panic(
            "Full device registry mutation contract failed"
        );
    }

    /*
     * The iterator captured before the failed registration must still observe
     * every published object exactly once and in registration order.
     */
    for (
        size_t index = 0;
        index < DEVICE_REGISTRY_CAPACITY;
        ++index
    ) {
        struct device *result =
            NULL;

        if (
            device_registry_iterator_next(
                &iterator,
                &result
            ) !=
                DEVICE_REGISTRY_ITERATION_RESULT_DEVICE ||
            result !=
                &devices[index] ||
            !device_release(
                result
            )
        ) {
            kernel_panic(
                "Full device registry deterministic iteration failed"
            );
        }
    }

    struct device *sentinel =
        (struct device *)
        (uintptr_t) 1U;

    struct device *result =
        sentinel;

    if (
        device_registry_iterator_next(
            &iterator,
            &result
        ) !=
            DEVICE_REGISTRY_ITERATION_RESULT_END ||
        result != sentinel ||
        iterator.index !=
            DEVICE_REGISTRY_CAPACITY
    ) {
        kernel_panic(
            "Full device registry iterator completion failed"
        );
    }

    /*
     * Remove from the end so capacity cleanup does not spend time repeatedly
     * compacting the whole table. Each detach transfers registry ownership.
     */
    for (
        size_t remaining =
            DEVICE_REGISTRY_CAPACITY;
        remaining != 0;
        --remaining
    ) {
        size_t index =
            remaining - 1U;

        struct device *detached =
            NULL;

        if (
            device_registry_unregister(
                &registry,
                &devices[index],
                &detached
            ) !=
                DEVICE_REGISTRY_UNREGISTER_RESULT_UNREGISTERED ||
            detached !=
                &devices[index] ||
            registry.count !=
                index ||
            !device_finish_removal(
                &devices[index]
            ) ||
            !device_release(
                detached
            ) ||
            devices[index].reference_count != 1 ||
            !device_release(
                &devices[index]
            )
        ) {
            kernel_panic(
                "Device registry capacity fixture cleanup failed"
            );
        }
    }

    if (
        registry.count != 0 ||
        !device_begin_removal(
            &overflow
        ) ||
        !device_finish_removal(
            &overflow
        ) ||
        !device_release(
            &overflow
        )
    ) {
        kernel_panic(
            "Device registry overflow fixture cleanup failed"
        );
    }
}

static void device_registry_test_invalid_structure(void)
{
    struct device_registry registry;

    struct device object;

    if (
        !device_registry_initialize(
            &registry
        ) ||
        !device_initialize(
            &object,
            3000,
            "structure",
            sizeof("structure") - 1U,
            DEVICE_KIND_PSEUDO,
            NULL,
            NULL
        )
    ) {
        kernel_panic(
            "Device registry structural fixture initialization failed"
        );
    }

    struct device *sentinel =
        (struct device *)
        (uintptr_t) 1U;

    struct device *result =
        sentinel;

    struct device_registry_iterator iterator;

    /*
     * generation zero identifies an uninitialized/invalid registry.
     */
    registry.generation =
        0;

    if (
        device_registry_register(
            &registry,
            &object
        ) !=
            DEVICE_REGISTRY_REGISTER_RESULT_INVALID_ARGUMENT ||
        device_registry_lookup_identifier(
            &registry,
            3000,
            &result
        ) !=
            DEVICE_REGISTRY_LOOKUP_RESULT_INVALID_ARGUMENT ||
        device_registry_iterator_initialize(
            &registry,
            &iterator
        ) ||
        result != sentinel ||
        object.reference_count != 1
    ) {
        kernel_panic(
            "Device registry accepted zero generation"
        );
    }

    /*
     * An impossible count must be rejected before table traversal.
     */
    if (!device_registry_initialize(
        &registry
    )) {
        kernel_panic(
            "Device registry structural fixture reset failed"
        );
    }

    registry.count =
        DEVICE_REGISTRY_CAPACITY + 1U;

    if (
        device_registry_register(
            &registry,
            &object
        ) !=
            DEVICE_REGISTRY_REGISTER_RESULT_INVALID_ARGUMENT ||
        device_registry_lookup_name(
            &registry,
            "structure",
            sizeof("structure") - 1U,
            &result
        ) !=
            DEVICE_REGISTRY_LOOKUP_RESULT_INVALID_ARGUMENT ||
        result != sentinel ||
        object.reference_count != 1
    ) {
        kernel_panic(
            "Device registry accepted impossible count"
        );
    }

    /*
     * A hole inside the live prefix is invalid.
     */
    if (!device_registry_initialize(
        &registry
    )) {
        kernel_panic(
            "Device registry structural fixture second reset failed"
        );
    }

    registry.count =
        1;

    registry.devices[0] =
        NULL;

    if (
        device_registry_lookup_identifier(
            &registry,
            3000,
            &result
        ) !=
            DEVICE_REGISTRY_LOOKUP_RESULT_INVALID_ARGUMENT ||
        device_registry_iterator_initialize(
            &registry,
            &iterator
        ) ||
        result != sentinel
    ) {
        kernel_panic(
            "Device registry accepted live-prefix hole"
        );
    }

    /*
     * Entries beyond count must remain empty.
     */
    if (!device_registry_initialize(
        &registry
    )) {
        kernel_panic(
            "Device registry structural fixture third reset failed"
        );
    }

    registry.devices[0] =
        &object;

    if (
        device_registry_register(
            &registry,
            &object
        ) !=
            DEVICE_REGISTRY_REGISTER_RESULT_INVALID_ARGUMENT ||
        device_registry_iterator_initialize(
            &registry,
            &iterator
        ) ||
        object.reference_count != 1
    ) {
        kernel_panic(
            "Device registry accepted hidden tail entry"
        );
    }

    /*
     * Restore ordinary state before disposing of the independent fixture
     * object. No synthetic malformed registry state owns a device reference.
     */
    if (
        !device_registry_initialize(
            &registry
        ) ||
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
            "Device registry structural fixture cleanup failed"
        );
    }
}

static void device_registry_test_repeated_attach_detach(void)
{
    struct device_registry registry;

    struct device objects[
        DEVICE_REGISTRY_TEST_REATTACH_CYCLES
    ];

    struct device_operations operations = {
        .destroy =
            device_registry_test_destroy,
    };

    if (!device_registry_initialize(
        &registry
    )) {
        kernel_panic(
            "Device registry repeated-lifetime initialization failed"
        );
    }

    device_registry_test_destroy_count =
        0;

    struct device *previous_outstanding =
        NULL;

    for (
        size_t cycle = 0;
        cycle <
            DEVICE_REGISTRY_TEST_REATTACH_CYCLES;
        ++cycle
    ) {
        struct device *current =
            &objects[cycle];

        /*
         * Every cycle represents a new object lifetime with the same logical
         * identity and name.
         */
        if (!device_initialize(
            current,
            4000,
            "repeat-device",
            sizeof("repeat-device") - 1U,
            DEVICE_KIND_VIRTUAL,
            &operations,
            NULL
        )) {
            kernel_panic(
                "Repeated device lifetime initialization failed"
            );
        }

        /*
         * From cycle 1 onward, the previous GONE object is still physically
         * alive through one outstanding lookup reference. Publishing this new
         * lifetime must nevertheless succeed because the old object has
         * already left the discovery namespace.
         */
        if (
            device_registry_register(
                &registry,
                current
            ) !=
                DEVICE_REGISTRY_REGISTER_RESULT_REGISTERED ||
            registry.count != 1 ||
            registry.devices[0] !=
                current ||
            current->reference_count != 2
        ) {
            kernel_panic(
                "Repeated device lifetime registration failed"
            );
        }

        if (
            previous_outstanding != NULL
        ) {
            if (
                previous_outstanding->state !=
                    DEVICE_STATE_GONE ||
                previous_outstanding->reference_count != 1 ||
                device_registry_test_destroy_count !=
                    cycle - 1U
            ) {
                kernel_panic(
                    "Previous detached device lifetime corrupted"
                );
            }

            /*
             * Release the old lifetime only after the replacement has become
             * visible. This proves namespace reuse is independent of physical
             * lifetime.
             */
            if (
                !device_release(
                    previous_outstanding
                ) ||
                device_registry_test_destroy_count !=
                    cycle
            ) {
                kernel_panic(
                    "Outstanding detached device destruction failed"
                );
            }

            previous_outstanding =
                NULL;
        }

        struct device *outstanding =
            NULL;

        if (
            device_registry_lookup_identifier(
                &registry,
                4000,
                &outstanding
            ) !=
                DEVICE_REGISTRY_LOOKUP_RESULT_FOUND ||
            outstanding !=
                current ||
            current->reference_count != 3
        ) {
            kernel_panic(
                "Repeated device lifetime lookup failed"
            );
        }

        struct device *detached =
            NULL;

        if (
            device_registry_unregister(
                &registry,
                current,
                &detached
            ) !=
                DEVICE_REGISTRY_UNREGISTER_RESULT_UNREGISTERED ||
            detached !=
                current ||
            registry.count != 0 ||
            current->state !=
                DEVICE_STATE_REMOVING ||
            current->reference_count != 3
        ) {
            kernel_panic(
                "Repeated device lifetime detach failed"
            );
        }

        /*
         * Discovery must end immediately at detach even though three owned
         * references still keep the object alive.
         */
        struct device *sentinel =
            (struct device *)
            (uintptr_t) 1U;

        struct device *missing =
            sentinel;

        if (
            device_registry_lookup_identifier(
                &registry,
                4000,
                &missing
            ) !=
                DEVICE_REGISTRY_LOOKUP_RESULT_NOT_FOUND ||
            missing != sentinel ||
            device_registry_lookup_name(
                &registry,
                "repeat-device",
                sizeof("repeat-device") - 1U,
                &missing
            ) !=
                DEVICE_REGISTRY_LOOKUP_RESULT_NOT_FOUND ||
            missing != sentinel
        ) {
            kernel_panic(
                "Detached device lifetime remained discoverable"
            );
        }

        if (
            !device_finish_removal(
                current
            ) ||
            current->state !=
                DEVICE_STATE_GONE ||
            !device_release(
                detached
            ) ||
            current->reference_count != 2 ||
            !device_release(
                current
            ) ||
            current->reference_count != 1 ||
            device_registry_test_destroy_count !=
                cycle
        ) {
            kernel_panic(
                "Repeated detached device lifetime teardown failed"
            );
        }

        /*
         * The lookup reference is now the sole remaining owner. Keep it until
         * the next lifetime has successfully been registered.
         */
        previous_outstanding =
            outstanding;
    }

    if (
        previous_outstanding == NULL ||
        device_registry_test_destroy_count !=
            DEVICE_REGISTRY_TEST_REATTACH_CYCLES - 1U
    ) {
        kernel_panic(
            "Final outstanding device lifetime missing"
        );
    }

    if (
        !device_release(
            previous_outstanding
        ) ||
        device_registry_test_destroy_count !=
            DEVICE_REGISTRY_TEST_REATTACH_CYCLES ||
        registry.count != 0
    ) {
        kernel_panic(
            "Repeated device lifetime final destruction failed"
        );
    }
}
