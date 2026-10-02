// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file console_device_test.c
 * @brief System console character-device regression tests.
 */

#include "console_device_test.h"

#include "../console/console.h"
#include "../console/console_device.h"
#include "../core/device/character_device.h"
#include "../core/device/device.h"
#include "../core/device/registry.h"
#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../drivers/framebuffer.h"

#include <stddef.h>
#include <stdint.h>

#define CONSOLE_DEVICE_TEST_WIDTH 64U
#define CONSOLE_DEVICE_TEST_HEIGHT 32U

// Private functions and helpers declarations
static void console_device_test_unregister(
    struct device_registry *registry,
    struct device *device
);

static void console_device_test_registration_and_write(void);

static void console_device_test_registration_rollback(void);

// Public functions implementations
void console_device_test_run(void)
{
    console_device_test_registration_and_write();
    console_device_test_registration_rollback();

    diagnostics_write(
        "[device] System console device tests passed\n"
    );
}

// Private functions and helpers implementations
static void console_device_test_unregister(
    struct device_registry *registry,
    struct device *device)
{
    if (
        registry == NULL ||
        device == NULL
    ) {
        kernel_panic(
            "Console-device cleanup received invalid arguments"
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
            "Console-device fixture cleanup failed"
        );
    }
}

static void console_device_test_registration_and_write(void)
{
    static uint32_t pixels[
        CONSOLE_DEVICE_TEST_WIDTH *
        CONSOLE_DEVICE_TEST_HEIGHT
    ];

    for (
        size_t index = 0;
        index <
            CONSOLE_DEVICE_TEST_WIDTH *
            CONSOLE_DEVICE_TEST_HEIGHT;
        ++index
    ) {
        pixels[index] =
            0;
    }

    struct framebuffer framebuffer = {
        .address =
            pixels,
        .width =
            CONSOLE_DEVICE_TEST_WIDTH,
        .height =
            CONSOLE_DEVICE_TEST_HEIGHT,
        .pitch =
            CONSOLE_DEVICE_TEST_WIDTH *
            sizeof(uint32_t),
        .bpp =
            32,
        .red_mask_size =
            8,
        .red_mask_shift =
            16,
        .green_mask_size =
            8,
        .green_mask_shift =
            8,
        .blue_mask_size =
            8,
        .blue_mask_shift =
            0,
    };

    struct console console;

    console_init(
        &console,
        &framebuffer,
        0,
        0,
        0x00ffffffU,
        1
    );

    struct device_registry registry;

    if (!device_registry_initialize(
        &registry
    )) {
        kernel_panic(
            "Console-device registry initialization failed"
        );
    }

    struct console_device device;

    if (
        console_device_initialize(
            &device,
            &console,
            &registry
        ) !=
            CONSOLE_DEVICE_INITIALIZE_RESULT_INITIALIZED
    ) {
        kernel_panic(
            "Console-device initialization failed"
        );
    }

    if (
        registry.count != 1 ||
        device.device.identifier !=
            CONSOLE_DEVICE_IDENTIFIER ||
        device.device.kind !=
            DEVICE_KIND_VIRTUAL ||
        device.device.state !=
            DEVICE_STATE_ACTIVE ||
        device.device.reference_count != 2 ||
        device.character.device !=
            &device.device ||
        device.console !=
            &console ||
        character_device_can_read(
            &device.character
        ) ||
        !character_device_can_write(
            &device.character
        )
    ) {
        kernel_panic(
            "Console-device published state is invalid"
        );
    }

    struct device *lookup =
        NULL;

    if (
        device_registry_lookup_name(
            &registry,
            "console",
            sizeof("console") - 1U,
            &lookup
        ) !=
            DEVICE_REGISTRY_LOOKUP_RESULT_FOUND ||
        lookup !=
            &device.device ||
        lookup->reference_count != 3 ||
        !device_release(
            lookup
        ) ||
        device.device.reference_count != 2
    ) {
        kernel_panic(
            "Console-device registry lookup contract failed"
        );
    }

    /*
     * A missing read capability is explicit. In particular, an empty input
     * queue is not represented as EOF by this bootstrap implementation.
     */
    uint8_t read_buffer =
        0xa5U;

    size_t bytes_read =
        SIZE_MAX;

    if (
        character_device_read(
            &device.character,
            &read_buffer,
            sizeof(read_buffer),
            &bytes_read
        ) !=
            CHARACTER_DEVICE_IO_RESULT_NOT_SUPPORTED ||
        bytes_read !=
            SIZE_MAX ||
        read_buffer !=
            0xa5U
    ) {
        kernel_panic(
            "Console-device unexpectedly supported reads"
        );
    }

    /*
     * Embedded NUL proves that character-device writes are length-delimited
     * byte streams rather than C strings.
     *
     * At scale 1 each ordinary byte advances the cursor by 8 glyph pixels plus
     * one spacing pixel, regardless of whether a glyph exists.
     */
    static const uint8_t payload[] = {
        'A',
        0,
        'B',
    };

    size_t bytes_written =
        SIZE_MAX;

    if (
        character_device_write(
            &device.character,
            payload,
            sizeof(payload),
            &bytes_written
        ) !=
            CHARACTER_DEVICE_IO_RESULT_SUCCESS ||
        bytes_written !=
            sizeof(payload) ||
        console.cursor_x !=
            27 ||
        console.cursor_y !=
            0
    ) {
        kernel_panic(
            "Console-device byte-stream write failed"
        );
    }

    /*
     * Supported zero-length writes succeed in the generic character layer
     * without invoking the backend or requiring a buffer.
     */
    uint64_t cursor_x_before =
        console.cursor_x;

    uint64_t cursor_y_before =
        console.cursor_y;

    bytes_written =
        SIZE_MAX;

    if (
        character_device_write(
            &device.character,
            NULL,
            0,
            &bytes_written
        ) !=
            CHARACTER_DEVICE_IO_RESULT_SUCCESS ||
        bytes_written != 0 ||
        console.cursor_x !=
            cursor_x_before ||
        console.cursor_y !=
            cursor_y_before
    ) {
        kernel_panic(
            "Console-device zero-length write contract failed"
        );
    }

    console_device_test_unregister(
        &registry,
        &device.device
    );

    if (
        registry.count != 0 ||
        device.device.reference_count != 0
    ) {
        kernel_panic(
            "Console-device cleanup leaked ownership"
        );
    }
}

static void console_device_test_registration_rollback(void)
{
    static uint32_t pixels[
        CONSOLE_DEVICE_TEST_WIDTH *
        CONSOLE_DEVICE_TEST_HEIGHT
    ];

    struct framebuffer framebuffer = {
        .address =
            pixels,
        .width =
            CONSOLE_DEVICE_TEST_WIDTH,
        .height =
            CONSOLE_DEVICE_TEST_HEIGHT,
        .pitch =
            CONSOLE_DEVICE_TEST_WIDTH *
            sizeof(uint32_t),
        .bpp =
            32,
        .red_mask_size =
            8,
        .red_mask_shift =
            16,
        .green_mask_size =
            8,
        .green_mask_shift =
            8,
        .blue_mask_size =
            8,
        .blue_mask_shift =
            0,
    };

    struct console console;

    console_init(
        &console,
        &framebuffer,
        0,
        0,
        0x00ffffffU,
        1
    );

    struct device_registry registry;

    if (!device_registry_initialize(
        &registry
    )) {
        kernel_panic(
            "Console rollback registry initialization failed"
        );
    }

    /*
     * Reserve the canonical internal name. The console initializer must fail
     * publication without mutating or withdrawing the preexisting device.
     */
    struct device preexisting;

    if (
        !device_initialize(
            &preexisting,
            6501,
            "console",
            sizeof("console") - 1U,
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
            "Console rollback preexisting device setup failed"
        );
    }

    uint64_t generation_before =
        registry.generation;

    struct console_device console_device;

    if (
        console_device_initialize(
            &console_device,
            &console,
            &registry
        ) !=
            CONSOLE_DEVICE_INITIALIZE_RESULT_REGISTRY_REJECTED ||
        registry.count != 1 ||
        registry.devices[0] !=
            &preexisting ||
        registry.generation !=
            generation_before ||
        preexisting.state !=
            DEVICE_STATE_ACTIVE ||
        preexisting.reference_count != 2 ||
        console_device.device.state !=
            DEVICE_STATE_GONE ||
        console_device.device.reference_count != 0
    ) {
        kernel_panic(
            "Console-device registration rollback failed"
        );
    }

    console_device_test_unregister(
        &registry,
        &preexisting
    );

    if (registry.count != 0) {
        kernel_panic(
            "Console rollback fixture cleanup failed"
        );
    }
}
