// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file malloc.c
 * @brief Fixed-arena bootstrap userspace allocator.
 */

#include <stdlib.h>

#include <string.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RUNTIME_HEAP_SIZE (16U * 1024U)
#define RUNTIME_HEAP_END SIZE_MAX

/**
 * Metadata stored before each allocation range in the bootstrap heap.
 *
 * Headers are serialized into the byte arena with memcpy() rather than
 * accessed through cast structure pointers. This keeps the arena as genuine
 * byte storage while still providing structured allocator metadata.
 */
struct runtime_heap_block_header {
    size_t payload_size;
    size_t next_offset;
    bool free;
};

_Alignas(max_align_t)
static unsigned char runtime_heap[
    RUNTIME_HEAP_SIZE
];

_Static_assert(
    RUNTIME_HEAP_SIZE %
        _Alignof(max_align_t) ==
        0,
    "bootstrap heap size must preserve maximum alignment"
);

static bool runtime_heap_align_size(
    size_t size,
    size_t *aligned_size
);

static bool runtime_heap_header_span(
    size_t *span
);

static bool runtime_heap_header_read(
    size_t offset,
    struct runtime_heap_block_header *header
);

static bool runtime_heap_header_write(
    size_t offset,
    const struct runtime_heap_block_header *header
);

static bool runtime_heap_initialize(void);

static bool runtime_heap_align_size(
    size_t size,
    size_t *aligned_size)
{
    if (aligned_size == NULL) {
        return false;
    }

    size_t alignment =
        _Alignof(max_align_t);

    size_t remainder =
        size % alignment;

    if (remainder == 0) {
        *aligned_size =
            size;

        return true;
    }

    size_t padding =
        alignment - remainder;

    if (
        size >
        SIZE_MAX - padding
    ) {
        return false;
    }

    *aligned_size =
        size + padding;

    return true;
}

static bool runtime_heap_header_span(
    size_t *span)
{
    if (span == NULL) {
        return false;
    }

    return runtime_heap_align_size(
        sizeof(struct runtime_heap_block_header),
        span
    );
}

static bool runtime_heap_header_read(
    size_t offset,
    struct runtime_heap_block_header *header)
{
    if (header == NULL) {
        return false;
    }

    if (
        offset >
        RUNTIME_HEAP_SIZE
    ) {
        return false;
    }

    if (
        sizeof(*header) >
        RUNTIME_HEAP_SIZE - offset
    ) {
        return false;
    }

    memcpy(
        header,
        &runtime_heap[offset],
        sizeof(*header)
    );

    return true;
}

static bool runtime_heap_header_write(
    size_t offset,
    const struct runtime_heap_block_header *header)
{
    if (header == NULL) {
        return false;
    }

    if (
        offset >
        RUNTIME_HEAP_SIZE
    ) {
        return false;
    }

    if (
        sizeof(*header) >
        RUNTIME_HEAP_SIZE - offset
    ) {
        return false;
    }

    memcpy(
        &runtime_heap[offset],
        header,
        sizeof(*header)
    );

    return true;
}

static bool runtime_heap_initialize(void)
{
    size_t header_span;

    if (!runtime_heap_header_span(
        &header_span
    )) {
        return false;
    }

    if (
        header_span >=
        RUNTIME_HEAP_SIZE
    ) {
        return false;
    }

    struct runtime_heap_block_header header;

    if (!runtime_heap_header_read(
        0,
        &header
    )) {
        return false;
    }

    /*
     * Zero-initialized BSS leaves the first header with a zero payload.
     * Every initialized heap block has a non-zero payload size.
     */
    if (header.payload_size != 0) {
        return true;
    }

    /*
     * Before initialization the complete first header must still contain its
     * zero-initialized representation. Any other state indicates corrupted
     * allocator metadata rather than an uninitialized arena.
     */
    if (
        header.next_offset != 0 ||
        header.free
    ) {
        return false;
    }

    header.payload_size =
        RUNTIME_HEAP_SIZE -
        header_span;

    header.next_offset =
        RUNTIME_HEAP_END;

    header.free =
        true;

    return runtime_heap_header_write(
        0,
        &header
    );
}

void *malloc(size_t size)
{
    if (size == 0) {
        return NULL;
    }

    size_t aligned_size;

    if (!runtime_heap_align_size(
        size,
        &aligned_size
    )) {
        return NULL;
    }

    if (!runtime_heap_initialize()) {
        return NULL;
    }

    size_t header_span;

    if (!runtime_heap_header_span(
        &header_span
    )) {
        return NULL;
    }

    size_t current_offset =
        0;

    while (
        current_offset !=
        RUNTIME_HEAP_END
    ) {
        struct runtime_heap_block_header header;

        if (!runtime_heap_header_read(
            current_offset,
            &header
        )) {
            return NULL;
        }

        if (
            header.free &&
            header.payload_size >=
                aligned_size
        ) {
            size_t payload_offset =
                current_offset +
                header_span;

            size_t remaining =
                header.payload_size -
                aligned_size;

            size_t minimum_split =
                header_span +
                _Alignof(max_align_t);

            if (
                remaining >=
                minimum_split
            ) {
                size_t new_header_offset =
                    payload_offset +
                    aligned_size;

                struct runtime_heap_block_header
                    new_header = {
                        .payload_size =
                            remaining -
                            header_span,
                        .next_offset =
                            header.next_offset,
                        .free =
                            true,
                    };

                if (!runtime_heap_header_write(
                    new_header_offset,
                    &new_header
                )) {
                    return NULL;
                }

                header.payload_size =
                    aligned_size;

                header.next_offset =
                    new_header_offset;
            }

            header.free =
                false;

            if (!runtime_heap_header_write(
                current_offset,
                &header
            )) {
                return NULL;
            }

            return
                &runtime_heap[
                    payload_offset
                ];
        }

        if (
            header.next_offset !=
                RUNTIME_HEAP_END &&
            header.next_offset <=
                current_offset
        ) {
            return NULL;
        }

        current_offset =
            header.next_offset;
    }

    return NULL;
}

void free(void *pointer)
{
    if (pointer == NULL) {
        return;
    }

    size_t header_span;

    if (!runtime_heap_header_span(
        &header_span
    )) {
        return;
    }

    struct runtime_heap_block_header first_header;

    if (!runtime_heap_header_read(
        0,
        &first_header
    )) {
        return;
    }

    /*
     * A zero first payload denotes the untouched zero-initialized BSS arena.
     * No allocation can legitimately exist yet.
     */
    if (first_header.payload_size == 0) {
        return;
    }

    size_t previous_offset =
        RUNTIME_HEAP_END;

    size_t current_offset =
        0;

    while (
        current_offset !=
        RUNTIME_HEAP_END
    ) {
        struct runtime_heap_block_header header;

        if (!runtime_heap_header_read(
            current_offset,
            &header
        )) {
            return;
        }

        size_t payload_offset =
            current_offset +
            header_span;

        if (
            pointer ==
            (void *)
                &runtime_heap[
                    payload_offset
                ]
        ) {
            if (header.free) {
                return;
            }

            header.free =
                true;

            while (
                header.next_offset !=
                RUNTIME_HEAP_END
            ) {
                struct runtime_heap_block_header
                    next_header;

                if (!runtime_heap_header_read(
                    header.next_offset,
                    &next_header
                )) {
                    return;
                }

                if (!next_header.free) {
                    break;
                }

                header.payload_size +=
                    header_span +
                    next_header.payload_size;

                header.next_offset =
                    next_header.next_offset;
            }

            if (!runtime_heap_header_write(
                current_offset,
                &header
            )) {
                return;
            }

            if (
                previous_offset !=
                RUNTIME_HEAP_END
            ) {
                struct runtime_heap_block_header
                    previous_header;

                if (!runtime_heap_header_read(
                    previous_offset,
                    &previous_header
                )) {
                    return;
                }

                if (previous_header.free) {
                    previous_header.payload_size +=
                        header_span +
                        header.payload_size;

                    previous_header.next_offset =
                        header.next_offset;

                    (void) runtime_heap_header_write(
                        previous_offset,
                        &previous_header
                    );
                }
            }

            return;
        }

        previous_offset =
            current_offset;

        if (
            header.next_offset !=
                RUNTIME_HEAP_END &&
            header.next_offset <=
                current_offset
        ) {
            return;
        }

        current_offset =
            header.next_offset;
    }
}
