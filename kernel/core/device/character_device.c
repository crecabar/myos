// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file character_device.c
 * @brief Generic character-device capability implementation.
 */

#include "character_device.h"

#include <stdbool.h>
#include <stddef.h>

// Private functions and helpers declarations
static bool character_device_valid(
    const struct character_device *character
);

static bool character_device_io_result_valid(
    enum character_device_io_result result
);

// Public functions implementations
bool character_device_initialize(
    struct character_device *character,
    struct device *device,
    const struct character_device_operations *operations,
    void *private_data)
{
    if (
        character == NULL ||
        device == NULL ||
        device->reference_count == 0 ||
        device->state !=
            DEVICE_STATE_ACTIVE ||
        operations == NULL ||
        (
            operations->read == NULL &&
            operations->write == NULL
        )
    ) {
        return false;
    }

    character->device =
        device;

    character->operations =
        operations;

    character->private_data =
        private_data;

    return true;
}

bool character_device_can_read(
    const struct character_device *character)
{
    return
        character_device_valid(
            character
        ) &&
        character->operations->read !=
            NULL;
}

bool character_device_can_write(
    const struct character_device *character)
{
    return
        character_device_valid(
            character
        ) &&
        character->operations->write !=
            NULL;
}

enum character_device_io_result character_device_read(
    struct character_device *character,
    void *buffer,
    size_t size,
    size_t *bytes_read)
{
    if (
        bytes_read == NULL ||
        !character_device_valid(
            character
        )
    ) {
        return
            CHARACTER_DEVICE_IO_RESULT_INVALID_ARGUMENT;
    }

    if (
        character->operations->read ==
            NULL
    ) {
        return
            CHARACTER_DEVICE_IO_RESULT_NOT_SUPPORTED;
    }

    /*
     * Capability validation occurs before the zero-length shortcut. An object
     * with no read operation therefore remains non-readable even for a
     * zero-byte request.
     */
    if (size == 0) {
        *bytes_read =
            0;

        return
            CHARACTER_DEVICE_IO_RESULT_SUCCESS;
    }

    if (buffer == NULL) {
        return
            CHARACTER_DEVICE_IO_RESULT_INVALID_ARGUMENT;
    }

    size_t transferred =
        0;

    enum character_device_io_result io_result =
        character->operations->read(
            character,
            buffer,
            size,
            &transferred
        );

    if (!character_device_io_result_valid(
        io_result
    )) {
        return
            CHARACTER_DEVICE_IO_RESULT_INVALID_ARGUMENT;
    }

    if (
        io_result !=
            CHARACTER_DEVICE_IO_RESULT_SUCCESS
    ) {
        return
            io_result;
    }

    if (transferred > size) {
        return
            CHARACTER_DEVICE_IO_RESULT_INVALID_ARGUMENT;
    }

    *bytes_read =
        transferred;

    return
        CHARACTER_DEVICE_IO_RESULT_SUCCESS;
}

enum character_device_io_result character_device_write(
    struct character_device *character,
    const void *buffer,
    size_t size,
    size_t *bytes_written)
{
    if (
        bytes_written == NULL ||
        !character_device_valid(
            character
        )
    ) {
        return
            CHARACTER_DEVICE_IO_RESULT_INVALID_ARGUMENT;
    }

    if (
        character->operations->write ==
            NULL
    ) {
        return
            CHARACTER_DEVICE_IO_RESULT_NOT_SUPPORTED;
    }

    if (size == 0) {
        *bytes_written =
            0;

        return
            CHARACTER_DEVICE_IO_RESULT_SUCCESS;
    }

    if (buffer == NULL) {
        return
            CHARACTER_DEVICE_IO_RESULT_INVALID_ARGUMENT;
    }

    size_t transferred =
        0;

    enum character_device_io_result io_result =
        character->operations->write(
            character,
            buffer,
            size,
            &transferred
        );

    if (!character_device_io_result_valid(
        io_result
    )) {
        return
            CHARACTER_DEVICE_IO_RESULT_INVALID_ARGUMENT;
    }

    if (
        io_result !=
            CHARACTER_DEVICE_IO_RESULT_SUCCESS
    ) {
        return
            io_result;
    }

    if (transferred > size) {
        return
            CHARACTER_DEVICE_IO_RESULT_INVALID_ARGUMENT;
    }

    *bytes_written =
        transferred;

    return
        CHARACTER_DEVICE_IO_RESULT_SUCCESS;
}

// Private functions and helpers implementations
static bool character_device_valid(
    const struct character_device *character)
{
    if (
        character == NULL ||
        character->device == NULL ||
        character->operations == NULL ||
        character->device->reference_count == 0 ||
        character->device->state !=
            DEVICE_STATE_ACTIVE
    ) {
        return false;
    }

    return
        character->operations->read !=
            NULL ||
        character->operations->write !=
            NULL;
}

static bool character_device_io_result_valid(
    enum character_device_io_result result)
{
    switch (result) {
        case CHARACTER_DEVICE_IO_RESULT_SUCCESS:
        case CHARACTER_DEVICE_IO_RESULT_INVALID_ARGUMENT:
        case CHARACTER_DEVICE_IO_RESULT_NOT_SUPPORTED:
        case CHARACTER_DEVICE_IO_RESULT_RESOURCE_EXHAUSTED:
            return true;
    }

    return false;
}
