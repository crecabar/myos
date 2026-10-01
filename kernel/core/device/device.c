// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file device.c
 * @brief Generic kernel device identity and lifetime implementation.
 */

#include "device.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Private functions and helpers declarations
static bool device_kind_valid(
    enum device_kind kind
);

static bool device_state_valid(
    enum device_state state
);

static bool device_name_valid(
    const char *name,
    size_t name_length
);

// Public functions implementations
bool device_initialize(
    struct device *device,
    uint64_t identifier,
    const char *name,
    size_t name_length,
    enum device_kind kind,
    const struct device_operations *operations,
    void *private_data)
{
    if (
        device == NULL ||
        identifier ==
            DEVICE_IDENTIFIER_INVALID ||
        !device_name_valid(
            name,
            name_length
        ) ||
        !device_kind_valid(
            kind
        )
    ) {
        return false;
    }

    device->identifier =
        identifier;

    for (
        size_t index = 0;
        index < name_length;
        ++index
    ) {
        device->name[index] =
            name[index];
    }

    device->name[
        name_length
    ] = '\0';

    device->name_length =
        name_length;

    device->kind =
        kind;

    device->state =
        DEVICE_STATE_ACTIVE;

    device->reference_count =
        1;

    device->operations =
        operations;

    device->private_data =
        private_data;

    return true;
}

bool device_retain(
    struct device *device)
{
    if (
        device == NULL ||
        device->reference_count == 0 ||
        device->reference_count ==
            SIZE_MAX ||
        !device_state_valid(
            device->state
        )
    ) {
        return false;
    }

    if (
        device->state !=
        DEVICE_STATE_ACTIVE
    ) {
        return false;
    }

    ++device->reference_count;

    return true;
}

bool device_release(
    struct device *device)
{
    if (
        device == NULL ||
        device->reference_count == 0 ||
        !device_state_valid(
            device->state
        )
    ) {
        return false;
    }

    /*
     * The final reference represents ownership of the device object itself.
     * Destroying an ACTIVE or still-REMOVING object would bypass the explicit
     * removal protocol and could leave it published elsewhere.
     */
    if (
        device->reference_count == 1 &&
        device->state !=
            DEVICE_STATE_GONE
    ) {
        return false;
    }

    --device->reference_count;

    if (
        device->reference_count != 0
    ) {
        return true;
    }

    const struct device_operations *operations =
        device->operations;

    if (
        operations != NULL &&
        operations->destroy != NULL
    ) {
        operations->destroy(
            device
        );
    }

    /*
     * destroy() may have released the storage containing device.
     */
    return true;
}

bool device_begin_removal(
    struct device *device)
{
    if (
        device == NULL ||
        device->reference_count == 0 ||
        !device_state_valid(
            device->state
        )
    ) {
        return false;
    }

    if (
        device->state !=
        DEVICE_STATE_ACTIVE
    ) {
        return false;
    }

    device->state =
        DEVICE_STATE_REMOVING;

    return true;
}

bool device_finish_removal(
    struct device *device)
{
    if (
        device == NULL ||
        device->reference_count == 0 ||
        !device_state_valid(
            device->state
        )
    ) {
        return false;
    }

    if (
        device->state !=
        DEVICE_STATE_REMOVING
    ) {
        return false;
    }

    device->state =
        DEVICE_STATE_GONE;

    return true;
}

// Private functions and helpers implementations
static bool device_kind_valid(
    enum device_kind kind)
{
    switch (kind) {
        case DEVICE_KIND_PHYSICAL:
        case DEVICE_KIND_VIRTUAL:
        case DEVICE_KIND_PSEUDO:
            return true;
    }

    return false;
}

static bool device_state_valid(
    enum device_state state)
{
    switch (state) {
        case DEVICE_STATE_ACTIVE:
        case DEVICE_STATE_REMOVING:
        case DEVICE_STATE_GONE:
            return true;
    }

    return false;
}

static bool device_name_valid(
    const char *name,
    size_t name_length)
{
    if (
        name == NULL ||
        name_length == 0 ||
        name_length >
            DEVICE_NAME_MAX
    ) {
        return false;
    }

    for (
        size_t index = 0;
        index < name_length;
        ++index
    ) {
        if (
            name[index] == '\0' ||
            name[index] == '/'
        ) {
            return false;
        }
    }

    return true;
}
