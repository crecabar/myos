// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file boot_banner.c
 * @brief MyOS startup identity banner.
 */

#include "boot_banner.h"

#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../memory/memory.h"
#include "../version.h"

#include <stdint.h>

#define BOOT_BANNER_MIB (1024ULL * 1024ULL)
#define BOOT_BANNER_GIB (1024ULL * 1024ULL * 1024ULL)

static void boot_banner_print_memory_size(
    uint64_t bytes
);

static const char *boot_banner_platform_name(
    const struct cpu_info *cpu
);

static const char *boot_banner_mode_name(
    enum boot_mode mode
);

static void boot_banner_print_memory_size(
    uint64_t bytes)
{
    if (bytes >= BOOT_BANNER_GIB) {
        uint64_t whole =
            bytes / BOOT_BANNER_GIB;

        uint64_t remainder =
            bytes % BOOT_BANNER_GIB;

        uint64_t tenths =
            (
                remainder * 10ULL +
                BOOT_BANNER_GIB / 2ULL
            ) /
            BOOT_BANNER_GIB;

        if (tenths == 10ULL) {
            ++whole;
            tenths = 0;
        }

        diagnostics_printf(
            "%u.%u GiB",
            whole,
            tenths
        );

        return;
    }

    uint64_t mib =
        (
            bytes +
            BOOT_BANNER_MIB / 2ULL
        ) /
        BOOT_BANNER_MIB;

    diagnostics_printf(
        "%u MiB",
        mib
    );
}

static bool boot_banner_string_present(const char *string)
{
    return
        string != NULL &&
        string[0] != '\0';
}

static const char *boot_banner_platform_name(
    const struct cpu_info *cpu)
{
    if (cpu_info_is_qemu(cpu)) {
        return "QEMU";
    }

    if (cpu->hypervisor_present) {
        return "Virtual";
    }

    return "Physical";
}

static const char *boot_banner_mode_name(
    enum boot_mode mode)
{
    switch (mode) {
        case BOOT_MODE_NORMAL:
            return "normal";

        case BOOT_MODE_TEST:
            return "test";
    }

    return "unknown";
}

void boot_banner_print(
    const struct cpu_info *cpu,
    const struct smbios_system_info *system,
    enum boot_mode mode)
{
    if (cpu == NULL) {
        kernel_panic(
            "boot_banner_print received NULL CPU info"
        );
    }

    diagnostics_printf(
        "%s\n",
        MYOS_VERSION_STRING
    );

    if (mode == BOOT_MODE_NORMAL) {
        diagnostics_printf(
            "%s\n",
            MYOS_COPYRIGHT_STRING
        );
    }

    diagnostics_write("\n");

    diagnostics_printf(
        "CPU:      %s\n",
        cpu->brand
    );

    diagnostics_write(
        "Memory:   "
    );

    boot_banner_print_memory_size(
        memory_usable_byte_count()
    );

    diagnostics_write(
        " usable\n"
    );

    diagnostics_write(
        "Platform: "
    );

    if (
        system != NULL &&
        system->available
    ) {
        if (
            boot_banner_string_present(
                system->manufacturer
            )
        ) {
            diagnostics_printf(
                "%s",
                system->manufacturer
            );
        }

        if (
            boot_banner_string_present(
                system->product_name
            )
        ) {
            if (
                boot_banner_string_present(
                    system->manufacturer
                )
            ) {
                diagnostics_write(" ");
            }

            diagnostics_printf(
                "%s",
                system->product_name
            );
        }

        diagnostics_write(" / ");
    }

    diagnostics_printf(
        "%s\n",
        boot_banner_platform_name(cpu)
    );

    if (mode == BOOT_MODE_NORMAL) {
        diagnostics_write(
            "Firmware: UEFI\n"
        );
    }

    diagnostics_printf(
        "Mode:     %s\n",
        boot_banner_mode_name(mode)
    );

    diagnostics_write("\n");
}
