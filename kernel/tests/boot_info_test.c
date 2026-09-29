// SPDX-License-Identifier: GPL-2.0-only

#include "boot_info_test.h"

#include "../boot/boot.h"
#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"

#include <stddef.h>
#include <stdint.h>

// Static local variables
static uint32_t boot_info_test_framebuffer_storage;

static struct boot_info boot_info_test_fixture = {
    .direct_map_offset =
        0xffff800000000000ULL,

    .rsdp_snapshot_size = 0,

    .command_line = "myos.boot=test",
    .command_line_length = 14,

    .framebuffer = {
        .address =
            &boot_info_test_framebuffer_storage,
        .width = 800,
        .height = 600,
        .pitch = 3200,
        .bpp = 32,
        .red_mask_size = 8,
        .red_mask_shift = 16,
        .green_mask_size = 8,
        .green_mask_shift = 8,
        .blue_mask_size = 8,
        .blue_mask_shift = 0,
    },

    .memory_regions = {
        {
            .base = 0x100000,
            .length = 0x100000,
            .type = MEMORY_REGION_USABLE,
        },
    },

    .memory_region_count = 1,
    .module_count = 0,
};

// Public functions implementations
void boot_info_test_run(void)
{
    if (!boot_info_validate(
        &boot_info_test_fixture
    )) {
        kernel_panic(
            "Boot info validator rejected valid contract"
        );
    }

    boot_info_test_fixture.command_line_length =
        BOOT_COMMAND_LINE_MAX;

    if (boot_info_validate(
        &boot_info_test_fixture
    )) {
        kernel_panic(
            "Boot info validator accepted invalid command-line length"
        );
    }

    boot_info_test_fixture.command_line_length =
        14;

    boot_info_test_fixture.rsdp_snapshot_size =
        1;

    if (boot_info_validate(
        &boot_info_test_fixture
    )) {
        kernel_panic(
            "Boot info validator accepted invalid RSDP size"
        );
    }

    boot_info_test_fixture.rsdp_snapshot_size =
        0;

    boot_info_test_fixture.framebuffer.bpp =
        16;

    if (boot_info_validate(
        &boot_info_test_fixture
    )) {
        kernel_panic(
            "Boot info validator accepted invalid framebuffer"
        );
    }

    boot_info_test_fixture.framebuffer.bpp =
        32;

    boot_info_test_fixture
        .memory_regions[0]
        .length = 0;

    if (boot_info_validate(
        &boot_info_test_fixture
    )) {
        kernel_panic(
            "Boot info validator accepted empty memory region"
        );
    }

    boot_info_test_fixture
        .memory_regions[0]
        .length = 0x100000;

    boot_info_test_fixture.module_count =
        BOOT_MODULE_MAX + 1;

    if (boot_info_validate(
        &boot_info_test_fixture
    )) {
        kernel_panic(
            "Boot info validator accepted too many modules"
        );
    }

    boot_info_test_fixture.module_count = 1;

    struct boot_module *module =
        &boot_info_test_fixture.modules[0];

    module->physical_base = 0x300000;
    module->virtual_base =
        0xffff800000300000ULL;
    module->size = 4096;

    module->name[0] = 'm';
    module->name[1] = '\0';

    module->command_line[0] = '\0';

    if (!boot_info_validate(
        &boot_info_test_fixture
    )) {
        kernel_panic(
            "Boot info validator rejected valid module"
        );
    }

    module->size = 0;

    if (boot_info_validate(
        &boot_info_test_fixture
    )) {
        kernel_panic(
            "Boot info validator accepted empty module"
        );
    }

    module->size = 4096;

    boot_info_test_fixture.module_count = 0;

    diagnostics_write(
        "[boot] Normalized boot-info contract tests passed\n"
    );
}
