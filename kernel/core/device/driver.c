// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file driver.c
 * @brief Generic kernel driver initialization.
 */

#include "driver.h"

#include <stdbool.h>
#include <stddef.h>

// Private functions and helpers declarations
static bool driver_name_valid(
    const char *name,
    size_t name_length
);

// Public functions implementations
bool driver_initialize(
    struct driver *driver,
    const char *name,
    size_t name_length,
    const struct driver_operations *operations,
    void *private_data)
{
    if (
        driver == NULL ||
        !driver_name_valid(
            name,
            name_length
        ) ||
        operations == NULL ||
        operations->match == NULL ||
        operations->probe == NULL ||
        operations->remove == NULL
    ) {
        return false;
    }

    for (
        size_t index = 0;
        index < name_length;
        ++index
    ) {
        driver->name[index] =
            name[index];
    }

    driver->name[
        name_length
    ] = '\0';

    driver->name_length =
        name_length;

    driver->bus =
        NULL;

    driver->operations =
        operations;

    driver->private_data =
        private_data;

    return true;
}

// Private functions and helpers implementations
static bool driver_name_valid(
    const char *name,
    size_t name_length)
{
    if (
        name == NULL ||
        name_length == 0 ||
        name_length >
            DRIVER_NAME_MAX
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
