// SPDX-License-Identifier: GPL-2.0-only

#include "stack.h"

#include <stddef.h>

#define PROCESS_STACK_PAGE_SIZE 4096ULL

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

    uint64_t stack_size = (uint64_t) page_count * PROCESS_STACK_PAGE_SIZE;
    uint64_t base_address = stack_top - stack_size;
    uint64_t guard_address = base_address - PROCESS_STACK_PAGE_SIZE;

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
