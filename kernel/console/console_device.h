// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file console_device.h
 * @brief System console character-device implementation.
 *
 * The console device exposes the kernel's visible text console through the
 * generic device and character-device contracts.
 *
 * This initial bootstrap implementation is intentionally write-only.
 * Keyboard input, blocking reads, terminal semantics, line discipline, ioctl,
 * and controlling-terminal behavior remain outside this layer.
 *
 * VFS publication is also deliberately separate: device existence is not a
 * pathname.
 */

#ifndef MYOS_CONSOLE_CONSOLE_DEVICE_H
#define MYOS_CONSOLE_CONSOLE_DEVICE_H

#include "console.h"

#include "../core/device/character_device.h"
#include "../core/device/device.h"
#include "../core/device/registry.h"

#include <stdint.h>

/*
 * Stable kernel-internal identity for the system console device.
 *
 * This is not a userspace major/minor number and does not form part of the
 * userspace ABI.
 */
#define CONSOLE_DEVICE_IDENTIFIER 3ULL

enum console_device_initialize_result {
    CONSOLE_DEVICE_INITIALIZE_RESULT_INITIALIZED,
    CONSOLE_DEVICE_INITIALIZE_RESULT_INVALID_ARGUMENT,
    CONSOLE_DEVICE_INITIALIZE_RESULT_REGISTRY_REJECTED,
    CONSOLE_DEVICE_INITIALIZE_RESULT_ROLLBACK_FAILED,
};

/**
 * Caller-owned system-console device.
 *
 * character borrows device.
 * console is borrowed and must outlive this object while it remains reachable.
 *
 * The generic device retains its original caller-owned reference and, while
 * published, the registry owns one additional reference.
 */
struct console_device {
    struct device device;

    struct character_device character;

    struct console *console;
};

/**
 * Initializes and publishes one write-only system console device.
 *
 * console must refer to an initialized visible console with a framebuffer.
 * registry must already be initialized.
 *
 * On success:
 *
 * - device is ACTIVE with its original owning reference;
 * - registry owns one additional reference;
 * - character exposes write but not read;
 * - console remains borrowed.
 *
 * A registry publication failure retires the newly initialized device and
 * leaves the registry unchanged.
 */
enum console_device_initialize_result console_device_initialize(
    struct console_device *console_device,
    struct console *console,
    struct device_registry *registry
);

#endif
