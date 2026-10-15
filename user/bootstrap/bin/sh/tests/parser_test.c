// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

 #include <sh/parser.h>

#include <assert.h>
#include <stddef.h>
#include <string.h>

static void test_empty(void)
{
    char line[] = " \t ";
    char *argv[SH_MAX_ARGS + 1U];

    assert(sh_parse_line(
        line, argv, SH_MAX_ARGS + 1U
    ) == 0);

    assert(argv[0] == NULL);
}

static void test_simple_command(void)
{
    char line[] = "uname -m";
    char *argv[SH_MAX_ARGS + 1U];

    assert(sh_parse_line(
        line, argv, SH_MAX_ARGS + 1U
    ) == 2);

    assert(strcmp(argv[0], "uname") == 0);
    assert(strcmp(argv[1], "-m") == 0);
    assert(argv[2] == NULL);
}

static void test_whitespace(void)
{
    char line[] = "\t echo  hello\tworld  ";
    char *argv[SH_MAX_ARGS + 1U];

    assert(sh_parse_line(
        line, argv, SH_MAX_ARGS + 1U
    ) == 3);

    assert(strcmp(argv[0], "echo") == 0);
    assert(strcmp(argv[1], "hello") == 0);
    assert(strcmp(argv[2], "world") == 0);
    assert(argv[3] == NULL);
}

static void test_argument_limit(void)
{
    char line[] = "a b c";
    char *argv[3];

    assert(sh_parse_line(line, argv, 3U) == -1);
}

static void test_invalid_arguments(void)
{
    char line[] = "uname";
    char *argv[SH_MAX_ARGS + 1U];

    assert(sh_parse_line(
        NULL, argv, SH_MAX_ARGS + 1U
    ) == -1);

    assert(sh_parse_line(
        line, NULL, SH_MAX_ARGS + 1U
    ) == -1);

    assert(sh_parse_line(
        line, argv, 0U
    ) == -1);
}

int main(void)
{
    test_empty();
    test_simple_command();
    test_whitespace();
    test_argument_limit();
    test_invalid_arguments();

    return 0;
}
