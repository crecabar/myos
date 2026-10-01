// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file vfs_file_test.h
 * @brief VFS open-file ownership and lifetime regression tests.
 */

#ifndef MYOS_TESTS_VFS_FILE_TEST_H
#define MYOS_TESTS_VFS_FILE_TEST_H

/**
 * Verifies the VFS open-file description and reference-lifetime contract.
 */
void vfs_file_test_run(void);

#endif
