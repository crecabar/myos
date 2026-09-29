// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file user_processes.c
 * @brief User-process scheduler test fixtures.
 */

#include "user_processes.h"
#include "user_processes/block.h"
#include "user_processes/exec.h"
#include "user_processes/lifecycle_stress.h"
#include "user_processes/scheduler_context.h"
#include "user_processes/sleep.h"
#include "user_processes/standard.h"
#include "user_processes/syscall_abi.h"
#include "user_processes/syscall_pointer.h"
#include "user_processes/waitpid.h"

#include "../process/process.h"
#include "../scheduler/scheduler.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


// Private helpers declarations
static void user_process_exec_terminated_handler(
    struct process *process);

static void user_process_lifecycle_stress_terminated_handler(
    struct process *process
);

static void user_process_sleep_terminated_handler(
    struct process *process
);

static void user_process_block_terminated_handler(
    struct process *process
);

static void user_process_syscall_abi_terminated_handler(
    struct process *process
);

static void user_process_scheduler_context_terminated_handler(
    struct process *process
);

static void user_process_syscall_pointer_terminated_handler(
    struct process *process
);

static void user_process_waitpid_terminated_handler(
    struct process *process
);
// End private helpers declarations

void user_process_tests_prepare(void)
{
    scheduler_set_terminated_handler(
        user_process_lifecycle_stress_terminated_handler
    );

    user_process_lifecycle_stress_test_prepare();
}

// Private helpers implementation
static void user_process_lifecycle_stress_terminated_handler(
    struct process *process)
{
    if (!user_process_lifecycle_stress_test_terminated(
        process
    )) {
        return;
    }

    /*
     * No standard processes have been registered yet.
     * The sleep probe will therefore be the only runnable
     * process and must pass through scheduler_idle().
     */
    scheduler_set_terminated_handler(
        user_process_sleep_terminated_handler
    );

    user_process_sleep_test_prepare();
}

static void user_process_sleep_terminated_handler(
    struct process *process)
{
    if (!user_process_sleep_test_terminated(
        process
    )) {
        return;
    }

    /*
     * Register the blocking process first. Its syscall must transfer
     * execution to the event worker, which remains READY.
     */
     scheduler_set_terminated_handler(
        user_process_block_terminated_handler
    );

    user_process_block_test_prepare();
}

static void user_process_block_terminated_handler(
    struct process *process)
{
    if (!user_process_block_test_terminated(
        process
    )) {
        return;
    }

    scheduler_set_terminated_handler(
        user_process_waitpid_terminated_handler
    );

    user_process_waitpid_test_prepare();
}

static void user_process_waitpid_terminated_handler(
    struct process *process)
{
    if (!user_process_waitpid_test_terminated(
        process
    )) {
        return;
    }

    scheduler_set_terminated_handler(
        user_process_syscall_abi_terminated_handler
    );

    user_process_syscall_abi_test_prepare();
}

static void user_process_syscall_abi_terminated_handler(
    struct process *process)
{
    user_process_syscall_abi_test_terminated(
        process
    );

    scheduler_set_terminated_handler(
        user_process_syscall_pointer_terminated_handler
    );

    user_process_syscall_pointer_test_prepare();
}

static void user_process_syscall_pointer_terminated_handler(
    struct process *process)
{
    user_process_syscall_pointer_test_terminated(
        process
    );

    scheduler_set_terminated_handler(
        user_process_scheduler_context_terminated_handler
    );

    user_process_scheduler_context_test_prepare();
}

static void user_process_scheduler_context_terminated_handler(
    struct process *process)
{
    if (!user_process_scheduler_context_test_terminated(
        process
    )) {
        return;
    }

    scheduler_set_terminated_handler(
        user_process_exec_terminated_handler
    );

    user_process_exec_test_prepare();
}

static void user_process_exec_terminated_handler(
    struct process *process)
{
    user_process_exec_test_terminated(
        process
    );

    scheduler_set_terminated_handler(
        user_process_standard_test_terminated
    );

    user_process_standard_test_prepare();
}
