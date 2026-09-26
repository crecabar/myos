// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file acpi_discovery.h
 * @brief Discovery of ACPI tables published by platform firmware.
 */

#ifndef MYOS_FIRMWARE_ACPI_DISCOVERY_H
#define MYOS_FIRMWARE_ACPI_DISCOVERY_H

#include "acpi.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Finds and validates the MADT referenced by a firmware RSDP.
 *
 * The RSDP must be a kernel-owned snapshot. Physical table addresses
 * are checked against the ACPI memory map and the installed direct
 * mapping before their contents are read.
 *
 * The returned MADT borrows its bytes from ACPI firmware memory.
 * That memory must remain reserved and mapped while the MADT is used.
 *
 * This function does not initialize interrupt controllers.
 *
 * @param rsdp Kernel-owned RSDP bytes.
 * @param rsdp_size Number of available RSDP bytes.
 * @param madt Receives the validated MADT.
 *
 * @return true when exactly one valid MADT was discovered;
 *         false otherwise.
 */
bool acpi_madt_discover(
    const uint8_t *rsdp,
    size_t rsdp_size,
    struct acpi_madt *madt
);

#endif
