// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file boot_config_test.c
 * @brief Kernel boot command-line parser regression tests.
 */

#include "boot_config_test.h"

#include <myos/kernel/config/boot_config.h>
#include <myos/kernel/core/panic.h>
#include <myos/kernel/diagnostics/diagnostics.h>

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

static void boot_config_test_expect_scale(
    const char *command_line,
    uint64_t expected_scale)
{
    struct kernel_boot_config config;

    boot_config_parse(
        command_line,
        &config
    );

    if (config.console_scale != expected_scale) {
        kernel_panic(
            "Boot configuration console scale mismatch"
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

    boot_config_test_expect_scale(
        NULL, 1U
    );

    boot_config_test_expect_scale(
        "myos.console.scale=1", 1U
    );

    boot_config_test_expect_scale(
        "myos.console.scale=2", 2U
    );

    boot_config_test_expect_scale(
        "myos.console.scale=3", 3U
    );

    boot_config_test_expect_scale(
        "myos.console.scale=4", 4U
    );

    boot_config_test_expect_scale(
        "myos.console.scale=0", 1U
    );

    boot_config_test_expect_scale(
        "myos.console.scale=5", 1U
    );

    boot_config_test_expect_scale(
        "myos.console.scale=abc", 1U
    );

    boot_config_test_expect_scale(
        "myos.console.scale=2x", 1U
    );

    boot_config_test_expect_scale(
        "myos.console.scale=2 myos.console.scale=3",
        3U
    );

    boot_config_test_expect_scale(
        "myos.console.scale=2 myos.console.scale=invalid",
        2U
    );

    boot_config_test_expect_scale(
        "myos.boot=test myos.loglevel=debug myos.console.scale=2",
        2U
    );

    diagnostics_write(
        "[boot] Command-line configuration test passed\n"
    );
}
