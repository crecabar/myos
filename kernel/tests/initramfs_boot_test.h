// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file initramfs_boot_test.h
 * @brief Boot initramfs root filesystem integration regression.
 */

#ifndef MYOS_TESTS_INITRAMFS_BOOT_TEST_H
#define MYOS_TESTS_INITRAMFS_BOOT_TEST_H

struct vfs_mount_table;

void initramfs_boot_test_run(
    const struct vfs_mount_table *mounts
);

#endif
