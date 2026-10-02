// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file pseudo.h
 * @brief Core pseudo character devices.
 *
 * null and zero are kernel-created pseudo devices. They participate in the
 * generic device registry and expose character-device capabilities, but they
 * are not discovered through a hardware bus.
 *
 * VFS publication is deliberately outside this module. Device existence and
 * pathname visibility remain separate concerns.
 */

#ifndef MYOS_DRIVERS_PSEUDO_H
#define MYOS_DRIVERS_PSEUDO_H

#include "../core/device/character_device.h"
#include "../core/device/device.h"
#include "../core/device/registry.h"

#include <stdint.h>

/*
 * Stable kernel-internal identities for the bootstrap pseudo devices.
 *
 * These values are not userspace major/minor numbers and do not form part of
 * the userspace ABI.
 */
#define PSEUDO_DEVICE_NULL_IDENTIFIER 1ULL
#define PSEUDO_DEVICE_ZERO_IDENTIFIER 2ULL

enum pseudo_devices_initialize_result {
    PSEUDO_DEVICES_INITIALIZE_RESULT_INITIALIZED,
    PSEUDO_DEVICES_INITIALIZE_RESULT_INVALID_ARGUMENT,
    PSEUDO_DEVICES_INITIALIZE_RESULT_REGISTRY_REJECTED,
    PSEUDO_DEVICES_INITIALIZE_RESULT_ROLLBACK_FAILED,
};

/**
 * One generic device plus its character-device capability.
 *
 * character borrows device. Ownership of the generic device remains with the
 * containing pseudo_devices object and, while published, the registry.
 */
struct pseudo_character_device {
    struct device device;
    struct character_device character;
};

/**
 * Caller-owned storage for the initial kernel pseudo devices.
 *
 * On successful initialization:
 *
 * - each embedded device owns its original reference;
 * - registry owns one additional reference to each device;
 * - each character capability borrows its corresponding generic device.
 */
struct pseudo_devices {
    struct pseudo_character_device null_device;
    struct pseudo_character_device zero_device;
};

/**
 * Initializes and publishes the core null and zero pseudo devices.
 *
 * registry must already be initialized. devices supplies permanent caller
 * storage and must remain alive while either device is registered or otherwise
 * referenced.
 *
 * A partial registration failure is rolled back. Existing registry entries
 * are never removed by this function.
 */
enum pseudo_devices_initialize_result pseudo_devices_initialize(
    struct pseudo_devices *devices,
    struct device_registry *registry
);

#endif
