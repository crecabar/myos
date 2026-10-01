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
static size_t device_test_destroy_count;
static void *device_test_destroy_private_data;
static uint64_t device_test_destroy_identifier;

// Private functions and helpers declarations
static void device_test_destroy(
    struct device *device
);

static bool device_test_name_equal(
    const struct device *device,
    const char *name,
    size_t name_length
);

static void device_test_initialization(void);

static void device_test_invalid_initialization(void);

static void device_test_reference_lifetime(void);

static void device_test_state_transitions(void);

static void device_test_optional_destroy(void);

static void device_test_reference_overflow(void);

// Public functions implementations
void device_test_run(void)
{
    device_test_initialization();
    device_test_invalid_initialization();
    device_test_reference_lifetime();
    device_test_state_transitions();
    device_test_optional_destroy();
    device_test_reference_overflow();

    diagnostics_write(
        "[device] Generic device lifetime tests passed\n"
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
