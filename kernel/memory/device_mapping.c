// SPDX-License-Identifier: GPL-2.0-only

#include "device_mapping.h"

#include "memory.h"
#include "../arch/x86_64/paging.h"
#include "../core/panic.h"
#include "../drivers/framebuffer.h"

#include <stddef.h>
#include <stdint.h>

#define KERNEL_FRAMEBUFFER_WINDOW_BASE 0xffffffffc0000000ULL
#define KERNEL_FRAMEBUFFER_WINDOW_SIZE 0x20000000ULL

#define KERNEL_MMIO_WINDOW_BASE 0xffffffffe0000000ULL
#define KERNEL_MMIO_WINDOW_SIZE 0x10000000ULL

static uint64_t next_mmio_virtual =
    KERNEL_MMIO_WINDOW_BASE;

static void device_mapping_rollback(
    uint64_t virtual_start,
    uint64_t mapped_size
);

bool device_mapping_map_framebuffer(
    struct framebuffer *framebuffer)
{
    if (framebuffer == NULL) return false;
    if (framebuffer->address == NULL) return false;
    if (framebuffer->height == 0) return false;
    if (framebuffer->pitch == 0) return false;

    if (
        framebuffer->pitch >
        UINT64_MAX / framebuffer->height
    ) {
        return false;
    }

    uint64_t framebuffer_size =
        framebuffer->pitch *
        framebuffer->height;

    if (framebuffer_size == 0) {
        return false;
    }

    uint64_t source_address =
        (uint64_t) framebuffer->address;

    uint64_t source_page =
        source_address &
        PAGE_ADDRESS_MASK_4K;

    uint64_t page_offset =
        source_address &
        (MEMORY_FRAME_SIZE - 1);

    if (
        framebuffer_size >
        UINT64_MAX - page_offset
    ) {
        return false;
    }

    uint64_t required_size =
        framebuffer_size +
        page_offset;

    if (
        required_size >
        UINT64_MAX -
            (MEMORY_FRAME_SIZE - 1)
    ) {
        return false;
    }

    uint64_t mapped_size =
        (
            required_size +
            MEMORY_FRAME_SIZE - 1
        ) &
        PAGE_ADDRESS_MASK_4K;

    if (
        mapped_size == 0 ||
        mapped_size >
            KERNEL_FRAMEBUFFER_WINDOW_SIZE
    ) {
        return false;
    }

    uint64_t mapped_bytes = 0;

    while (mapped_bytes < mapped_size) {
        uint64_t source_virtual =
            source_page +
            mapped_bytes;

        struct paging_translation translation;

        if (!paging_translate(
            source_virtual,
            &translation
        )) {
            device_mapping_rollback(
                KERNEL_FRAMEBUFFER_WINDOW_BASE,
                mapped_bytes
            );

            return false;
        }

        uint64_t physical_page =
            translation.physical_address &
            PAGE_ADDRESS_MASK_4K;

        uint64_t destination_virtual =
            KERNEL_FRAMEBUFFER_WINDOW_BASE +
            mapped_bytes;

        if (!paging_map_kernel_device_page(
            destination_virtual,
            physical_page,
            PAGING_CACHE_WRITE_COMBINING
        )) {
            device_mapping_rollback(
                KERNEL_FRAMEBUFFER_WINDOW_BASE,
                mapped_bytes
            );

            return false;
        }

        mapped_bytes +=
            MEMORY_FRAME_SIZE;
    }

    framebuffer->address =
        (void *) (
            KERNEL_FRAMEBUFFER_WINDOW_BASE +
            page_offset
        );

    return true;
}

bool device_mapping_map_mmio_page(
    uint64_t physical_address,
    volatile void **virtual_address)
{
    if (virtual_address == NULL) return false;

    if (
        (physical_address &
         (MEMORY_FRAME_SIZE - 1)) != 0
    ) {
        return false;
    }

    uint64_t mmio_window_end =
        KERNEL_MMIO_WINDOW_BASE +
        KERNEL_MMIO_WINDOW_SIZE;

    if (
        next_mmio_virtual >
        mmio_window_end -
            MEMORY_FRAME_SIZE
    ) {
        return false;
    }

    uint64_t mapping_virtual =
        next_mmio_virtual;

    if (!paging_map_kernel_device_page(
        mapping_virtual,
        physical_address,
        PAGING_CACHE_UNCACHEABLE
    )) {
        return false;
    }

    next_mmio_virtual +=
        MEMORY_FRAME_SIZE;

    *virtual_address =
        (volatile void *)
        mapping_virtual;

    return true;
}

static void device_mapping_rollback(
    uint64_t virtual_start,
    uint64_t mapped_size)
{
    uint64_t offset = 0;

    while (offset < mapped_size) {
        uint64_t physical_address;

        if (!paging_unmap_page(
            paging_kernel_address_space(),
            virtual_start + offset,
            &physical_address
        )) {
            kernel_panic(
                "Unable to roll back kernel device mapping"
            );
        }

        offset +=
            MEMORY_FRAME_SIZE;
    }
}
