// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file boot_config.c
 * @brief Kernel boot configuration command-line parser.
 */

#include "boot_config.h"

#include "../core/panic.h"

#include <stdbool.h>
#include <stddef.h>

#define BOOT_CONFIG_BOOT_KEY     "myos.boot"
#define BOOT_CONFIG_LOGLEVEL_KEY "myos.loglevel"

static bool boot_config_is_separator(
    char character
);

static bool boot_config_token_equals(
    const char *token,
    size_t token_length,
    const char *expected
);

static void boot_config_parse_token(
    const char *token,
    size_t token_length,
    struct kernel_boot_config *config
);

static void boot_config_parse_boot_mode(
    const char *value,
    size_t value_length,
    struct kernel_boot_config *config
);

static void boot_config_parse_log_level(
    const char *value,
    size_t value_length,
    struct kernel_boot_config *config
);

static bool boot_config_is_separator(
    char character)
{
    return
        character == ' ' ||
        character == '\t';
}

static bool boot_config_token_equals(
    const char *token,
    size_t token_length,
    const char *expected)
{
    size_t index = 0;

    while (
        index < token_length &&
        expected[index] != '\0'
    ) {
        if (token[index] != expected[index]) {
            return false;
        }

        ++index;
    }

    return
        index == token_length &&
        expected[index] == '\0';
}

static void boot_config_parse_boot_mode(
    const char *value,
    size_t value_length,
    struct kernel_boot_config *config)
{
    if (
        boot_config_token_equals(
            value,
            value_length,
            "normal"
        )
    ) {
        config->mode =
            KERNEL_BOOT_MODE_NORMAL;

        return;
    }

    if (
        boot_config_token_equals(
            value,
            value_length,
            "test"
        )
    ) {
        config->mode =
            KERNEL_BOOT_MODE_TEST;
    }
}

static void boot_config_parse_log_level(
    const char *value,
    size_t value_length,
    struct kernel_boot_config *config)
{
    if (
        boot_config_token_equals(
            value,
            value_length,
            "quiet"
        )
    ) {
        config->log_level =
            KERNEL_LOG_LEVEL_QUIET;

        return;
    }

    if (
        boot_config_token_equals(
            value,
            value_length,
            "normal"
        )
    ) {
        config->log_level =
            KERNEL_LOG_LEVEL_NORMAL;

        return;
    }

    if (
        boot_config_token_equals(
            value,
            value_length,
            "verbose"
        )
    ) {
        config->log_level =
            KERNEL_LOG_LEVEL_VERBOSE;

        return;
    }

    if (
        boot_config_token_equals(
            value,
            value_length,
            "debug"
        )
    ) {
        config->log_level =
            KERNEL_LOG_LEVEL_DEBUG;
    }
}

static void boot_config_parse_token(
    const char *token,
    size_t token_length,
    struct kernel_boot_config *config)
{
    size_t separator_index = 0;

    while (
        separator_index < token_length &&
        token[separator_index] != '='
    ) {
        ++separator_index;
    }

    if (
        separator_index == 0 ||
        separator_index >= token_length
    ) {
        return;
    }

    const char *value =
        token + separator_index + 1;

    size_t value_length =
        token_length -
        separator_index -
        1;

    if (value_length == 0) {
        return;
    }

    if (
        boot_config_token_equals(
            token,
            separator_index,
            BOOT_CONFIG_BOOT_KEY
        )
    ) {
        boot_config_parse_boot_mode(
            value,
            value_length,
            config
        );

        return;
    }

    if (
        boot_config_token_equals(
            token,
            separator_index,
            BOOT_CONFIG_LOGLEVEL_KEY
        )
    ) {
        boot_config_parse_log_level(
            value,
            value_length,
            config
        );
    }
}

void boot_config_parse(
    const char *command_line,
    struct kernel_boot_config *config)
{
    if (config == NULL) {
        kernel_panic(
            "boot_config_parse received NULL config"
        );
    }

    config->mode =
        KERNEL_BOOT_MODE_NORMAL;

    config->log_level =
        KERNEL_LOG_LEVEL_NORMAL;

    if (command_line == NULL) {
        return;
    }

    const char *cursor =
        command_line;

    while (*cursor != '\0') {
        while (
            *cursor != '\0' &&
            boot_config_is_separator(*cursor)
        ) {
            ++cursor;
        }

        if (*cursor == '\0') {
            break;
        }

        const char *token =
            cursor;

        while (
            *cursor != '\0' &&
            !boot_config_is_separator(*cursor)
        ) {
            ++cursor;
        }

        size_t token_length =
            (size_t) (cursor - token);

        boot_config_parse_token(
            token,
            token_length,
            config
        );
    }
}
