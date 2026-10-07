// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file process_executable_test.h
 * @brief VFS-backed executable source regression tests.
 */

#ifndef MYOS_TESTS_PROCESS_EXECUTABLE_TEST_H
#define MYOS_TESTS_PROCESS_EXECUTABLE_TEST_H

#include "../vfs/mount.h"

void process_executable_test_run(
    const struct vfs_mount_table *mounts
);

#endif
