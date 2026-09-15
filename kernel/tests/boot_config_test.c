// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file boot_config_test.c
 * @brief Kernel boot command-line parser regression tests.
 */

#include "boot_config_test.h"

#include "../config/boot_config.h"
#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"

#include <stddef.h>

static void boot_config_test_expect(
    const char *command_line,
    enum kernel_boot_mode expected_mode,
    enum kernel_log_level expected_log_level
);

static void boot_config_test_expect(
    const char *command_line,
    enum kernel_boot_mode expected_mode,
    enum kernel_log_level expected_log_level)
{
    struct kernel_boot_config config;

    boot_config_parse(
        command_line,
        &config
    );

    if (config.mode != expected_mode) {
        kernel_panic(
            "Boot configuration mode mismatch"
        );
    }

    if (config.log_level != expected_log_level) {
        kernel_panic(
            "Boot configuration log-level mismatch"
        );
    }
}

void boot_config_test_run(void)
{
    boot_config_test_expect(
        NULL,
        KERNEL_BOOT_MODE_NORMAL,
        KERNEL_LOG_LEVEL_NORMAL
    );

    boot_config_test_expect(
        "",
        KERNEL_BOOT_MODE_NORMAL,
        KERNEL_LOG_LEVEL_NORMAL
    );

    boot_config_test_expect(
        "myos.boot=test",
        KERNEL_BOOT_MODE_TEST,
        KERNEL_LOG_LEVEL_NORMAL
    );

    boot_config_test_expect(
        "myos.loglevel=verbose",
        KERNEL_BOOT_MODE_NORMAL,
        KERNEL_LOG_LEVEL_VERBOSE
    );

    boot_config_test_expect(
        "myos.boot=test myos.loglevel=debug",
        KERNEL_BOOT_MODE_TEST,
        KERNEL_LOG_LEVEL_DEBUG
    );

    boot_config_test_expect(
        "myos.loglevel=quiet myos.boot=normal",
        KERNEL_BOOT_MODE_NORMAL,
        KERNEL_LOG_LEVEL_QUIET
    );

    boot_config_test_expect(
        "unknown=value",
        KERNEL_BOOT_MODE_NORMAL,
        KERNEL_LOG_LEVEL_NORMAL
    );

    boot_config_test_expect(
        "myos.boot=potato myos.loglevel=loud",
        KERNEL_BOOT_MODE_NORMAL,
        KERNEL_LOG_LEVEL_NORMAL
    );

    boot_config_test_expect(
        "myos.boot=test myos.boot=normal",
        KERNEL_BOOT_MODE_NORMAL,
        KERNEL_LOG_LEVEL_NORMAL
    );

    boot_config_test_expect(
        "   myos.boot=test\tmyos.loglevel=verbose   ",
        KERNEL_BOOT_MODE_TEST,
        KERNEL_LOG_LEVEL_VERBOSE
    );

    diagnostics_write(
        "[boot] Command-line configuration test passed\n"
    );
}
