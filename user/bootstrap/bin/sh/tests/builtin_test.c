// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file builtin_test.c
 * @brief Host-side regressions for shell builtins.
 */

#include <sh/builtin.h>

#include <myos/abi/syscall.h>

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static unsigned int chdir_calls;
static const char *chdir_path;
static size_t chdir_path_length;
static syscall_result_t chdir_result;

/*
 * Host test substitute for the MyOS process runtime.
 */
syscall_result_t myos_chdir(
    const char *path,
    size_t path_length)
{
    ++chdir_calls;
    chdir_path = path;
    chdir_path_length = path_length;

    return chdir_result;
}

static void reset_chdir(void)
{
    chdir_calls = 0U;
    chdir_path = NULL;
    chdir_path_length = 0U;
    chdir_result = 0;
}

static void test_cd_success(void)
{
    char *argv[] = {"cd", "/bin", NULL};

    reset_chdir();

    struct sh_builtin_result result =
        sh_builtin_execute(2, argv);

    assert(result.handled);
    assert(!result.exit_requested);
    assert(result.status == 0);

    assert(chdir_calls == 1U);
    assert(chdir_path != NULL);
    assert(strcmp(chdir_path, "/bin") == 0);
    assert(chdir_path_length == 4U);
}

static void test_cd_invalid_arguments(void)
{
    char *missing[] = {"cd", NULL};
    char *empty[] = {"cd", "", NULL};
    char *excess[] = {"cd", "/bin", "/dev", NULL};

    char **cases[] = {
        missing,
        empty,
        excess,
    };

    const int counts[] = {1, 2, 3};

    reset_chdir();

    for (size_t index = 0U; index < 3U; ++index) {
        struct sh_builtin_result result =
            sh_builtin_execute(
                counts[index],
                cases[index]
            );

        assert(result.handled);
        assert(!result.exit_requested);
        assert(result.status == 1);
    }

    assert(chdir_calls == 0U);
}

static void test_cd_failure(void)
{
    char *argv[] = {"cd", "/missing", NULL};

    reset_chdir();
    chdir_result = -SYSCALL_ERROR_NOT_FOUND;

    struct sh_builtin_result result =
        sh_builtin_execute(2, argv);

    assert(result.handled);
    assert(!result.exit_requested);
    assert(result.status == 1);

    assert(chdir_calls == 1U);
    assert(strcmp(chdir_path, "/missing") == 0);
    assert(chdir_path_length == 8U);
}

static void test_exit_success(void)
{
    char *plain[] = {"exit", NULL};
    char *number[] = {"exit", "42", NULL};
    char *maximum[] = {"exit", "255", NULL};

    reset_chdir();

    struct sh_builtin_result result =
        sh_builtin_execute(1, plain);

    assert(result.handled);
    assert(result.exit_requested);
    assert(result.status == 0);

    result = sh_builtin_execute(2, number);

    assert(result.handled);
    assert(result.exit_requested);
    assert(result.status == 42);

    result = sh_builtin_execute(2, maximum);

    assert(result.handled);
    assert(result.exit_requested);
    assert(result.status == 255);

    assert(chdir_calls == 0U);
}

static void test_exit_invalid_arguments(void)
{
    char *overflow[] = {"exit", "256", NULL};
    char *text[] = {"exit", "abc", NULL};
    char *empty[] = {"exit", "", NULL};
    char *negative[] = {"exit", "-1", NULL};
    char *excess[] = {"exit", "1", "2", NULL};

    char **cases[] = {
        overflow,
        text,
        empty,
        negative,
        excess,
    };

    const int counts[] = {2, 2, 2, 2, 3};

    reset_chdir();

    for (size_t index = 0U; index < 5U; ++index) {
        struct sh_builtin_result result =
            sh_builtin_execute(
                counts[index],
                cases[index]
            );

        assert(result.handled);
        assert(!result.exit_requested);
        assert(result.status == 1);
    }

    assert(chdir_calls == 0U);
}

static void test_not_builtin(void)
{
    char *argv[] = {"uname", "-m", NULL};

    reset_chdir();

    struct sh_builtin_result result =
        sh_builtin_execute(2, argv);

    assert(!result.handled);
    assert(!result.exit_requested);
    assert(result.status == 0);
    assert(chdir_calls == 0U);
}

static void test_invalid_dispatch(void)
{
    char *null_command[] = {NULL};

    struct sh_builtin_result result =
        sh_builtin_execute(0, NULL);

    assert(!result.handled);
    assert(!result.exit_requested);

    result = sh_builtin_execute(1, null_command);

    assert(!result.handled);
    assert(!result.exit_requested);
}

int main(void)
{
    test_cd_success();
    test_cd_invalid_arguments();
    test_cd_failure();

    test_exit_success();
    test_exit_invalid_arguments();

    test_not_builtin();
    test_invalid_dispatch();

    return 0;
}
