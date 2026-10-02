// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file driver.h
 * @brief Generic kernel driver matching and lifetime-independent metadata.
 *
 * A driver is caller-owned. Registering it with a bus does not transfer
 * storage ownership; the driver object must remain alive until it has been
 * unregistered from that bus.
 *
 * Driver callbacks are synchronous in the initial device model.
 *
 * probe() must either establish complete driver ownership and return true, or
 * unwind every resource acquired during the failed attempt before returning
 * false.
 *
 * remove() must release all driver-owned state associated with the device.
 */

#ifndef MYOS_CORE_DEVICE_DRIVER_H
#define MYOS_CORE_DEVICE_DRIVER_H

#include "device.h"

#include <stdbool.h>
#include <stddef.h>

#define DRIVER_NAME_MAX 63U

struct bus;
struct driver;

struct driver_operations {
    bool (*match)(
        const struct driver *driver,
        const struct device *device
    );

    bool (*probe)(
        struct driver *driver,
        struct device *device
    );

    void (*remove)(
        struct driver *driver,
        struct device *device
    );
};

struct driver {
    char name[
        DRIVER_NAME_MAX + 1U
    ];

    size_t name_length;

    /*
     * Non-owning while registered. NULL means the driver is currently
     * unregistered.
     */
    struct bus *bus;

    const struct driver_operations *operations;

    void *private_data;
};

bool driver_initialize(
    struct driver *driver,
    const char *name,
    size_t name_length,
    const struct driver_operations *operations,
    void *private_data
);

#endif
