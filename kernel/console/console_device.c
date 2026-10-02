// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file console_device.c
 * @brief System console character-device implementation.
 */

#include "console_device.h"

#include <stddef.h>
#include <stdint.h>

// Private functions and helpers declarations
static struct console_device *console_device_from_character(
    struct character_device *character
);

static bool console_device_retire_unpublished(
    struct console_device *console_device
);

static enum character_device_io_result console_device_write(
    struct character_device *character,
    const void *buffer,
    size_t size,
    size_t *bytes_written
);

// Static local variables
static const struct character_device_operations
    console_device_character_operations = {
        .read =
            NULL,
        .write =
            console_device_write,
    };

// Public functions implementations
enum console_device_initialize_result console_device_initialize(
    struct console_device *console_device,
    struct console *console,
    struct device_registry *registry)
{
    if (
        console_device == NULL ||
        console == NULL ||
        console->framebuffer == NULL ||
        registry == NULL
    ) {
        return
            CONSOLE_DEVICE_INITIALIZE_RESULT_INVALID_ARGUMENT;
    }

    if (!device_initialize(
        &console_device->device,
        CONSOLE_DEVICE_IDENTIFIER,
        "console",
        sizeof("console") - 1U,
        DEVICE_KIND_VIRTUAL,
        NULL,
        NULL
    )) {
        return
            CONSOLE_DEVICE_INITIALIZE_RESULT_INVALID_ARGUMENT;
    }

    console_device->console =
        console;

    if (!character_device_initialize(
        &console_device->character,
        &console_device->device,
        &console_device_character_operations,
        console_device
    )) {
        console_device->console =
            NULL;

        if (!console_device_retire_unpublished(
            console_device
        )) {
            return
                CONSOLE_DEVICE_INITIALIZE_RESULT_ROLLBACK_FAILED;
        }

        return
            CONSOLE_DEVICE_INITIALIZE_RESULT_INVALID_ARGUMENT;
    }

    enum device_registry_register_result register_result =
        device_registry_register(
            registry,
            &console_device->device
        );

    if (
        register_result !=
            DEVICE_REGISTRY_REGISTER_RESULT_REGISTERED
    ) {
        if (!console_device_retire_unpublished(
            console_device
        )) {
            return
                CONSOLE_DEVICE_INITIALIZE_RESULT_ROLLBACK_FAILED;
        }

        console_device->console =
            NULL;

        return
            CONSOLE_DEVICE_INITIALIZE_RESULT_REGISTRY_REJECTED;
    }

    return
        CONSOLE_DEVICE_INITIALIZE_RESULT_INITIALIZED;
}

// Private functions and helpers implementations
static struct console_device *console_device_from_character(
    struct character_device *character)
{
    if (
        character == NULL ||
        character->private_data == NULL
    ) {
        return NULL;
    }

    struct console_device *console_device =
        character->private_data;

    if (
        &console_device->character !=
            character ||
        character->device !=
            &console_device->device ||
        console_device->console == NULL ||
        console_device->console->framebuffer == NULL ||
        console_device->device.reference_count == 0 ||
        console_device->device.state !=
            DEVICE_STATE_ACTIVE
    ) {
        return NULL;
    }

    return
        console_device;
}

static bool console_device_retire_unpublished(
    struct console_device *console_device)
{
    if (
        console_device == NULL ||
        console_device->device.reference_count != 1 ||
        console_device->device.state !=
            DEVICE_STATE_ACTIVE
    ) {
        return false;
    }

    return
        device_begin_removal(
            &console_device->device
        ) &&
        device_finish_removal(
            &console_device->device
        ) &&
        device_release(
            &console_device->device
        );
}

static enum character_device_io_result console_device_write(
    struct character_device *character,
    const void *buffer,
    size_t size,
    size_t *bytes_written)
{
    struct console_device *console_device =
        console_device_from_character(
            character
        );

    if (
        console_device == NULL ||
        bytes_written == NULL ||
        (
            size != 0 &&
            buffer == NULL
        )
    ) {
        return
            CHARACTER_DEVICE_IO_RESULT_INVALID_ARGUMENT;
    }

    /*
     * Character-device writes are byte streams, not C strings. In
     * particular, an embedded NUL must not terminate the transfer.
     */
    const uint8_t *bytes =
        buffer;

    for (
        size_t index = 0;
        index < size;
        ++index
    ) {
        console_put_char(
            console_device->console,
            (char) bytes[index]
        );
    }

    *bytes_written =
        size;

    return
        CHARACTER_DEVICE_IO_RESULT_SUCCESS;
}
