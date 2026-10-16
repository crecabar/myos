// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#ifndef MYOS_BOOT_INITRAMFS_H
#define MYOS_BOOT_INITRAMFS_H

#include "boot.h"

#include <stdbool.h>

/**
 * Finds the boot module designated as the initramfs.
 *
 * Exactly one module must carry the command line "initramfs".
 *
 * @param boot_info Normalized boot information.
 * @param module Receives the matching module.
 *
 * @return true when exactly one initramfs module exists; false otherwise.
 */
bool boot_initramfs_find(
    const struct boot_info *boot_info,
    const struct boot_module **module
);

#endif
