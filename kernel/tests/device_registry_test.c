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

static void device_registry_test_iteration_order(void);

static void device_registry_test_iteration_invalidation(void);

// Public functions implementations
void device_registry_test_run(void)
{
    device_registry_test_register_lookup_unregister();
    device_registry_test_duplicates();
    device_registry_test_parent_removal_blocked();
    device_registry_test_invalid_arguments();
    device_registry_test_iteration_order();
    device_registry_test_iteration_invalidation();

    diagnostics_write(
        "[device] Device registry publication and iteration tests passed\n"
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
