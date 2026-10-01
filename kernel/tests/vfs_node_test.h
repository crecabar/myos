// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file vfs_node_test.h
 * @brief VFS node ownership and lifetime regression tests.
 */

#ifndef MYOS_TESTS_VFS_NODE_TEST_H
#define MYOS_TESTS_VFS_NODE_TEST_H

/**
 * Verifies the VFS node identity and reference-lifetime contract.
 */
void vfs_node_test_run(void);

#endif
