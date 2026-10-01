// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file device.c
 * @brief Generic kernel device identity, topology, and lifetime implementation.
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

static bool device_initialize_arguments_valid(
    struct device *device,
    uint64_t identifier,
    const char *name,
    size_t name_length,
    enum device_kind kind
);

static void device_initialize_validated(
    struct device *device,
    uint64_t identifier,
    const char *name,
    size_t name_length,
    enum device_kind kind,
    const struct device_operations *operations,
    void *private_data
);

static bool device_children_removed(
    const struct device *device
);

static bool device_parent_link_valid(
    const struct device *device
);

static void device_unlink_from_parent(
    struct device *device
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
    if (!device_initialize_arguments_valid(
        device,
        identifier,
        name,
        name_length,
        kind
    )) {
        return false;
    }

    device_initialize_validated(
        device,
        identifier,
        name,
        name_length,
        kind,
        operations,
        private_data
    );

    return true;
}

bool device_initialize_child(
    struct device *device,
    struct device *parent,
    uint64_t identifier,
    const char *name,
    size_t name_length,
    enum device_kind kind,
    const struct device_operations *operations,
    void *private_data)
{
    if (
        !device_initialize_arguments_valid(
            device,
            identifier,
            name,
            name_length,
            kind
        ) ||
        parent == NULL ||
        parent == device ||
        parent->reference_count == 0 ||
        !device_state_valid(
            parent->state
        ) ||
        parent->state !=
            DEVICE_STATE_ACTIVE
    ) {
        return false;
    }

    /*
     * The child owns one parent reference for its entire object lifetime.
     * Acquire it before mutating child storage so a failed retain leaves the
     * requested initialization completely unobservable.
     */
    if (!device_retain(
        parent
    )) {
        return false;
    }

    device_initialize_validated(
        device,
        identifier,
        name,
        name_length,
        kind,
        operations,
        private_data
    );

    device->parent =
        parent;

    device->next_sibling =
        parent->first_child;

    if (
        parent->first_child != NULL
    ) {
        parent->first_child->previous_sibling =
            device;
    }

    parent->first_child =
        device;

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

    if (
        device->reference_count == 1 &&
        device->state !=
            DEVICE_STATE_GONE
    ) {
        return false;
    }

    /*
     * A correctly maintained topology cannot leave children attached when the
     * final reference to their parent is about to disappear: each child owns
     * one parent reference.
     *
     * Treat this as invalid state instead of producing dangling topology.
     */
    if (
        device->reference_count == 1 &&
        device->first_child != NULL
    ) {
        return false;
    }

    if (
        device->reference_count != 1
    ) {
        --device->reference_count;

        return true;
    }

    /*
     * From here onward this is the final release of a GONE device.
     *
     * Validate the parent link before mutating anything. The topology-held
     * parent reference must itself be releasable after the child's destroy
     * callback returns.
     */
    if (!device_parent_link_valid(
        device
    )) {
        return false;
    }

    struct device *parent =
        device->parent;

    const struct device_operations *operations =
        device->operations;

    device_unlink_from_parent(
        device
    );

    device->reference_count =
        0;

    if (
        operations != NULL &&
        operations->destroy != NULL
    ) {
        operations->destroy(
            device
        );
    }

    /*
     * destroy() may have released the child's storage. parent was captured
     * beforehand and remains live because the child still owns that reference
     * until this exact point.
     */
    if (
        parent != NULL &&
        !device_release(
            parent
        )
    ) {
        /*
         * This indicates corruption of an invariant maintained entirely by
         * the device core. The child lifetime has already ended, so there is
         * no state that can be rolled back here.
         *
         * Under the current serialized model this path is unreachable for
         * topology created through device_initialize_child().
         */
        return false;
    }

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

    /*
     * Logical topology must be removed bottom-up. A child that has reached
     * GONE may remain allocated and linked while outstanding references drain;
     * its parent reference keeps this object alive during that interval.
     */
    if (!device_children_removed(
        device
    )) {
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

static bool device_initialize_arguments_valid(
    struct device *device,
    uint64_t identifier,
    const char *name,
    size_t name_length,
    enum device_kind kind)
{
    return
        device != NULL &&
        identifier !=
            DEVICE_IDENTIFIER_INVALID &&
        device_name_valid(
            name,
            name_length
        ) &&
        device_kind_valid(
            kind
        );
}

static void device_initialize_validated(
    struct device *device,
    uint64_t identifier,
    const char *name,
    size_t name_length,
    enum device_kind kind,
    const struct device_operations *operations,
    void *private_data)
{
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

    device->parent =
        NULL;

    device->first_child =
        NULL;

    device->previous_sibling =
        NULL;

    device->next_sibling =
        NULL;

    device->operations =
        operations;

    device->private_data =
        private_data;
}

static bool device_children_removed(
    const struct device *device)
{
    if (device == NULL) {
        return false;
    }

    for (
        const struct device *child =
            device->first_child;
        child != NULL;
        child = child->next_sibling
    ) {
        if (
            child->parent != device ||
            child->reference_count == 0 ||
            !device_state_valid(
                child->state
            ) ||
            child->state !=
                DEVICE_STATE_GONE
        ) {
            return false;
        }
    }

    return true;
}

static bool device_parent_link_valid(
    const struct device *device)
{
    if (device == NULL) {
        return false;
    }

    if (device->parent == NULL) {
        return
            device->previous_sibling == NULL &&
            device->next_sibling == NULL;
    }

    const struct device *parent =
        device->parent;

    if (
        parent->reference_count == 0 ||
        !device_state_valid(
            parent->state
        )
    ) {
        return false;
    }

    /*
     * Releasing the child's topology ownership must be possible after the
     * child's destroy callback.
     */
    if (
        parent->reference_count == 1 &&
        parent->state !=
            DEVICE_STATE_GONE
    ) {
        return false;
    }

    if (
        device->previous_sibling == NULL
    ) {
        if (
            parent->first_child !=
            device
        ) {
            return false;
        }
    } else {
        if (
            device->previous_sibling->parent !=
                parent ||
            device->previous_sibling->next_sibling !=
                device
        ) {
            return false;
        }
    }

    if (
        device->next_sibling != NULL &&
        (
            device->next_sibling->parent !=
                parent ||
            device->next_sibling->previous_sibling !=
                device
        )
    ) {
        return false;
    }

    return true;
}

static void device_unlink_from_parent(
    struct device *device)
{
    if (
        device == NULL ||
        device->parent == NULL
    ) {
        return;
    }

    struct device *parent =
        device->parent;

    if (
        device->previous_sibling != NULL
    ) {
        device->previous_sibling->next_sibling =
            device->next_sibling;
    } else {
        parent->first_child =
            device->next_sibling;
    }

    if (
        device->next_sibling != NULL
    ) {
        device->next_sibling->previous_sibling =
            device->previous_sibling;
    }

    /*
     * parent intentionally remains intact through destroy(). It is still
     * backed by the topology-owned reference captured by device_release().
     *
     * Sibling membership, however, has ended before destruction begins.
     */
    device->previous_sibling =
        NULL;

    device->next_sibling =
        NULL;
}
