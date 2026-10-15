// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file blocking_read.c
 * @brief Ring-3 blocking descriptor read regression.
 *
 * Two instances of this executable are launched:
 *
 * - reader: reads one byte from descriptor 0;
 * - writer: writes one byte to descriptor 1.
 *
 * The kernel test supplies both descriptors through
 * an ordinary VFS backend with read notifications.
 */

#include <myos/file.h>

#include <stddef.h>
#include <stdint.h>

#define BLOCKING_READ_TEST_BYTE 0x4dU

static int blocking_read_reader(void)
{
    uint8_t byte = 0xa5U;

    syscall_result_t result =
        myos_fd_read(
            0U,
            &byte,
            sizeof(byte)
        );

    if (result != 1) {
        return 11;
    }

    if (byte != BLOCKING_READ_TEST_BYTE) {
        return 12;
    }

    if (myos_fd_close(0U) != 0) {
        return 13;
    }

    return 0;
}

static int blocking_read_writer(void)
{
    const uint8_t byte =
        BLOCKING_READ_TEST_BYTE;

    syscall_result_t result =
        myos_fd_write(
            1U,
            &byte,
            sizeof(byte)
        );

    if (result != 1) {
        return 21;
    }

    if (myos_fd_close(1U) != 0) {
        return 22;
    }

    return 0;
}

int main(
    int argc,
    char *argv[],
    char *envp[])
{
    (void) envp;

    if (
        argc != 2 ||
        argv == NULL ||
        argv[1] == NULL
    ) {
        return 1;
    }

    if (
        argv[1][0] == 'r' &&
        argv[1][1] == '\0'
    ) {
        return blocking_read_reader();
    }

    if (
        argv[1][0] == 'w' &&
        argv[1][1] == '\0'
    ) {
        return blocking_read_writer();
    }

    return 2;
}
