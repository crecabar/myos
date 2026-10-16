// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file character_device.h
 * @brief VFS adapter for generic character devices.
 *
 * This module exposes one character_device through the ordinary VFS node and
 * open-file contracts.
 *
 * Device identity and I/O remain owned by the device model. The adapter does
 * not assign pathnames and has no knowledge of filesystems, /dev, processes,
 * descriptors, or syscalls.
 */

#ifndef MYOS_VFS_CHARACTER_DEVICE_H
#define MYOS_VFS_CHARACTER_DEVICE_H

#include "vfs.h"

#include "../core/device/character_device.h"

#include <stdbool.h>

/**
 * Caller-owned VFS representation of one character device.
 *
 * character is borrowed and must outlive this adapter.
 *
 * While the VFS node is live, the adapter owns one reference to
 * character->device. That reference is independent of registry publication
 * and is released when the VFS node reaches its final release.
 */
struct vfs_character_device_node {
    struct vfs_node vfs;

    struct character_device *character;
};

/**
 * Initializes one VFS character-device node.
 *
 * character must describe a live ACTIVE device and expose at least one I/O
 * capability.
 *
 * On success:
 *
 * - node receives one initial VFS ownership reference;
 * - node retains one independent generic-device reference;
 * - character itself remains borrowed.
 */
bool vfs_character_device_node_initialize(
    struct vfs_character_device_node *node,
    struct character_device *character
);

#endif
