// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file device_test.c
 * @brief Generic kernel device identity and lifetime regression tests.
 */

#include "device_test.h"

#include "../core/device/device.h"
#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"

#include <stddef.h>
#include <stdint.h>

// Static local variables
#define DEVICE_TEST_DESTROY_ORDER_MAX 8U

static uint64_t device_test_destroy_order[
    DEVICE_TEST_DESTROY_ORDER_MAX
];

static size_t device_test_destroy_order_count;

static size_t device_test_destroy_count;
static void *device_test_destroy_private_data;
static uint64_t device_test_destroy_identifier;

static struct device *device_test_expected_destroy_parent;
static bool device_test_destroy_saw_live_parent;

// Private functions and helpers declarations
static void device_test_destroy(
    struct device *device
);

static bool device_test_name_equal(
    const struct device *device,
    const char *name,
    size_t name_length
);

static void device_test_destroy_check_parent(
    struct device *device
);

static void device_test_initialization(void);

static void device_test_invalid_initialization(void);

static void device_test_reference_lifetime(void);

static void device_test_state_transitions(void);

static void device_test_optional_destroy(void);

static void device_test_reference_overflow(void);

static void device_test_parent_child_topology(void);

static void device_test_parent_removal_order(void);

static void device_test_multiple_gone_children(void);

static void device_test_child_initialization_errors(void);

static void device_test_invalid_state_rejection(void);

static void device_test_sibling_unlink_positions(void);

static void device_test_corrupt_topology_rejection(void);

static void device_test_destroy_parent_lifetime(void);

static void device_test_class_binding(void);

// Public functions implementations
void device_test_run(void)
{
    device_test_initialization();
    device_test_class_binding();
    device_test_invalid_initialization();
    device_test_reference_lifetime();
    device_test_state_transitions();
    device_test_optional_destroy();
    device_test_reference_overflow();
    device_test_parent_child_topology();
    device_test_parent_removal_order();
    device_test_multiple_gone_children();
    device_test_child_initialization_errors();
    device_test_invalid_state_rejection();
    device_test_sibling_unlink_positions();
    device_test_corrupt_topology_rejection();
    device_test_destroy_parent_lifetime();

    diagnostics_write(
        "[device] Generic device lifetime, topology and rejection tests passed\n"
    );
}

// Private functions and helpers implementations
static void device_test_destroy(
    struct device *device)
{
    if (device == NULL) {
        kernel_panic(
            "Device destroy callback received NULL"
        );
    }

    if (
        device->reference_count != 0 ||
        device->state !=
            DEVICE_STATE_GONE
    ) {
        kernel_panic(
            "Device destroy callback observed invalid lifetime state"
        );
    }

    ++device_test_destroy_count;

    device_test_destroy_private_data =
        device->private_data;

    device_test_destroy_identifier =
        device->identifier;

    if (
        device_test_destroy_order_count <
        DEVICE_TEST_DESTROY_ORDER_MAX
    ) {
        device_test_destroy_order[
            device_test_destroy_order_count
        ] = device->identifier;

        ++device_test_destroy_order_count;
    }
}

static void device_test_destroy_check_parent(
    struct device *device)
{
    if (
        device == NULL ||
        device->reference_count != 0 ||
        device->state !=
            DEVICE_STATE_GONE
    ) {
        kernel_panic(
            "Device parent-lifetime callback observed invalid child"
        );
    }

    struct device *parent =
        device->parent;

    if (
        parent == NULL ||
        parent !=
            device_test_expected_destroy_parent ||
        parent->reference_count == 0
    ) {
        kernel_panic(
            "Device parent died before child destroy callback"
        );
    }

    /*
     * Topology membership ends before destroy(), while the parent ownership
     * reference deliberately remains alive until the callback returns.
     */
    if (
        parent->first_child ==
            device ||
        device->previous_sibling !=
            NULL ||
        device->next_sibling !=
            NULL
    ) {
        kernel_panic(
            "Device child remained linked during destroy callback"
        );
    }

    device_test_destroy_saw_live_parent =
        true;
}

static bool device_test_name_equal(
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

    return
        device->name[
            name_length
        ] == '\0';
}

static void device_test_initialization(void)
{
    struct device object;

    uint64_t private_value =
        0x1122334455667788ULL;

    struct device_operations operations = {
        .destroy =
            device_test_destroy,
    };

    static const char name[] =
        "console";

    if (!device_initialize(
        &object,
        42,
        name,
        sizeof(name) - 1U,
        DEVICE_KIND_PSEUDO,
        &operations,
        &private_value
    )) {
        kernel_panic(
            "Device initialization rejected valid object"
        );
    }

    if (
        object.identifier != 42 ||
        !device_test_name_equal(
            &object,
            name,
            sizeof(name) - 1U
        ) ||
        object.kind !=
            DEVICE_KIND_PSEUDO ||
        object.device_class !=
            DEVICE_CLASS_NONE ||
        object.class_interface !=
            NULL ||
        object.state !=
            DEVICE_STATE_ACTIVE ||
        object.reference_count != 1 ||
        object.operations !=
            &operations ||
        object.private_data !=
            &private_value
    ) {
        kernel_panic(
            "Device initialization produced invalid state"
        );
    }

    if (
        object.identifier != 42 ||
        !device_test_name_equal(
            &object,
            name,
            sizeof(name) - 1U
        ) ||
        object.kind !=
            DEVICE_KIND_PSEUDO ||
        object.state !=
            DEVICE_STATE_ACTIVE ||
        object.reference_count != 1 ||
        object.parent != NULL ||
        object.first_child != NULL ||
        object.previous_sibling != NULL ||
        object.next_sibling != NULL ||
        object.operations !=
            &operations ||
        object.private_data !=
            &private_value
    ) {
        kernel_panic(
            "Device initialization produced invalid state"
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
            "Device initialization fixture cleanup failed"
        );
    }
}

static void device_test_class_binding(void)
{
    struct device object;

    if (!device_initialize(
        &object,
        43,
        "class-test",
        sizeof("class-test") - 1U,
        DEVICE_KIND_VIRTUAL,
        NULL,
        NULL
    )) {
        kernel_panic(
            "Device class fixture initialization failed"
        );
    }

    uint64_t character_interface =
        0x1122334455667788ULL;

    uint64_t block_interface =
        0x8877665544332211ULL;

    if (
        object.device_class !=
            DEVICE_CLASS_NONE ||
        object.class_interface !=
            NULL ||
        device_class_interface(
            &object,
            DEVICE_CLASS_CHARACTER
        ) != NULL ||
        device_class_interface(
            &object,
            DEVICE_CLASS_BLOCK
        ) != NULL ||
        device_class_interface(
            &object,
            DEVICE_CLASS_NONE
        ) != NULL
    ) {
        kernel_panic(
            "Unbound device exposed a class interface"
        );
    }

    if (
        device_class_bind(
            NULL,
            DEVICE_CLASS_CHARACTER,
            &character_interface
        ) ||
        device_class_bind(
            &object,
            DEVICE_CLASS_NONE,
            &character_interface
        ) ||
        device_class_bind(
            &object,
            (enum device_class) 99,
            &character_interface
        ) ||
        device_class_bind(
            &object,
            DEVICE_CLASS_CHARACTER,
            NULL
        )
    ) {
        kernel_panic(
            "Device class binding accepted invalid input"
        );
    }

    if (
        object.device_class !=
            DEVICE_CLASS_NONE ||
        object.class_interface !=
            NULL
    ) {
        kernel_panic(
            "Rejected device class binding mutated object"
        );
    }

    if (!device_class_bind(
        &object,
        DEVICE_CLASS_CHARACTER,
        &character_interface
    )) {
        kernel_panic(
            "Device rejected valid class binding"
        );
    }

    if (
        object.device_class !=
            DEVICE_CLASS_CHARACTER ||
        object.class_interface !=
            &character_interface ||
        device_class_interface(
            &object,
            DEVICE_CLASS_CHARACTER
        ) !=
            &character_interface ||
        device_class_interface(
            &object,
            DEVICE_CLASS_BLOCK
        ) != NULL
    ) {
        kernel_panic(
            "Device class discovery contract failed"
        );
    }

    /*
     * The initial device model exposes one primary I/O class. Binding is
     * therefore one-shot for one initialized device lifetime.
     */
    if (
        device_class_bind(
            &object,
            DEVICE_CLASS_BLOCK,
            &block_interface
        ) ||
        object.device_class !=
            DEVICE_CLASS_CHARACTER ||
        object.class_interface !=
            &character_interface
    ) {
        kernel_panic(
            "Device accepted a second primary class"
        );
    }

    if (!device_begin_removal(
        &object
    )) {
        kernel_panic(
            "Device class fixture failed to begin removal"
        );
    }

    if (
        device_class_interface(
            &object,
            DEVICE_CLASS_CHARACTER
        ) != NULL ||
        device_class_bind(
            &object,
            DEVICE_CLASS_BLOCK,
            &block_interface
        )
    ) {
        kernel_panic(
            "Inactive device exposed or accepted a class"
        );
    }

    if (
        !device_finish_removal(
            &object
        ) ||
        !device_release(
            &object
        )
    ) {
        kernel_panic(
            "Device class fixture cleanup failed"
        );
    }
}

static void device_test_invalid_initialization(void)
{
    struct device object = {
        .identifier =
            0xaabbccddeeff0011ULL,
    };

    static const char valid_name[] =
        "device";

    static const char slash_name[] = {
        'd', 'e', 'v', '/', 'x',
    };

    static const char embedded_nul[] = {
        'a', '\0', 'b',
    };

    char oversized_name[
        DEVICE_NAME_MAX + 1U
    ];

    for (
        size_t index = 0;
        index < sizeof(oversized_name);
        ++index
    ) {
        oversized_name[index] =
            'x';
    }

    if (
        device_initialize(
            NULL,
            1,
            valid_name,
            sizeof(valid_name) - 1U,
            DEVICE_KIND_PHYSICAL,
            NULL,
            NULL
        ) ||
        device_initialize(
            &object,
            DEVICE_IDENTIFIER_INVALID,
            valid_name,
            sizeof(valid_name) - 1U,
            DEVICE_KIND_PHYSICAL,
            NULL,
            NULL
        ) ||
        device_initialize(
            &object,
            1,
            NULL,
            sizeof(valid_name) - 1U,
            DEVICE_KIND_PHYSICAL,
            NULL,
            NULL
        ) ||
        device_initialize(
            &object,
            1,
            valid_name,
            0,
            DEVICE_KIND_PHYSICAL,
            NULL,
            NULL
        ) ||
        device_initialize(
            &object,
            1,
            oversized_name,
            sizeof(oversized_name),
            DEVICE_KIND_PHYSICAL,
            NULL,
            NULL
        ) ||
        device_initialize(
            &object,
            1,
            slash_name,
            sizeof(slash_name),
            DEVICE_KIND_PHYSICAL,
            NULL,
            NULL
        ) ||
        device_initialize(
            &object,
            1,
            embedded_nul,
            sizeof(embedded_nul),
            DEVICE_KIND_PHYSICAL,
            NULL,
            NULL
        ) ||
        device_initialize(
            &object,
            1,
            valid_name,
            sizeof(valid_name) - 1U,
            (enum device_kind) 99,
            NULL,
            NULL
        )
    ) {
        kernel_panic(
            "Device initialization accepted invalid input"
        );
    }

    if (
        object.identifier !=
        0xaabbccddeeff0011ULL
    ) {
        kernel_panic(
            "Rejected device initialization mutated object"
        );
    }
}

static void device_test_reference_lifetime(void)
{
    struct device object;

    uint64_t private_value =
        0x8877665544332211ULL;

    struct device_operations operations = {
        .destroy =
            device_test_destroy,
    };

    device_test_destroy_count =
        0;

    device_test_destroy_private_data =
        NULL;

    device_test_destroy_identifier =
        DEVICE_IDENTIFIER_INVALID;

    if (!device_initialize(
        &object,
        7,
        "null",
        sizeof("null") - 1U,
        DEVICE_KIND_PSEUDO,
        &operations,
        &private_value
    )) {
        kernel_panic(
            "Device lifetime fixture initialization failed"
        );
    }

    /*
     * The initial reference owns the device object itself. An ACTIVE device
     * cannot lose that final reference without first completing removal.
     */
    if (
        device_release(
            &object
        ) ||
        object.reference_count != 1 ||
        object.state !=
            DEVICE_STATE_ACTIVE ||
        device_test_destroy_count != 0
    ) {
        kernel_panic(
            "ACTIVE device released its final owning reference"
        );
    }

    if (
        !device_retain(
            &object
        ) ||
        !device_retain(
            &object
        ) ||
        object.reference_count != 3
    ) {
        kernel_panic(
            "Device retain contract failed"
        );
    }

    if (
        !device_release(
            &object
        ) ||
        object.reference_count != 2 ||
        device_test_destroy_count != 0
    ) {
        kernel_panic(
            "Device ACTIVE release contract failed"
        );
    }

    if (!device_begin_removal(
        &object
    )) {
        kernel_panic(
            "Device failed to begin removal"
        );
    }

    if (
        object.state !=
            DEVICE_STATE_REMOVING ||
        device_retain(
            &object
        ) ||
        object.reference_count != 2
    ) {
        kernel_panic(
            "Device accepted reference after removal began"
        );
    }

    if (
        !device_release(
            &object
        ) ||
        object.reference_count != 1 ||
        device_test_destroy_count != 0
    ) {
        kernel_panic(
            "Device REMOVING reference release failed"
        );
    }

    /*
     * The last reference owns the device object itself and cannot disappear
     * while removal is still incomplete.
     */
    if (
        device_release(
            &object
        ) ||
        object.reference_count != 1 ||
        device_test_destroy_count != 0
    ) {
        kernel_panic(
            "Device destroyed before removal completed"
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
            "Device failed to complete removal"
        );
    }

    /*
     * GONE ends discoverability/acquisition, not object lifetime. Existing
     * references remain valid, but no new owner may acquire the device.
     */
    if (
        device_retain(
            &object
        ) ||
        object.reference_count != 1 ||
        device_test_destroy_count != 0
    ) {
        kernel_panic(
            "GONE device accepted a new reference"
        );
    }

    if (!device_release(
        &object
    )) {
        kernel_panic(
            "Device final release failed"
        );
    }

    if (
        object.reference_count != 0 ||
        device_test_destroy_count != 1 ||
        device_test_destroy_private_data !=
            &private_value ||
        device_test_destroy_identifier != 7
    ) {
        kernel_panic(
            "Device final destruction contract mismatch"
        );
    }

    if (
        device_retain(
            &object
        ) ||
        device_release(
            &object
        ) ||
        device_begin_removal(
            &object
        ) ||
        device_finish_removal(
            &object
        ) ||
        device_test_destroy_count != 1
    ) {
        kernel_panic(
            "Dead device accepted lifetime operation"
        );
    }
}

static void device_test_state_transitions(void)
{
    struct device object;

    if (!device_initialize(
        &object,
        9,
        "virtual-device",
        sizeof("virtual-device") - 1U,
        DEVICE_KIND_VIRTUAL,
        NULL,
        NULL
    )) {
        kernel_panic(
            "Device state fixture initialization failed"
        );
    }

    if (
        device_finish_removal(
            &object
        ) ||
        object.state !=
            DEVICE_STATE_ACTIVE
    ) {
        kernel_panic(
            "Device skipped REMOVING state"
        );
    }

    if (
        !device_begin_removal(
            &object
        ) ||
        object.state !=
            DEVICE_STATE_REMOVING
    ) {
        kernel_panic(
            "Device ACTIVE to REMOVING transition failed"
        );
    }

    if (
        device_begin_removal(
            &object
        ) ||
        object.state !=
            DEVICE_STATE_REMOVING
    ) {
        kernel_panic(
            "Device repeated begin-removal transition"
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
            "Device REMOVING to GONE transition failed"
        );
    }

    if (
        device_begin_removal(
            &object
        ) ||
        device_finish_removal(
            &object
        ) ||
        object.state !=
            DEVICE_STATE_GONE
    ) {
        kernel_panic(
            "Device accepted transition after GONE"
        );
    }

    if (!device_release(
        &object
    )) {
        kernel_panic(
            "Device state fixture cleanup failed"
        );
    }
}

static void device_test_optional_destroy(void)
{
    struct device object;

    if (
        !device_initialize(
            &object,
            10,
            "zero",
            sizeof("zero") - 1U,
            DEVICE_KIND_PSEUDO,
            NULL,
            NULL
        ) ||
        !device_begin_removal(
            &object
        ) ||
        !device_finish_removal(
            &object
        ) ||
        !device_release(
            &object
        ) ||
        object.reference_count != 0
    ) {
        kernel_panic(
            "Device without destroy callback lifetime failed"
        );
    }
}

static void device_test_reference_overflow(void)
{
    struct device object;

    if (!device_initialize(
        &object,
        11,
        "physical-device",
        sizeof("physical-device") - 1U,
        DEVICE_KIND_PHYSICAL,
        NULL,
        NULL
    )) {
        kernel_panic(
            "Device overflow fixture initialization failed"
        );
    }

    object.reference_count =
        SIZE_MAX;

    if (
        device_retain(
            &object
        ) ||
        object.reference_count !=
            SIZE_MAX
    ) {
        kernel_panic(
            "Device reference overflow accepted"
        );
    }

    object.reference_count =
        1;

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
            "Device overflow fixture cleanup failed"
        );
    }
}

static void device_test_parent_child_topology(void)
{
    struct device parent;
    struct device first;
    struct device second;
    struct device grandchild;

    if (!device_initialize(
        &parent,
        100,
        "parent",
        sizeof("parent") - 1U,
        DEVICE_KIND_PHYSICAL,
        NULL,
        NULL
    )) {
        kernel_panic(
            "Device parent topology initialization failed"
        );
    }

    if (
        parent.parent != NULL ||
        parent.first_child != NULL ||
        parent.previous_sibling != NULL ||
        parent.next_sibling != NULL ||
        parent.reference_count != 1
    ) {
        kernel_panic(
            "Root device initialized with invalid topology"
        );
    }

    if (!device_initialize_child(
        &first,
        &parent,
        101,
        "first",
        sizeof("first") - 1U,
        DEVICE_KIND_VIRTUAL,
        NULL,
        NULL
    )) {
        kernel_panic(
            "Unable to initialize first child device"
        );
    }

    if (
        first.parent !=
            &parent ||
        parent.first_child !=
            &first ||
        first.previous_sibling !=
            NULL ||
        first.next_sibling !=
            NULL ||
        parent.reference_count !=
            2
    ) {
        kernel_panic(
            "First child topology contract mismatch"
        );
    }

    if (!device_initialize_child(
        &second,
        &parent,
        102,
        "second",
        sizeof("second") - 1U,
        DEVICE_KIND_VIRTUAL,
        NULL,
        NULL
    )) {
        kernel_panic(
            "Unable to initialize second child device"
        );
    }

    if (
        parent.first_child !=
            &second ||
        second.parent !=
            &parent ||
        second.previous_sibling !=
            NULL ||
        second.next_sibling !=
            &first ||
        first.previous_sibling !=
            &second ||
        first.next_sibling !=
            NULL ||
        parent.reference_count !=
            3
    ) {
        kernel_panic(
            "Sibling topology contract mismatch"
        );
    }

    if (!device_initialize_child(
        &grandchild,
        &first,
        103,
        "grandchild",
        sizeof("grandchild") - 1U,
        DEVICE_KIND_VIRTUAL,
        NULL,
        NULL
    )) {
        kernel_panic(
            "Unable to initialize grandchild device"
        );
    }

    if (
        grandchild.parent !=
            &first ||
        first.first_child !=
            &grandchild ||
        first.reference_count !=
            2 ||
        parent.reference_count !=
            3
    ) {
        kernel_panic(
            "Nested device topology contract mismatch"
        );
    }

    /*
     * Teardown bottom-up.
     */
    if (
        !device_begin_removal(
            &grandchild
        ) ||
        !device_finish_removal(
            &grandchild
        ) ||
        !device_release(
            &grandchild
        )
    ) {
        kernel_panic(
            "Grandchild topology cleanup failed"
        );
    }

    if (
        first.first_child != NULL ||
        first.reference_count != 1
    ) {
        kernel_panic(
            "Grandchild destruction did not release parent topology"
        );
    }

    if (
        !device_begin_removal(
            &first
        ) ||
        !device_finish_removal(
            &first
        ) ||
        !device_release(
            &first
        )
    ) {
        kernel_panic(
            "First child topology cleanup failed"
        );
    }

    if (
        parent.first_child !=
            &second ||
        second.next_sibling != NULL ||
        parent.reference_count !=
            2
    ) {
        kernel_panic(
            "Child unlink did not repair sibling topology"
        );
    }

    if (
        !device_begin_removal(
            &second
        ) ||
        !device_finish_removal(
            &second
        ) ||
        !device_release(
            &second
        )
    ) {
        kernel_panic(
            "Second child topology cleanup failed"
        );
    }

    if (
        parent.first_child != NULL ||
        parent.reference_count != 1
    ) {
        kernel_panic(
            "Parent topology remained after child destruction"
        );
    }

    if (
        !device_begin_removal(
            &parent
        ) ||
        !device_finish_removal(
            &parent
        ) ||
        !device_release(
            &parent
        )
    ) {
        kernel_panic(
            "Parent topology fixture cleanup failed"
        );
    }
}

static void device_test_parent_removal_order(void)
{
    struct device parent;
    struct device child;

    struct device_operations operations = {
        .destroy =
            device_test_destroy,
    };

    device_test_destroy_count =
        0;

    device_test_destroy_order_count =
        0;

    if (
        !device_initialize(
            &parent,
            200,
            "root-device",
            sizeof("root-device") - 1U,
            DEVICE_KIND_PHYSICAL,
            &operations,
            NULL
        ) ||
        !device_initialize_child(
            &child,
            &parent,
            201,
            "child-device",
            sizeof("child-device") - 1U,
            DEVICE_KIND_PHYSICAL,
            &operations,
            NULL
        )
    ) {
        kernel_panic(
            "Device removal-order fixture initialization failed"
        );
    }

    /*
     * Keep one external child reference alive so the child can become GONE
     * without being physically destroyed.
     */
    if (
        !device_retain(
            &child
        ) ||
        child.reference_count != 2 ||
        parent.reference_count != 2
    ) {
        kernel_panic(
            "Unable to retain child removal-order reference"
        );
    }

    /*
     * An ACTIVE child blocks removal of its parent.
     */
    if (
        device_begin_removal(
            &parent
        ) ||
        parent.state !=
            DEVICE_STATE_ACTIVE
    ) {
        kernel_panic(
            "Parent removal accepted ACTIVE child"
        );
    }

    if (!device_begin_removal(
        &child
    )) {
        kernel_panic(
            "Unable to begin child removal"
        );
    }

    /*
     * REMOVING is still logically present enough to block parent removal.
     */
    if (
        device_begin_removal(
            &parent
        ) ||
        parent.state !=
            DEVICE_STATE_ACTIVE
    ) {
        kernel_panic(
            "Parent removal accepted REMOVING child"
        );
    }

    if (
        !device_finish_removal(
            &child
        ) ||
        child.state !=
            DEVICE_STATE_GONE ||
        child.reference_count != 2
    ) {
        kernel_panic(
            "Unable to complete logical child removal"
        );
    }

    /*
     * GONE is sufficient. Physical child lifetime may continue because an
     * existing holder still owns a reference.
     */
    if (
        !device_begin_removal(
            &parent
        ) ||
        !device_finish_removal(
            &parent
        ) ||
        parent.state !=
            DEVICE_STATE_GONE
    ) {
        kernel_panic(
            "Parent removal remained blocked by GONE child"
        );
    }

    /*
     * Drop the parent's original owner. The child-owned topology reference
     * must keep the parent object alive.
     */
    if (
        !device_release(
            &parent
        ) ||
        parent.reference_count != 1 ||
        device_test_destroy_count != 0
    ) {
        kernel_panic(
            "Child failed to retain GONE parent lifetime"
        );
    }

    /*
     * First child release drops only the external holder.
     */
    if (
        !device_release(
            &child
        ) ||
        child.reference_count != 1 ||
        parent.reference_count != 1 ||
        device_test_destroy_count != 0
    ) {
        kernel_panic(
            "Existing child reference release mismatch"
        );
    }

    /*
     * Final child release destroys child first, then releases the topology
     * reference which allows the already-GONE parent to be destroyed.
     */
    if (!device_release(
        &child
    )) {
        kernel_panic(
            "Final child removal-order release failed"
        );
    }

    if (
        device_test_destroy_count != 2 ||
        device_test_destroy_order_count != 2 ||
        device_test_destroy_order[0] != 201 ||
        device_test_destroy_order[1] != 200
    ) {
        kernel_panic(
            "Device child/parent destruction order mismatch"
        );
    }
}

static void device_test_multiple_gone_children(void)
{
    struct device parent;
    struct device first;
    struct device second;

    struct device_operations operations = {
        .destroy =
            device_test_destroy,
    };

    device_test_destroy_count =
        0;

    device_test_destroy_order_count =
        0;

    if (
        !device_initialize(
            &parent,
            400,
            "multi-parent",
            sizeof("multi-parent") - 1U,
            DEVICE_KIND_PHYSICAL,
            &operations,
            NULL
        ) ||
        !device_initialize_child(
            &first,
            &parent,
            401,
            "first-child",
            sizeof("first-child") - 1U,
            DEVICE_KIND_VIRTUAL,
            &operations,
            NULL
        ) ||
        !device_initialize_child(
            &second,
            &parent,
            402,
            "second-child",
            sizeof("second-child") - 1U,
            DEVICE_KIND_VIRTUAL,
            &operations,
            NULL
        )
    ) {
        kernel_panic(
            "Multiple GONE children fixture initialization failed"
        );
    }

    if (
        parent.reference_count != 3 ||
        parent.first_child !=
            &second ||
        second.next_sibling !=
            &first ||
        first.previous_sibling !=
            &second
    ) {
        kernel_panic(
            "Multiple-child initial topology mismatch"
        );
    }

    /*
     * Simulate independent consumers that already hold both children.
     */
    if (
        !device_retain(
            &first
        ) ||
        !device_retain(
            &second
        ) ||
        first.reference_count != 2 ||
        second.reference_count != 2
    ) {
        kernel_panic(
            "Unable to retain multiple child references"
        );
    }

    /*
     * Both children leave logical visibility while their existing holders keep
     * the objects alive.
     */
    if (
        !device_begin_removal(
            &first
        ) ||
        !device_finish_removal(
            &first
        ) ||
        !device_begin_removal(
            &second
        ) ||
        !device_finish_removal(
            &second
        ) ||
        first.state !=
            DEVICE_STATE_GONE ||
        second.state !=
            DEVICE_STATE_GONE
    ) {
        kernel_panic(
            "Unable to make multiple children logically GONE"
        );
    }

    /*
     * GONE children no longer block logical parent removal even though each
     * still owns one topology reference to it.
     */
    if (
        !device_begin_removal(
            &parent
        ) ||
        !device_finish_removal(
            &parent
        ) ||
        parent.state !=
            DEVICE_STATE_GONE
    ) {
        kernel_panic(
            "Multiple GONE children blocked parent removal"
        );
    }

    /*
     * Drop the parent's original owner. The two child topology references must
     * now be the only references keeping the GONE parent alive.
     */
    if (
        !device_release(
            &parent
        ) ||
        parent.reference_count != 2 ||
        device_test_destroy_count != 0
    ) {
        kernel_panic(
            "Multiple children failed to retain parent lifetime"
        );
    }

    /*
     * Drop the independent holder of each child. Both objects remain alive on
     * their final owning references.
     */
    if (
        !device_release(
            &first
        ) ||
        !device_release(
            &second
        ) ||
        first.reference_count != 1 ||
        second.reference_count != 1 ||
        parent.reference_count != 2 ||
        device_test_destroy_count != 0
    ) {
        kernel_panic(
            "Multiple child external-reference release mismatch"
        );
    }

    /*
     * Destroy the first child. Exactly one parent topology reference must
     * disappear, but the second child still keeps the parent alive.
     */
    if (!device_release(
        &first
    )) {
        kernel_panic(
            "First GONE child final release failed"
        );
    }

    if (
        device_test_destroy_count != 1 ||
        device_test_destroy_order_count != 1 ||
        device_test_destroy_order[0] != 401 ||
        parent.reference_count != 1 ||
        parent.first_child !=
            &second ||
        second.previous_sibling != NULL ||
        second.next_sibling != NULL
    ) {
        kernel_panic(
            "First GONE child teardown corrupted shared parent"
        );
    }

    /*
     * The last child destroys itself first and then releases the final parent
     * topology reference. That permits the already-GONE parent to die.
     */
    if (!device_release(
        &second
    )) {
        kernel_panic(
            "Second GONE child final release failed"
        );
    }

    if (
        device_test_destroy_count != 3 ||
        device_test_destroy_order_count != 3 ||
        device_test_destroy_order[0] != 401 ||
        device_test_destroy_order[1] != 402 ||
        device_test_destroy_order[2] != 400
    ) {
        kernel_panic(
            "Multiple GONE child destruction order mismatch"
        );
    }
}

static void device_test_child_initialization_errors(void)
{
    struct device parent;
    struct device candidate = {
        .identifier =
            0x1122334455667788ULL,
    };

    if (!device_initialize(
        &parent,
        300,
        "parent",
        sizeof("parent") - 1U,
        DEVICE_KIND_PHYSICAL,
        NULL,
        NULL
    )) {
        kernel_panic(
            "Device child-error parent initialization failed"
        );
    }

    size_t original_parent_references =
        parent.reference_count;

    if (
        device_initialize_child(
            &candidate,
            NULL,
            301,
            "child",
            sizeof("child") - 1U,
            DEVICE_KIND_VIRTUAL,
            NULL,
            NULL
        ) ||
        device_initialize_child(
            &candidate,
            &candidate,
            301,
            "child",
            sizeof("child") - 1U,
            DEVICE_KIND_VIRTUAL,
            NULL,
            NULL
        ) ||
        candidate.identifier !=
            0x1122334455667788ULL ||
        parent.reference_count !=
            original_parent_references
    ) {
        kernel_panic(
            "Device child initialization accepted invalid parent"
        );
    }

    /*
     * A parent that has begun removal cannot accept new children.
     */
    if (!device_begin_removal(
        &parent
    )) {
        kernel_panic(
            "Unable to prepare removing parent fixture"
        );
    }

    if (
        device_initialize_child(
            &candidate,
            &parent,
            301,
            "child",
            sizeof("child") - 1U,
            DEVICE_KIND_VIRTUAL,
            NULL,
            NULL
        ) ||
        candidate.identifier !=
            0x1122334455667788ULL ||
        parent.reference_count !=
            original_parent_references
    ) {
        kernel_panic(
            "REMOVING parent accepted new child"
        );
    }

    if (
        !device_finish_removal(
            &parent
        ) ||
        device_initialize_child(
            &candidate,
            &parent,
            301,
            "child",
            sizeof("child") - 1U,
            DEVICE_KIND_VIRTUAL,
            NULL,
            NULL
        ) ||
        candidate.identifier !=
            0x1122334455667788ULL ||
        parent.reference_count !=
            original_parent_references
    ) {
        kernel_panic(
            "GONE parent accepted new child"
        );
    }

    if (!device_release(
        &parent
    )) {
        kernel_panic(
            "Device child-error fixture cleanup failed"
        );
    }

    /*
     * A parent whose reference count cannot be increased must also leave child
     * storage untouched.
     */
    struct device saturated_parent;

    if (!device_initialize(
        &saturated_parent,
        302,
        "saturated",
        sizeof("saturated") - 1U,
        DEVICE_KIND_PHYSICAL,
        NULL,
        NULL
    )) {
        kernel_panic(
            "Device saturated-parent initialization failed"
        );
    }

    saturated_parent.reference_count =
        SIZE_MAX;

    if (
        device_initialize_child(
            &candidate,
            &saturated_parent,
            303,
            "child",
            sizeof("child") - 1U,
            DEVICE_KIND_VIRTUAL,
            NULL,
            NULL
        ) ||
        candidate.identifier !=
            0x1122334455667788ULL ||
        saturated_parent.reference_count !=
            SIZE_MAX
    ) {
        kernel_panic(
            "Device child initialization ignored parent retain failure"
        );
    }

    saturated_parent.reference_count =
        1;

    if (
        !device_begin_removal(
            &saturated_parent
        ) ||
        !device_finish_removal(
            &saturated_parent
        ) ||
        !device_release(
            &saturated_parent
        )
    ) {
        kernel_panic(
            "Device saturated-parent fixture cleanup failed"
        );
    }
}

static void device_test_invalid_state_rejection(void)
{
    struct device object;

    if (
        !device_initialize(
            &object,
            500,
            "invalid-state",
            sizeof("invalid-state") - 1U,
            DEVICE_KIND_VIRTUAL,
            NULL,
            NULL
        ) ||
        !device_retain(
            &object
        )
    ) {
        kernel_panic(
            "Device invalid-state fixture initialization failed"
        );
    }

    object.state =
        (enum device_state) 99;

    size_t references =
        object.reference_count;

    if (
        device_retain(
            &object
        ) ||
        device_release(
            &object
        ) ||
        device_begin_removal(
            &object
        ) ||
        device_finish_removal(
            &object
        ) ||
        object.reference_count !=
            references ||
        object.state !=
            (enum device_state) 99
    ) {
        kernel_panic(
            "Device invalid-state rejection mutated object"
        );
    }

    object.state =
        DEVICE_STATE_ACTIVE;

    if (
        !device_release(
            &object
        ) ||
        object.reference_count != 1 ||
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
            "Device invalid-state fixture cleanup failed"
        );
    }
}

static void device_test_sibling_unlink_positions(void)
{
    struct device parent;
    struct device first;
    struct device middle;
    struct device last;

    if (
        !device_initialize(
            &parent,
            510,
            "unlink-parent",
            sizeof("unlink-parent") - 1U,
            DEVICE_KIND_PHYSICAL,
            NULL,
            NULL
        ) ||
        !device_initialize_child(
            &first,
            &parent,
            511,
            "first",
            sizeof("first") - 1U,
            DEVICE_KIND_VIRTUAL,
            NULL,
            NULL
        ) ||
        !device_initialize_child(
            &middle,
            &parent,
            512,
            "middle",
            sizeof("middle") - 1U,
            DEVICE_KIND_VIRTUAL,
            NULL,
            NULL
        ) ||
        !device_initialize_child(
            &last,
            &parent,
            513,
            "last",
            sizeof("last") - 1U,
            DEVICE_KIND_VIRTUAL,
            NULL,
            NULL
        )
    ) {
        kernel_panic(
            "Device sibling-unlink fixture initialization failed"
        );
    }

    /*
     * Insertion occurs at the head:
     *
     * last <-> middle <-> first
     */
    if (
        parent.first_child !=
            &last ||
        last.previous_sibling !=
            NULL ||
        last.next_sibling !=
            &middle ||
        middle.previous_sibling !=
            &last ||
        middle.next_sibling !=
            &first ||
        first.previous_sibling !=
            &middle ||
        first.next_sibling !=
            NULL ||
        parent.reference_count != 4
    ) {
        kernel_panic(
            "Device sibling-unlink initial topology mismatch"
        );
    }

    /*
     * Remove the middle element.
     */
    if (
        !device_begin_removal(
            &middle
        ) ||
        !device_finish_removal(
            &middle
        ) ||
        !device_release(
            &middle
        )
    ) {
        kernel_panic(
            "Device middle-sibling removal failed"
        );
    }

    if (
        parent.first_child !=
            &last ||
        last.next_sibling !=
            &first ||
        first.previous_sibling !=
            &last ||
        parent.reference_count != 3
    ) {
        kernel_panic(
            "Device middle-sibling unlink mismatch"
        );
    }

    /*
     * Remove the head.
     */
    if (
        !device_begin_removal(
            &last
        ) ||
        !device_finish_removal(
            &last
        ) ||
        !device_release(
            &last
        )
    ) {
        kernel_panic(
            "Device head-sibling removal failed"
        );
    }

    if (
        parent.first_child !=
            &first ||
        first.previous_sibling !=
            NULL ||
        first.next_sibling !=
            NULL ||
        parent.reference_count != 2
    ) {
        kernel_panic(
            "Device head-sibling unlink mismatch"
        );
    }

    /*
     * Remove the remaining tail.
     */
    if (
        !device_begin_removal(
            &first
        ) ||
        !device_finish_removal(
            &first
        ) ||
        !device_release(
            &first
        )
    ) {
        kernel_panic(
            "Device tail-sibling removal failed"
        );
    }

    if (
        parent.first_child != NULL ||
        parent.reference_count != 1
    ) {
        kernel_panic(
            "Device tail-sibling unlink mismatch"
        );
    }

    if (
        !device_begin_removal(
            &parent
        ) ||
        !device_finish_removal(
            &parent
        ) ||
        !device_release(
            &parent
        )
    ) {
        kernel_panic(
            "Device sibling-unlink fixture cleanup failed"
        );
    }
}

static void device_test_corrupt_topology_rejection(void)
{
    struct device parent;
    struct device child;

    struct device candidate = {
        .identifier =
            0xaabbccddeeff0011ULL,
    };

    if (
        !device_initialize(
            &parent,
            520,
            "corrupt-parent",
            sizeof("corrupt-parent") - 1U,
            DEVICE_KIND_PHYSICAL,
            NULL,
            NULL
        ) ||
        !device_initialize_child(
            &child,
            &parent,
            521,
            "corrupt-child",
            sizeof("corrupt-child") - 1U,
            DEVICE_KIND_VIRTUAL,
            NULL,
            NULL
        )
    ) {
        kernel_panic(
            "Device corrupt-topology fixture initialization failed"
        );
    }

    /*
     * Manufacture a self-cycle from valid object pointers. The device core
     * must detect it rather than traversing forever or mutating the topology.
     */
    child.previous_sibling =
        &child;

    child.next_sibling =
        &child;

    size_t parent_references =
        parent.reference_count;

    if (
        device_initialize_child(
            &candidate,
            &parent,
            522,
            "candidate",
            sizeof("candidate") - 1U,
            DEVICE_KIND_VIRTUAL,
            NULL,
            NULL
        ) ||
        candidate.identifier !=
            0xaabbccddeeff0011ULL ||
        parent.reference_count !=
            parent_references ||
        parent.first_child !=
            &child
    ) {
        kernel_panic(
            "Device attached child to corrupt topology"
        );
    }

    if (
        device_begin_removal(
            &parent
        ) ||
        parent.state !=
            DEVICE_STATE_ACTIVE ||
        parent.reference_count !=
            parent_references
    ) {
        kernel_panic(
            "Device removed parent with corrupt child topology"
        );
    }

    /*
     * A child may complete its own logical removal because its children are
     * independent of its sibling membership. Its final destruction, however,
     * must reject the corrupt parent link without changing ownership.
     */
    if (
        !device_begin_removal(
            &child
        ) ||
        !device_finish_removal(
            &child
        ) ||
        device_release(
            &child
        ) ||
        child.reference_count != 1 ||
        parent.reference_count !=
            parent_references ||
        parent.first_child !=
            &child
    ) {
        kernel_panic(
            "Device final release accepted corrupt parent topology"
        );
    }

    /*
     * Restore the synthetic corruption and prove normal teardown still works.
     */
    child.previous_sibling =
        NULL;

    child.next_sibling =
        NULL;

    if (
        !device_release(
            &child
        ) ||
        parent.first_child !=
            NULL ||
        parent.reference_count != 1 ||
        !device_begin_removal(
            &parent
        ) ||
        !device_finish_removal(
            &parent
        ) ||
        !device_release(
            &parent
        )
    ) {
        kernel_panic(
            "Device corrupt-topology fixture cleanup failed"
        );
    }
}

static void device_test_destroy_parent_lifetime(void)
{
    struct device parent;
    struct device child;

    struct device_operations child_operations = {
        .destroy =
            device_test_destroy_check_parent,
    };

    device_test_expected_destroy_parent =
        &parent;

    device_test_destroy_saw_live_parent =
        false;

    if (
        !device_initialize(
            &parent,
            530,
            "callback-parent",
            sizeof("callback-parent") - 1U,
            DEVICE_KIND_PHYSICAL,
            NULL,
            NULL
        ) ||
        !device_initialize_child(
            &child,
            &parent,
            531,
            "callback-child",
            sizeof("callback-child") - 1U,
            DEVICE_KIND_VIRTUAL,
            &child_operations,
            NULL
        )
    ) {
        kernel_panic(
            "Device destroy-parent fixture initialization failed"
        );
    }

    if (
        !device_begin_removal(
            &child
        ) ||
        !device_finish_removal(
            &child
        ) ||
        !device_begin_removal(
            &parent
        ) ||
        !device_finish_removal(
            &parent
        )
    ) {
        kernel_panic(
            "Device destroy-parent fixture removal failed"
        );
    }

    /*
     * Remove the parent's original ownership. The child topology reference is
     * now the only thing keeping the parent object alive.
     */
    if (
        !device_release(
            &parent
        ) ||
        parent.reference_count != 1
    ) {
        kernel_panic(
            "Device destroy-parent ownership setup failed"
        );
    }

    if (!device_release(
        &child
    )) {
        kernel_panic(
            "Device child destroy callback release failed"
        );
    }

    if (
        !device_test_destroy_saw_live_parent ||
        parent.reference_count != 0
    ) {
        kernel_panic(
            "Device parent lifetime did not cover child destroy callback"
        );
    }

    device_test_expected_destroy_parent =
        NULL;
}
