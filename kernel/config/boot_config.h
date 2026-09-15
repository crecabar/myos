// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file boot_config.h
 * @brief Kernel boot configuration derived from the bootloader command line.
 */

#ifndef MYOS_CONFIG_BOOT_CONFIG_H
#define MYOS_CONFIG_BOOT_CONFIG_H

enum kernel_boot_mode {
    KERNEL_BOOT_MODE_NORMAL,
    KERNEL_BOOT_MODE_TEST,
};

enum kernel_log_level {
    KERNEL_LOG_LEVEL_QUIET,
    KERNEL_LOG_LEVEL_NORMAL,
    KERNEL_LOG_LEVEL_VERBOSE,
    KERNEL_LOG_LEVEL_DEBUG,
};

struct kernel_boot_config {
    enum kernel_boot_mode mode;
    enum kernel_log_level log_level;
};

/**
 * Parses the bootloader command line into kernel boot configuration.
 *
 * Missing or unsupported options fall back to safe defaults.
 *
 * @param command_line Bootloader-provided command line, or NULL.
 * @param config Output kernel boot configuration.
 */
void boot_config_parse(
    const char *command_line,
    struct kernel_boot_config *config
);

#endif
