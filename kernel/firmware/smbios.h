// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file smbios.h
 * @brief SMBIOS platform identification.
 */

#ifndef MYOS_FIRMWARE_SMBIOS_H
#define MYOS_FIRMWARE_SMBIOS_H

#include <stdbool.h>

#define SMBIOS_SYSTEM_STRING_CAPACITY 64U

struct smbios_system_info {
    char manufacturer[SMBIOS_SYSTEM_STRING_CAPACITY];
    char product_name[SMBIOS_SYSTEM_STRING_CAPACITY];
    char version[SMBIOS_SYSTEM_STRING_CAPACITY];
    char serial_number[SMBIOS_SYSTEM_STRING_CAPACITY];

    bool available;
};

/**
 * Reads SMBIOS Type 1 system information.
 *
 * SMBIOS 3.x is preferred when both entry-point formats are available.
 *
 * @param entry_32 SMBIOS 2.x entry point supplied by the bootloader.
 * @param entry_64 SMBIOS 3.x entry point supplied by the bootloader.
 * @param info Output system information descriptor.
 *
 * @return true when valid Type 1 information was found; false otherwise.
 */
bool smbios_system_info_read(
    const void *entry_32,
    const void *entry_64,
    struct smbios_system_info *info
);

#endif
