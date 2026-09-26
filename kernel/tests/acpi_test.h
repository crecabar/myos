// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file acpi_test.h
 * @brief ACPI parser regression tests.
 */

#ifndef MYOS_TESTS_ACPI_TEST_H
#define MYOS_TESTS_ACPI_TEST_H

#include <stddef.h>
#include <stdint.h>

/**
 * Runs synthetic ACPI parser regressions and validates the RSDP
 * snapshot supplied by the current firmware.
 *
 * @param firmware_rsdp Kernel-owned RSDP snapshot.
 * @param firmware_rsdp_size Number of available snapshot bytes.
 */
void acpi_test_run(
    const uint8_t *firmware_rsdp,
    size_t firmware_rsdp_size
);

#endif
