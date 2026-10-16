// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file runtime_malloc.c
 * @brief Ring-3 regression for bootstrap malloc and free.
 */

#include <stdlib.h>

#include <string.h>

#include <stddef.h>
#include <stdint.h>

int main(
    int argc,
    char *argv[],
    char *envp[])
{
    (void) argc;
    (void) argv;
    (void) envp;

    if (malloc(0) != NULL) {
        return 1;
    }

    if (malloc(SIZE_MAX) != NULL) {
        return 2;
    }

    size_t alignment =
        _Alignof(max_align_t);

    for (
        size_t size = 1;
        size <= 64;
        ++size
    ) {
        void *pointer =
            malloc(size);

        if (pointer == NULL) {
            return 3;
        }

        if (
            (uintptr_t) pointer %
                alignment !=
            0
        ) {
            return 4;
        }

        memset(
            pointer,
            0xa5,
            size
        );

        const unsigned char *bytes =
            pointer;

        for (
            size_t index = 0;
            index < size;
            ++index
        ) {
            if (bytes[index] != 0xa5) {
                return 5;
            }
        }

        free(pointer);
    }

    void *original =
        malloc(512);

    if (original == NULL) {
        return 6;
    }

    uintptr_t original_address =
        (uintptr_t) original;

    free(original);

    void *first =
        malloc(128);

    if (
        first == NULL ||
        (uintptr_t) first !=
            original_address
    ) {
        return 7;
    }

    void *second =
        malloc(128);

    if (second == NULL) {
        return 8;
    }

    uintptr_t first_address =
        (uintptr_t) first;

    uintptr_t second_address =
        (uintptr_t) second;

    if (
        second_address <=
            first_address ||
        second_address >=
            original_address + 512
    ) {
        return 9;
    }

    memset(
        first,
        0x11,
        128
    );

    memset(
        second,
        0x22,
        128
    );

    const unsigned char *first_bytes =
        first;

    const unsigned char *second_bytes =
        second;

    for (
        size_t index = 0;
        index < 128;
        ++index
    ) {
        if (
            first_bytes[index] !=
                0x11 ||
            second_bytes[index] !=
                0x22
        ) {
            return 10;
        }
    }

    free(first);
    free(second);

    free(NULL);

    void *after_null_free =
        malloc(64);

    if (after_null_free == NULL) {
        return 11;
    }

    free(after_null_free);

    void *blocks[64];
    size_t block_count = 0;

    while (block_count < 64) {
        void *block =
            malloc(256);

        if (block == NULL) {
            break;
        }

        blocks[block_count] =
            block;

        ++block_count;
    }

    if (
        block_count == 0 ||
        block_count == 64
    ) {
        return 12;
    }

    if (malloc(256) != NULL) {
        return 13;
    }

    for (
        size_t index = 0;
        index < block_count;
        ++index
    ) {
        free(blocks[index]);
    }

    void *coalesced =
        malloc(16000);

    if (coalesced == NULL) {
        return 14;
    }

    free(coalesced);

    block_count = 0;

    while (block_count < 64) {
        void *block =
            malloc(256);

        if (block == NULL) {
            break;
        }

        blocks[block_count] =
            block;

        ++block_count;
    }

    if (
        block_count == 0 ||
        block_count == 64
    ) {
        return 15;
    }

    if (malloc(256) != NULL) {
        return 16;
    }

    for (
        size_t remaining = block_count;
        remaining > 0;
        --remaining
    ) {
        free(
            blocks[
                remaining - 1
            ]
        );
    }

    coalesced =
        malloc(16000);

    if (coalesced == NULL) {
        return 17;
    }

    free(coalesced);

    return 0;
}
