// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file pseudo.c
 * @brief Core null and zero pseudo-device implementation.
 */

#include "pseudo.h"

#include <stddef.h>
#include <stdint.h>

// Private functions and helpers declarations
static struct pseudo_character_device *pseudo_device_from_character(
    struct character_device *character
);

static bool pseudo_device_initialize(
    struct pseudo_character_device *pseudo,
    uint64_t identifier,
    const char *name,
    size_t name_length,
    const struct character_device_operations *operations
);

static bool pseudo_device_retire_unpublished(
    struct pseudo_character_device *pseudo
);

static bool pseudo_device_unpublish(
    struct pseudo_character_device *pseudo,
    struct device_registry *registry
);

static enum character_device_io_result pseudo_null_read(
    struct character_device *character,
    void *buffer,
    size_t size,
    size_t *bytes_read
);

static enum character_device_io_result pseudo_null_write(
    struct character_device *character,
    const void *buffer,
    size_t size,
    size_t *bytes_written
);

static enum character_device_io_result pseudo_zero_read(
    struct character_device *character,
    void *buffer,
    size_t size,
    size_t *bytes_read
);

static enum character_device_io_result pseudo_zero_write(
    struct character_device *character,
    const void *buffer,
    size_t size,
    size_t *bytes_written
);

// Static local variables
static const struct character_device_operations pseudo_null_operations = {
    .read =
        pseudo_null_read,
    .write =
        pseudo_null_write,
};

static const struct character_device_operations pseudo_zero_operations = {
    .read =
        pseudo_zero_read,
    .write =
        pseudo_zero_write,
};

// Public functions implementations
enum pseudo_devices_initialize_result pseudo_devices_initialize(
    struct pseudo_devices *devices,
    struct device_registry *registry)
{
    if (
        devices == NULL ||
        registry == NULL
    ) {
        return
            PSEUDO_DEVICES_INITIALIZE_RESULT_INVALID_ARGUMENT;
    }

    if (!pseudo_device_initialize(
        &devices->null_device,
        PSEUDO_DEVICE_NULL_IDENTIFIER,
        "null",
        sizeof("null") - 1U,
        &pseudo_null_operations
    )) {
        return
            PSEUDO_DEVICES_INITIALIZE_RESULT_INVALID_ARGUMENT;
    }

    if (!pseudo_device_initialize(
        &devices->zero_device,
        PSEUDO_DEVICE_ZERO_IDENTIFIER,
        "zero",
        sizeof("zero") - 1U,
        &pseudo_zero_operations
    )) {
        if (!pseudo_device_retire_unpublished(
            &devices->null_device
        )) {
            return
                PSEUDO_DEVICES_INITIALIZE_RESULT_ROLLBACK_FAILED;
        }

        return
            PSEUDO_DEVICES_INITIALIZE_RESULT_INVALID_ARGUMENT;
    }

    enum device_registry_register_result null_result =
        device_registry_register(
            registry,
            &devices->null_device.device
        );

    if (
        null_result !=
            DEVICE_REGISTRY_REGISTER_RESULT_REGISTERED
    ) {
        bool zero_clean =
            pseudo_device_retire_unpublished(
                &devices->zero_device
            );

        bool null_clean =
            pseudo_device_retire_unpublished(
                &devices->null_device
            );

        if (
            !zero_clean ||
            !null_clean
        ) {
            return
                PSEUDO_DEVICES_INITIALIZE_RESULT_ROLLBACK_FAILED;
        }

        return
            PSEUDO_DEVICES_INITIALIZE_RESULT_REGISTRY_REJECTED;
    }

    enum device_registry_register_result zero_result =
        device_registry_register(
            registry,
            &devices->zero_device.device
        );

    if (
        zero_result !=
            DEVICE_REGISTRY_REGISTER_RESULT_REGISTERED
    ) {
        bool zero_clean =
            pseudo_device_retire_unpublished(
                &devices->zero_device
            );

        bool null_clean =
            pseudo_device_unpublish(
                &devices->null_device,
                registry
            );

        if (
            !zero_clean ||
            !null_clean
        ) {
            return
                PSEUDO_DEVICES_INITIALIZE_RESULT_ROLLBACK_FAILED;
        }

        return
            PSEUDO_DEVICES_INITIALIZE_RESULT_REGISTRY_REJECTED;
    }

    return
        PSEUDO_DEVICES_INITIALIZE_RESULT_INITIALIZED;
}

// Private functions and helpers implementations
static struct pseudo_character_device *pseudo_device_from_character(
    struct character_device *character)
{
    if (
        character == NULL ||
        character->private_data == NULL
    ) {
        return NULL;
    }

    struct pseudo_character_device *pseudo =
        character->private_data;

    if (
        &pseudo->character !=
            character ||
        character->device !=
            &pseudo->device ||
        pseudo->device.reference_count == 0 ||
        pseudo->device.state !=
            DEVICE_STATE_ACTIVE
    ) {
        return NULL;
    }

    return
        pseudo;
}

static bool pseudo_device_initialize(
    struct pseudo_character_device *pseudo,
    uint64_t identifier,
    const char *name,
    size_t name_length,
    const struct character_device_operations *operations)
{
    if (
        pseudo == NULL ||
        operations == NULL
    ) {
        return false;
    }

    if (!device_initialize(
        &pseudo->device,
        identifier,
        name,
        name_length,
        DEVICE_KIND_PSEUDO,
        NULL,
        NULL
    )) {
        return false;
    }

    if (!character_device_initialize(
        &pseudo->character,
        &pseudo->device,
        operations,
        pseudo
    )) {
        if (
            !device_begin_removal(
                &pseudo->device
            ) ||
            !device_finish_removal(
                &pseudo->device
            ) ||
            !device_release(
                &pseudo->device
            )
        ) {
            return false;
        }

        return false;
    }

    return true;
}

static bool pseudo_device_retire_unpublished(
    struct pseudo_character_device *pseudo)
{
    if (
        pseudo == NULL ||
        pseudo->device.reference_count != 1 ||
        pseudo->device.state !=
            DEVICE_STATE_ACTIVE
    ) {
        return false;
    }

    return
        device_begin_removal(
            &pseudo->device
        ) &&
        device_finish_removal(
            &pseudo->device
        ) &&
        device_release(
            &pseudo->device
        );
}

static bool pseudo_device_unpublish(
    struct pseudo_character_device *pseudo,
    struct device_registry *registry)
{
    if (
        pseudo == NULL ||
        registry == NULL
    ) {
        return false;
    }

    struct device *detached =
        NULL;

    if (
        device_registry_unregister(
            registry,
            &pseudo->device,
            &detached
        ) !=
            DEVICE_REGISTRY_UNREGISTER_RESULT_UNREGISTERED ||
        detached !=
            &pseudo->device ||
        pseudo->device.state !=
            DEVICE_STATE_REMOVING
    ) {
        return false;
    }

    if (!device_finish_removal(
        detached
    )) {
        return false;
    }

    /*
     * Drop the ownership transferred out of the registry first. The original
     * pseudo-device owner remains until the final release below.
     */
    if (!device_release(
        detached
    )) {
        return false;
    }

    return
        device_release(
            &pseudo->device
        );
}

static enum character_device_io_result pseudo_null_read(
    struct character_device *character,
    void *buffer,
    size_t size,
    size_t *bytes_read)
{
    if (
        pseudo_device_from_character(
            character
        ) == NULL ||
        bytes_read == NULL ||
        (
            size != 0 &&
            buffer == NULL
        )
    ) {
        return
            CHARACTER_DEVICE_IO_RESULT_INVALID_ARGUMENT;
    }

    /*
     * null is permanently at EOF. The supplied buffer is intentionally
     * untouched.
     */
    *bytes_read =
        0;

    return
        CHARACTER_DEVICE_IO_RESULT_SUCCESS;
}

static enum character_device_io_result pseudo_null_write(
    struct character_device *character,
    const void *buffer,
    size_t size,
    size_t *bytes_written)
{
    if (
        pseudo_device_from_character(
            character
        ) == NULL ||
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
     * Every byte written to null is consumed immediately.
     */
    *bytes_written =
        size;

    return
        CHARACTER_DEVICE_IO_RESULT_SUCCESS;
}

static enum character_device_io_result pseudo_zero_read(
    struct character_device *character,
    void *buffer,
    size_t size,
    size_t *bytes_read)
{
    if (
        pseudo_device_from_character(
            character
        ) == NULL ||
        bytes_read == NULL ||
        (
            size != 0 &&
            buffer == NULL
        )
    ) {
        return
            CHARACTER_DEVICE_IO_RESULT_INVALID_ARGUMENT;
    }

    uint8_t *bytes =
        buffer;

    for (
        size_t index = 0;
        index < size;
        ++index
    ) {
        bytes[index] =
            0;
    }

    *bytes_read =
        size;

    return
        CHARACTER_DEVICE_IO_RESULT_SUCCESS;
}

static enum character_device_io_result pseudo_zero_write(
    struct character_device *character,
    const void *buffer,
    size_t size,
    size_t *bytes_written)
{
    if (
        pseudo_device_from_character(
            character
        ) == NULL ||
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
     * Writes to zero have the same discard behavior as writes to null.
     */
    *bytes_written =
        size;

    return
        CHARACTER_DEVICE_IO_RESULT_SUCCESS;
}
