// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file test_numbers.h
 * @brief Kernel-test-only syscall numbers.
 *
 * These numbers are private to the kernel regression harness and are not part
 * of the published MyOS userspace ABI.
 */

#ifndef MYOS_SYSCALL_TEST_NUMBERS_H
#define MYOS_SYSCALL_TEST_NUMBERS_H

#define SYSCALL_TEST_SLEEP         0x100
#define SYSCALL_TEST_BLOCK         0x101
#define SYSCALL_TEST_EXEC          0x102
#define SYSCALL_TEST_CONTEXT_GATE  0x103

#endif
