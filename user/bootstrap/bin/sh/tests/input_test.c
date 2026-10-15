// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include <sh/input.h>

#include <myos/file.h>

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const unsigned char *test_bytes;
static size_t test_length;
static size_t test_position;
static size_t test_fail_position;
static size_t test_read_calls;

static void test_feed(
    const unsigned char *bytes,
    size_t length)
{
    test_bytes = bytes;
    test_length = length;
    test_position = 0U;
    test_fail_position = SIZE_MAX;
    test_read_calls = 0U;
}

syscall_result_t myos_fd_read(
    uint64_t descriptor,
    void *buffer,
    size_t size)
{
    assert(descriptor == 0U);
    assert(buffer != NULL);
    assert(size == 1U);

    ++test_read_calls;

    if (test_position == test_fail_position) {
        return syscall_result_error(
            SYSCALL_ERROR_WOULD_BLOCK
        );
    }

    if (test_position == test_length) {
        return 0;
    }

    *(unsigned char *) buffer =
        test_bytes[test_position++];

    return 1;
}

static void test_complete_line(void)
{
    static const unsigned char input[] =
        "uname -m\n";

    char line[SH_LINE_CAPACITY];

    test_feed(input, sizeof(input) - 1U);

    assert(
        sh_read_line(line, sizeof(line)) ==
            SH_INPUT_RESULT_LINE
    );

    assert(strcmp(line, "uname -m") == 0);
    assert(test_position == sizeof(input) - 1U);
}

static void test_empty_and_crlf(void)
{
    static const unsigned char input[] =
        "\nhello\r\n";

    char line[SH_LINE_CAPACITY];

    test_feed(input, sizeof(input) - 1U);

    assert(
        sh_read_line(line, sizeof(line)) ==
            SH_INPUT_RESULT_LINE
    );
    assert(strcmp(line, "") == 0);

    assert(
        sh_read_line(line, sizeof(line)) ==
            SH_INPUT_RESULT_LINE
    );
    assert(strcmp(line, "hello") == 0);
}

static void test_eof(void)
{
    static const unsigned char input[] =
        "pwd";

    char line[SH_LINE_CAPACITY];

    test_feed(input, 0U);

    assert(
        sh_read_line(line, sizeof(line)) ==
            SH_INPUT_RESULT_EOF
    );

    test_feed(input, sizeof(input) - 1U);

    assert(
        sh_read_line(line, sizeof(line)) ==
            SH_INPUT_RESULT_LINE
    );
    assert(strcmp(line, "pwd") == 0);

    assert(
        sh_read_line(line, sizeof(line)) ==
            SH_INPUT_RESULT_EOF
    );
}

static void test_oversized_line(void)
{
    static const unsigned char input[] =
        "abcdef\nok\n";

    char line[4];

    test_feed(input, sizeof(input) - 1U);

    assert(
        sh_read_line(line, sizeof(line)) ==
            SH_INPUT_RESULT_INVALID_LINE
    );

    assert(
        sh_read_line(line, sizeof(line)) ==
            SH_INPUT_RESULT_LINE
    );

    assert(strcmp(line, "ok") == 0);
}

static void test_capacity_boundary(void)
{
    static const unsigned char input[] =
        "abc\nabc\r\nabcd\r\nok\n";

    char line[4];

    test_feed(input, sizeof(input) - 1U);

    assert(
        sh_read_line(line, sizeof(line)) ==
            SH_INPUT_RESULT_LINE
    );
    assert(strcmp(line, "abc") == 0);

    assert(
        sh_read_line(line, sizeof(line)) ==
            SH_INPUT_RESULT_LINE
    );
    assert(strcmp(line, "abc") == 0);

    assert(
        sh_read_line(line, sizeof(line)) ==
            SH_INPUT_RESULT_INVALID_LINE
    );

    assert(
        sh_read_line(line, sizeof(line)) ==
            SH_INPUT_RESULT_LINE
    );
    assert(strcmp(line, "ok") == 0);
}

static void test_minimum_capacity(void)
{
    static const unsigned char input[] =
        "\r\n\nx\n";

    char line[1];

    test_feed(input, sizeof(input) - 1U);

    assert(
        sh_read_line(line, sizeof(line)) ==
            SH_INPUT_RESULT_LINE
    );
    assert(line[0] == '\0');

    assert(
        sh_read_line(line, sizeof(line)) ==
            SH_INPUT_RESULT_LINE
    );
    assert(line[0] == '\0');

    assert(
        sh_read_line(line, sizeof(line)) ==
            SH_INPUT_RESULT_INVALID_LINE
    );
}

static void test_binary_input(void)
{
    static const unsigned char input[] = {
        'a', '\0', 'b', '\n',
        'x', '\n',
    };

    char line[SH_LINE_CAPACITY];

    test_feed(input, sizeof(input));

    assert(
        sh_read_line(line, sizeof(line)) ==
            SH_INPUT_RESULT_INVALID_LINE
    );

    assert(
        sh_read_line(line, sizeof(line)) ==
            SH_INPUT_RESULT_LINE
    );

    assert(strcmp(line, "x") == 0);
}

static void test_error(void)
{
    static const unsigned char input[] = {
        'a', '\n',
    };

    char line[SH_LINE_CAPACITY];

    test_feed(input, sizeof(input));
    test_fail_position = 0U;

    assert(
        sh_read_line(line, sizeof(line)) ==
            SH_INPUT_RESULT_ERROR
    );

    assert(test_position == 0U);

    test_feed(input, sizeof(input));

    assert(
        sh_read_line(NULL, sizeof(line)) ==
            SH_INPUT_RESULT_ERROR
    );

    assert(
        sh_read_line(line, 0U) ==
            SH_INPUT_RESULT_ERROR
    );

    assert(test_read_calls == 0U);
}

int main(void)
{
    test_complete_line();
    test_empty_and_crlf();
    test_eof();
    test_oversized_line();
    test_capacity_boundary();
    test_minimum_capacity();
    test_binary_input();
    test_error();

    return 0;
}
