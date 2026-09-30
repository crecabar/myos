// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include "syscall.h"

#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../process/user_copy.h"
#include "../process/wait.h"
#include "../scheduler/scheduler.h"

#include <stddef.h>
#include <stdint.h>

#define SYSCALL_WRITE_MAX_SIZE 256

// Private functions and helpers declarations
static syscall_result_t syscall_write(
    uint64_t user_address,
    uint64_t length);

static uint64_t syscall_wait_termination_reason(
    enum process_termination_reason reason
);

// Public functions implementations
enum syscall_waitpid_action syscall_waitpid_prepare(
    uint64_t child_pid,
    uint64_t status_address,
    uint64_t options,
    syscall_result_t *result)
{
    if (result == NULL) {
        kernel_panic(
            "waitpid received null result storage"
        );
    }

    if (
        (options & ~SYSCALL_WAITPID_NOHANG) != 0
    ) {
        *result = syscall_result_error(
            SYSCALL_ERROR_INVALID_ARGUMENT
        );

        return SYSCALL_WAITPID_ACTION_RETURN;
    }

    struct process *process =
        scheduler_current();

    if (
        process == NULL ||
        process->instance == NULL ||
        process->instance->process.instance !=
            process->instance
    ) {
        *result = syscall_result_error(
            SYSCALL_ERROR_INVALID_ARGUMENT
        );

        return SYSCALL_WAITPID_ACTION_RETURN;
    }

    struct process_instance *parent =
        process->instance;

    struct process_wait_status wait_status;

    enum process_wait_result wait_result;

    if (child_pid == 0) {
        wait_result =
            process_wait_peek(
                parent,
                &wait_status
            );
    } else {
        wait_result =
            process_waitpid_peek(
                parent,
                child_pid,
                &wait_status
            );
    }

    switch (wait_result) {
        case PROCESS_WAIT_RESULT_INVALID_ARGUMENT:
            *result = syscall_result_error(
                SYSCALL_ERROR_INVALID_ARGUMENT
            );

            return SYSCALL_WAITPID_ACTION_RETURN;

        case PROCESS_WAIT_RESULT_NO_CHILD:
            *result = syscall_result_error(
                SYSCALL_ERROR_NO_CHILD
            );

            return SYSCALL_WAITPID_ACTION_RETURN;

        case PROCESS_WAIT_RESULT_NOT_TERMINATED:
            if (
                (options & SYSCALL_WAITPID_NOHANG) != 0
            ) {
                *result = 0;

                return SYSCALL_WAITPID_ACTION_RETURN;
            }

            if (!process_wait_register(
                parent,
                child_pid
            )) {
                kernel_panic(
                    "waitpid failed to register live child wait"
                );
            }

            return SYSCALL_WAITPID_ACTION_BLOCK;

        case PROCESS_WAIT_RESULT_TERMINATED:
            break;

        case PROCESS_WAIT_RESULT_REAPED:
        default:
            kernel_panic(
                "waitpid peek returned impossible result"
            );
    }

    if (status_address != 0) {
        struct syscall_wait_status user_status = {
            .termination_reason =
                syscall_wait_termination_reason(
                    wait_status.termination_reason
                ),
            .exit_status =
                wait_status.exit_status,
        };

        if (!copy_to_user(
            process->memory,
            status_address,
            &user_status,
            sizeof(user_status)
        )) {
            /*
             * A previous blocking invocation may have registered this
             * request before the child terminated. Returning to userspace
             * must not leave a wait request active once the process is no
             * longer BLOCKED.
             *
             * The child itself remains waitable and may be consumed by a
             * later waitpid call.
             */
            if (
                parent->wait_active &&
                !process_wait_cancel(parent)
            ) {
                kernel_panic(
                    "waitpid failed to cancel request after copy failure"
                );
            }

            *result = syscall_result_error(
                SYSCALL_ERROR_BAD_ADDRESS
            );

            return SYSCALL_WAITPID_ACTION_RETURN;
        }
    }

    enum process_wait_result reap_result =
        process_waitpid_try_reap(
            parent,
            wait_status.pid,
            NULL
        );

    if (
        reap_result !=
        PROCESS_WAIT_RESULT_REAPED
    ) {
        kernel_panic(
            "waitpid failed to reap observed child"
        );
    }

    *result =
        (syscall_result_t) wait_status.pid;

    return SYSCALL_WAITPID_ACTION_RETURN;
}

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

// Private functions and helpers implementations
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

    if (!copy_from_user(
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

    return (syscall_result_t) length;
}

static uint64_t syscall_wait_termination_reason(
    enum process_termination_reason reason)
{
    switch (reason) {
        case PROCESS_TERMINATION_EXITED:
            return
                SYSCALL_WAIT_TERMINATION_EXITED;

        case PROCESS_TERMINATION_SEGMENTATION_FAULT:
            return
                SYSCALL_WAIT_TERMINATION_SEGMENTATION_FAULT;

        case PROCESS_TERMINATION_ILLEGAL_INSTRUCTION:
            return
                SYSCALL_WAIT_TERMINATION_ILLEGAL_INSTRUCTION;

        case PROCESS_TERMINATION_PROTECTION_FAULT:
            return
                SYSCALL_WAIT_TERMINATION_PROTECTION_FAULT;

        case PROCESS_TERMINATION_ARITHMETIC_FAULT:
            return
                SYSCALL_WAIT_TERMINATION_ARITHMETIC_FAULT;

        case PROCESS_TERMINATION_TRAP:
            return
                SYSCALL_WAIT_TERMINATION_TRAP;

        case PROCESS_TERMINATION_NONE:
        default:
            kernel_panic(
                "waitpid observed invalid termination reason"
            );
    }
}
