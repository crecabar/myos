// SPDX-License-Identifier: GPL-2.0-only

#include "syscall.h"

#include "../diagnostics/diagnostics.h"
#include "../process/memory.h"
#include "../scheduler/scheduler.h"

#include <stddef.h>
#include <stdint.h>

#define SYSCALL_WRITE_MAX_SIZE 256

static syscall_result_t syscall_write(
    uint64_t user_address,
    uint64_t length);

syscall_result_t syscall_dispatch(
    uint64_t number,
    uint64_t argument0,
    uint64_t argument1,
    uint64_t argument2,
    uint64_t argument3,
    uint64_t argument4,
    uint64_t argument5
) {
    (void) argument2;
    (void) argument3;
    (void) argument4;
    (void) argument5;

    switch (number) {
        case SYSCALL_DEBUG_PUTC:
            diagnostics_printf(
                "%c",
                (char) argument0
            );

            return 0;

        case SYSCALL_WRITE:
            return syscall_write(argument0, argument1);

        default:
            return syscall_result_error(
                SYSCALL_ERROR_NOT_IMPLEMENTED
            );
    }
}

static syscall_result_t syscall_write(
    uint64_t user_address,
    uint64_t length)
{
    if (length == 0) {
        return 0;
    }

    if (length > SYSCALL_WRITE_MAX_SIZE) {
        return syscall_result_error(
            SYSCALL_ERROR_INVALID_ARGUMENT
        );
    }

    struct process *process = scheduler_current();

    if (process == NULL || process->memory == NULL) {
        return syscall_result_error(
            SYSCALL_ERROR_BAD_ADDRESS
        );
    }

    uint8_t buffer[SYSCALL_WRITE_MAX_SIZE];

    if (!process_memory_read(
        process->memory,
        user_address,
        buffer,
        (size_t) length
    )) {
        return syscall_result_error(
            SYSCALL_ERROR_BAD_ADDRESS
        );
    }

    for (size_t index = 0; index < (size_t) length; ++index) {
        diagnostics_printf("%c", (char) buffer[index]);
    }

    return length;
}
