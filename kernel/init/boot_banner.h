// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file boot_banner.h
 * @brief MyOS startup identity banner.
 */

#ifndef MYOS_INIT_BOOT_BANNER_H
#define MYOS_INIT_BOOT_BANNER_H

#include "../arch/x86_64/cpu.h"
#include "../firmware/smbios.h"

enum boot_mode {
    BOOT_MODE_NORMAL,
    BOOT_MODE_TEST,
};

/**
 * Prints the MyOS startup banner.
 *
 * @param cpu CPU identity discovered for the current machine.
 * @param mode Current kernel boot mode.
 */
 void boot_banner_print(
    const struct cpu_info *cpu,
    const struct smbios_system_info *system,
    enum boot_mode mode
);

#endif
