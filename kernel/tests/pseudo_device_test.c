// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file pseudo_device_test.c
 * @brief null/zero character-device and registry regression tests.
 */

#include "pseudo_device_test.h"

#include "../core/device/character_device.h"
#include "../core/device/device.h"
#include "../core/device/registry.h"
#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../drivers/pseudo.h"

#include <stddef.h>
#include <stdint.h>

// Private functions and helpers declarations
static void pseudo_device_test_unregister(
    struct device_registry *registry,
    struct device *device
);

static void pseudo_device_test_registration_and_io(void);

static void pseudo_device_test_partial_registration_rollback(void);

// Public functions implementations
void pseudo_device_test_run(void)
{
    pseudo_device_test_registration_and_io();
    pseudo_device_test_partial_registration_rollback();

    diagnostics_write(
        "[device] Core null/zero pseudo-device tests passed\n"
    );
}

// Private functions and helpers implementations
static void pseudo_device_test_unregister(
    struct device_registry *registry,
    struct device *device)
{
    if (
        registry == NULL ||
        device == NULL
    ) {
        kernel_panic(
            "Pseudo-device cleanup received invalid arguments"
        );
    }

    struct device *detached =
        NULL;

    if (
        device_registry_unregister(
            registry,
            device,
            &detached
        ) !=
            DEVICE_REGISTRY_UNREGISTER_RESULT_UNREGISTERED ||
        detached !=
            device ||
        detached->state !=
            DEVICE_STATE_REMOVING ||
        !device_finish_removal(
            detached
        ) ||
        !device_release(
            detached
        ) ||
        !device_release(
            device
        )
    ) {
        kernel_panic(
            "Pseudo-device fixture cleanup failed"
        );
    }
}

static void pseudo_device_test_registration_and_io(void)
{
    struct device_registry registry;
    struct pseudo_devices devices;

    if (
        !device_registry_initialize(
            &registry
        ) ||
        pseudo_devices_initialize(
            &devices,
            &registry
        ) !=
            PSEUDO_DEVICES_INITIALIZE_RESULT_INITIALIZED
    ) {
        kernel_panic(
            "Pseudo-device initialization failed"
        );
    }

    if (
        registry.count != 2 ||
        devices.null_device.device.identifier !=
            PSEUDO_DEVICE_NULL_IDENTIFIER ||
        devices.zero_device.device.identifier !=
            PSEUDO_DEVICE_ZERO_IDENTIFIER ||
        devices.null_device.device.kind !=
            DEVICE_KIND_PSEUDO ||
        devices.zero_device.device.kind !=
            DEVICE_KIND_PSEUDO ||
        devices.null_device.device.state !=
            DEVICE_STATE_ACTIVE ||
        devices.zero_device.device.state !=
            DEVICE_STATE_ACTIVE ||
        devices.null_device.device.reference_count != 2 ||
        devices.zero_device.device.reference_count != 2 ||
        devices.null_device.character.device !=
            &devices.null_device.device ||
        devices.zero_device.character.device !=
            &devices.zero_device.device ||
        !character_device_can_read(
            &devices.null_device.character
        ) ||
        !character_device_can_write(
            &devices.null_device.character
        ) ||
        !character_device_can_read(
            &devices.zero_device.character
        ) ||
        !character_device_can_write(
            &devices.zero_device.character
        )
    ) {
        kernel_panic(
            "Pseudo-device published state is invalid"
        );
    }

    struct device *lookup =
        NULL;

    if (
        device_registry_lookup_name(
            &registry,
            "null",
            sizeof("null") - 1U,
            &lookup
        ) !=
            DEVICE_REGISTRY_LOOKUP_RESULT_FOUND ||
        lookup !=
            &devices.null_device.device ||
        lookup->reference_count != 3 ||
        !device_release(
            lookup
        ) ||
        devices.null_device.device.reference_count != 2
    ) {
        kernel_panic(
            "null registry publication contract failed"
        );
    }

    lookup =
        NULL;

    if (
        device_registry_lookup_name(
            &registry,
            "zero",
            sizeof("zero") - 1U,
            &lookup
        ) !=
            DEVICE_REGISTRY_LOOKUP_RESULT_FOUND ||
        lookup !=
            &devices.zero_device.device ||
        lookup->reference_count != 3 ||
        !device_release(
            lookup
        ) ||
        devices.zero_device.device.reference_count != 2
    ) {
        kernel_panic(
            "zero registry publication contract failed"
        );
    }

    uint8_t null_buffer[4] = {
        0x11U,
        0x22U,
        0x33U,
        0x44U,
    };

    size_t transferred =
        SIZE_MAX;

    if (
        character_device_read(
            &devices.null_device.character,
            null_buffer,
            sizeof(null_buffer),
            &transferred
        ) !=
            CHARACTER_DEVICE_IO_RESULT_SUCCESS ||
        transferred != 0 ||
        null_buffer[0] != 0x11U ||
        null_buffer[1] != 0x22U ||
        null_buffer[2] != 0x33U ||
        null_buffer[3] != 0x44U
    ) {
        kernel_panic(
            "null read semantics failed"
        );
    }

    transferred =
        SIZE_MAX;

    if (
        character_device_write(
            &devices.null_device.character,
            null_buffer,
            sizeof(null_buffer),
            &transferred
        ) !=
            CHARACTER_DEVICE_IO_RESULT_SUCCESS ||
        transferred !=
            sizeof(null_buffer)
    ) {
        kernel_panic(
            "null write semantics failed"
        );
    }

    uint8_t zero_buffer[8] = {
        0xa1U,
        0xa2U,
        0xa3U,
        0xa4U,
        0xb1U,
        0xb2U,
        0xb3U,
        0xb4U,
    };

    transferred =
        SIZE_MAX;

    if (
        character_device_read(
            &devices.zero_device.character,
            zero_buffer,
            4,
            &transferred
        ) !=
            CHARACTER_DEVICE_IO_RESULT_SUCCESS ||
        transferred != 4 ||
        zero_buffer[0] != 0 ||
        zero_buffer[1] != 0 ||
        zero_buffer[2] != 0 ||
        zero_buffer[3] != 0 ||
        zero_buffer[4] != 0xb1U ||
        zero_buffer[5] != 0xb2U ||
        zero_buffer[6] != 0xb3U ||
        zero_buffer[7] != 0xb4U
    ) {
        kernel_panic(
            "zero read semantics failed"
        );
    }

    transferred =
        SIZE_MAX;

    if (
        character_device_write(
            &devices.zero_device.character,
            zero_buffer,
            sizeof(zero_buffer),
            &transferred
        ) !=
            CHARACTER_DEVICE_IO_RESULT_SUCCESS ||
        transferred !=
            sizeof(zero_buffer)
    ) {
        kernel_panic(
            "zero write semantics failed"
        );
    }

    /*
     * Remove in reverse publication order. The pseudo devices are independent
     * roots, but reverse order keeps teardown deterministic.
     */
    pseudo_device_test_unregister(
        &registry,
        &devices.zero_device.device
    );

    pseudo_device_test_unregister(
        &registry,
        &devices.null_device.device
    );

    if (
        registry.count != 0 ||
        devices.null_device.device.reference_count != 0 ||
        devices.zero_device.device.reference_count != 0
    ) {
        kernel_panic(
            "Pseudo-device cleanup leaked ownership"
        );
    }
}

static void pseudo_device_test_partial_registration_rollback(void)
{
    struct device_registry registry;

    if (!device_registry_initialize(
        &registry
    )) {
        kernel_panic(
            "Pseudo rollback registry initialization failed"
        );
    }

    /*
     * Reserve the internal name "zero". null must initially publish
     * successfully, zero must then be rejected, and the pseudo-device
     * initializer must restore the registry to this exact preexisting state.
     */
    struct device preexisting;

    if (
        !device_initialize(
            &preexisting,
            6500,
            "zero",
            sizeof("zero") - 1U,
            DEVICE_KIND_VIRTUAL,
            NULL,
            NULL
        ) ||
        device_registry_register(
            &registry,
            &preexisting
        ) !=
            DEVICE_REGISTRY_REGISTER_RESULT_REGISTERED
    ) {
        kernel_panic(
            "Pseudo rollback preexisting device setup failed"
        );
    }

    uint64_t generation_before =
        registry.generation;

    struct pseudo_devices devices;

    if (
        pseudo_devices_initialize(
            &devices,
            &registry
        ) !=
            PSEUDO_DEVICES_INITIALIZE_RESULT_REGISTRY_REJECTED ||
        registry.count != 1 ||
        registry.devices[0] !=
            &preexisting ||
        preexisting.state !=
            DEVICE_STATE_ACTIVE ||
        preexisting.reference_count != 2 ||
        devices.null_device.device.state !=
            DEVICE_STATE_GONE ||
        devices.null_device.device.reference_count != 0 ||
        devices.zero_device.device.state !=
            DEVICE_STATE_GONE ||
        devices.zero_device.device.reference_count != 0
    ) {
        kernel_panic(
            "Pseudo-device partial registration rollback failed"
        );
    }

    /*
     * null was published and then withdrawn. zero never published because its
     * name conflicted with the preexisting object.
     */
    if (
        registry.generation !=
            generation_before + 2ULL
    ) {
        kernel_panic(
            "Pseudo-device rollback mutated registry unexpectedly"
        );
    }

    pseudo_device_test_unregister(
        &registry,
        &preexisting
    );

    if (registry.count != 0) {
        kernel_panic(
            "Pseudo rollback fixture cleanup failed"
        );
    }
}
