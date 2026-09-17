// SPDX-License-Identifier: GPL-2.0-only

#include "stack.h"

#include <stddef.h>
#include <stdint.h>

#define PROCESS_STACK_PAGE_SIZE 4096ULL
#define PROCESS_STACK_ALIGNMENT 16ULL

static bool process_stack_size_add(
    size_t left,
    size_t right,
    size_t *result
);

static bool process_stack_string_size(
    const char *string,
    size_t *size
);

static uint64_t process_stack_align_down(
    uint64_t value
);

static bool process_stack_size_add(
    size_t left,
    size_t right,
    size_t *result)
{
    if (result == NULL) return false;

    if (left > SIZE_MAX - right) {
        return false;
    }

    *result = left + right;

    return true;
}

static bool process_stack_string_size(
    const char *string,
    size_t *size)
{
    if (string == NULL) return false;
    if (size == NULL) return false;

    size_t length = 0;

    while (string[length] != '\0') {
        if (length == SIZE_MAX - 1) {
            return false;
        }

        ++length;
    }

    *size = length + 1;

    return true;
}

static uint64_t process_stack_align_down(
    uint64_t value)
{
    return
        value &
        ~(PROCESS_STACK_ALIGNMENT - 1ULL);
}

bool process_stack_create(
    struct process_memory *memory,
    struct process_stack *stack,
    uint64_t stack_top,
    size_t page_count)
{
    if (memory == NULL) return false;
    if (stack == NULL) return false;
    if (page_count == 0) return false;
    if ((stack_top & (PROCESS_STACK_PAGE_SIZE - 1)) != 0) return false;

    if (
        page_count >
        UINT64_MAX / PROCESS_STACK_PAGE_SIZE
    ) {
        return false;
    }

    uint64_t stack_size =
        (uint64_t) page_count *
        PROCESS_STACK_PAGE_SIZE;

    if (stack_top < stack_size) {
        return false;
    }

    uint64_t base_address =
        stack_top - stack_size;

    if (base_address < PROCESS_STACK_PAGE_SIZE) {
        return false;
    }

    uint64_t guard_address =
        base_address -
        PROCESS_STACK_PAGE_SIZE;

    if (!process_memory_allocate_pages(
        memory,
        base_address,
        page_count,
        true
    )) {
        return false;
    }

    stack->guard_address = guard_address;
    stack->base_address = base_address;
    stack->stack_top = stack_top;
    stack->page_count = page_count;

    return true;
}

bool process_stack_build_initial(
    struct process_memory *memory,
    const struct process_stack *stack,
    size_t argc,
    const char *const argv[],
    size_t envc,
    const char *const envp[],
    uint64_t *initial_rsp)
{
    if (memory == NULL) return false;
    if (stack == NULL) return false;
    if (initial_rsp == NULL) return false;
    if (stack->page_count == 0) return false;
    if (stack->stack_top <= stack->base_address) return false;

    if (
        argc > 0 &&
        argv == NULL
    ) {
        return false;
    }

    if (
        envc > 0 &&
        envp == NULL
    ) {
        return false;
    }

    if (
        stack->page_count >
        UINT64_MAX / PROCESS_STACK_PAGE_SIZE
    ) {
        return false;
    }

    uint64_t expected_stack_size =
        (uint64_t) stack->page_count *
        PROCESS_STACK_PAGE_SIZE;

    if (
        stack->stack_top -
        stack->base_address !=
        expected_stack_size
    ) {
        return false;
    }

    size_t string_bytes = 0;

    for (size_t index = 0; index < argc; ++index) {
        size_t string_size;

        if (!process_stack_string_size(
            argv[index],
            &string_size
        )) {
            return false;
        }

        if (!process_stack_size_add(
            string_bytes,
            string_size,
            &string_bytes
        )) {
            return false;
        }
    }

    for (size_t index = 0; index < envc; ++index) {
        size_t string_size;

        if (!process_stack_string_size(
            envp[index],
            &string_size
        )) {
            return false;
        }

        if (!process_stack_size_add(
            string_bytes,
            string_size,
            &string_bytes
        )) {
            return false;
        }
    }

    size_t pointer_count;

    if (!process_stack_size_add(
        argc,
        envc,
        &pointer_count
    )) {
        return false;
    }

    /*
     * The metadata consists of:
     *
     * argc
     * argv[argc]
     * argv NULL terminator
     * envp[envc]
     * envp NULL terminator
     */
    size_t metadata_word_count;

    if (!process_stack_size_add(
        pointer_count,
        3,
        &metadata_word_count
    )) {
        return false;
    }

    if (
        metadata_word_count >
        SIZE_MAX / sizeof(uint64_t)
    ) {
        return false;
    }

    size_t metadata_size =
        metadata_word_count *
        sizeof(uint64_t);

    uint64_t stack_size =
        stack->stack_top -
        stack->base_address;

    if (
        string_bytes >
        stack_size
    ) {
        return false;
    }

    uint64_t strings_start =
        stack->stack_top -
        (uint64_t) string_bytes;

    uint64_t metadata_limit =
        process_stack_align_down(
            strings_start
        );

    if (
        metadata_size >
        metadata_limit
    ) {
        return false;
    }

    uint64_t metadata_start =
        metadata_limit -
        (uint64_t) metadata_size;

    uint64_t rsp =
        process_stack_align_down(
            metadata_start
        );

    if (
        rsp <
        stack->base_address
    ) {
        return false;
    }

    if (!process_memory_zero(
        memory,
        stack->base_address,
        (size_t) stack_size
    )) {
        return false;
    }

    uint64_t string_cursor =
        strings_start;

    uint64_t metadata_cursor =
        rsp;

    uint64_t argc_value =
        (uint64_t) argc;

    if (!process_memory_write(
        memory,
        metadata_cursor,
        &argc_value,
        sizeof(argc_value)
    )) {
        return false;
    }

    metadata_cursor +=
        sizeof(uint64_t);

    for (size_t index = 0; index < argc; ++index) {
        size_t string_size;

        if (!process_stack_string_size(
            argv[index],
            &string_size
        )) {
            return false;
        }

        uint64_t user_string_address =
            string_cursor;

        if (!process_memory_write(
            memory,
            string_cursor,
            argv[index],
            string_size
        )) {
            return false;
        }

        if (!process_memory_write(
            memory,
            metadata_cursor,
            &user_string_address,
            sizeof(user_string_address)
        )) {
            return false;
        }

        string_cursor +=
            (uint64_t) string_size;

        metadata_cursor +=
            sizeof(uint64_t);
    }

    uint64_t null_pointer = 0;

    if (!process_memory_write(
        memory,
        metadata_cursor,
        &null_pointer,
        sizeof(null_pointer)
    )) {
        return false;
    }

    metadata_cursor +=
        sizeof(uint64_t);

    for (size_t index = 0; index < envc; ++index) {
        size_t string_size;

        if (!process_stack_string_size(
            envp[index],
            &string_size
        )) {
            return false;
        }

        uint64_t user_string_address =
            string_cursor;

        if (!process_memory_write(
            memory,
            string_cursor,
            envp[index],
            string_size
        )) {
            return false;
        }

        if (!process_memory_write(
            memory,
            metadata_cursor,
            &user_string_address,
            sizeof(user_string_address)
        )) {
            return false;
        }

        string_cursor +=
            (uint64_t) string_size;

        metadata_cursor +=
            sizeof(uint64_t);
    }

    if (!process_memory_write(
        memory,
        metadata_cursor,
        &null_pointer,
        sizeof(null_pointer)
    )) {
        return false;
    }

    *initial_rsp = rsp;

    return true;
}

bool process_stack_destroy(
    struct process_memory *memory,
    struct process_stack *stack)
{
    if (memory == NULL) return false;
    if (stack == NULL) return false;
    if (stack->page_count == 0) return false;

    if (!process_memory_release_pages(
        memory,
        stack->base_address,
        stack->page_count
    )) {
        return false;
    }

    stack->guard_address = 0;
    stack->base_address = 0;
    stack->stack_top = 0;
    stack->page_count = 0;

    return true;
}
