// SPDX-License-Identifier: GPL-2.0-only

#include "boot_module_test.h"

#include "../arch/x86_64/paging.h"
#include "../boot/initramfs.h"
#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../memory/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Defines
#define BOOT_MODULE_TEST_FNV_OFFSET \
    14695981039346656037ULL

#define BOOT_MODULE_TEST_FNV_PRIME \
    1099511628211ULL

// Static local variables
static bool boot_module_test_snapshot_valid;

static uint64_t boot_module_test_physical_base;
static uint64_t boot_module_test_virtual_base;
static uint64_t boot_module_test_size;
static uint64_t boot_module_test_hash;

// Private functions and helpers declarations
static uint64_t boot_module_test_hash_bytes(
    const uint8_t *bytes,
    uint64_t size
);

static bool boot_module_test_newc_magic(
    const struct boot_module *module
);

// Public functions implementations
void boot_module_test_capture_before_reclaim(
    const struct boot_info *boot_info)
{
    const struct boot_module *module;

    if (!boot_initramfs_find(
        boot_info,
        &module
    )) {
        kernel_panic(
            "Initramfs module unavailable before reclaim"
        );
    }

    if (!boot_module_test_newc_magic(
        module
    )) {
        kernel_panic(
            "Initramfs does not contain newc magic"
        );
    }

    boot_module_test_physical_base =
        module->physical_base;

    boot_module_test_virtual_base =
        module->virtual_base;

    boot_module_test_size =
        module->size;

    boot_module_test_hash =
        boot_module_test_hash_bytes(
            (const uint8_t *)
                module->virtual_base,
            module->size
        );

    boot_module_test_snapshot_valid =
        true;
}

void boot_module_test_run(
    const struct boot_info *boot_info)
{
    if (!boot_module_test_snapshot_valid) {
        kernel_panic(
            "Initramfs pre-reclaim snapshot missing"
        );
    }

    const struct boot_module *module;

    if (!boot_initramfs_find(
        boot_info,
        &module
    )) {
        kernel_panic(
            "Initramfs module unavailable after reclaim"
        );
    }

    if (
        module->physical_base !=
            boot_module_test_physical_base ||
        module->virtual_base !=
            boot_module_test_virtual_base ||
        module->size !=
            boot_module_test_size
    ) {
        kernel_panic(
            "Initramfs descriptor changed across reclaim"
        );
    }

    uint64_t hash =
        boot_module_test_hash_bytes(
            (const uint8_t *)
                module->virtual_base,
            module->size
        );

    if (hash != boot_module_test_hash) {
        kernel_panic(
            "Initramfs bytes changed across reclaim"
        );
    }

    if (!boot_module_test_newc_magic(
        module
    )) {
        kernel_panic(
            "Initramfs newc magic changed across reclaim"
        );
    }

    struct paging_translation translation;

    if (
        !paging_translate(
            module->virtual_base,
            &translation
        ) ||
        translation.physical_address !=
            module->physical_base
    ) {
        kernel_panic(
            "Initramfs virtual-to-physical mapping changed"
        );
    }

    uint64_t first_frame =
        module->physical_base &
        PAGE_ADDRESS_MASK_4K;

    uint64_t last_address =
        module->physical_base +
        module->size - 1;

    uint64_t last_frame =
        last_address &
        PAGE_ADDRESS_MASK_4K;

    for (
        uint64_t frame = first_frame;
        ;
        frame += MEMORY_FRAME_SIZE
    ) {
        if (
            memory_physical_frame_is_bootloader_reclaimable(
                frame
            )
        ) {
            kernel_panic(
                "Initramfs frame is bootloader-reclaimable"
            );
        }

        if (frame == last_frame) {
            break;
        }
    }

    diagnostics_printf(
        "[boot] Initramfs delivery/reclaim test passed: "
        "PA=%x VA=%x size=%u\n",
        module->physical_base,
        module->virtual_base,
        module->size
    );
}

// Private functions and helpers implementations
static uint64_t boot_module_test_hash_bytes(
    const uint8_t *bytes,
    uint64_t size)
{
    if (
        bytes == NULL ||
        size == 0
    ) {
        kernel_panic(
            "Invalid initramfs hash input"
        );
    }

    uint64_t hash =
        BOOT_MODULE_TEST_FNV_OFFSET;

    for (
        uint64_t index = 0;
        index < size;
        ++index
    ) {
        hash ^=
            bytes[index];

        hash *=
            BOOT_MODULE_TEST_FNV_PRIME;
    }

    return hash;
}

static bool boot_module_test_newc_magic(
    const struct boot_module *module)
{
    if (
        module == NULL ||
        module->size < 6
    ) {
        return false;
    }

    const uint8_t *bytes =
        (const uint8_t *)
        module->virtual_base;

    return
        bytes[0] == '0' &&
        bytes[1] == '7' &&
        bytes[2] == '0' &&
        bytes[3] == '7' &&
        bytes[4] == '0' &&
        bytes[5] == '1';
}
