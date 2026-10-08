// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file user_processes.c
 * @brief User-process scheduler test fixtures.
 */

#include "user_processes.h"
#include "user_processes/block.h"
#include "user_processes/elf_isolation.h"
#include "user_processes/elf_protection.h"
#include "user_processes/exec.h"
#include "user_processes/fork.h"
#include "user_processes/lifecycle_stress.h"
#include "user_processes/runtime_malloc.h"
#include "user_processes/runtime_string.h"
#include "user_processes/scheduler_context.h"
#include "user_processes/sleep.h"
#include "user_processes/standard.h"
#include "user_processes/syscall_abi.h"
#include "user_processes/syscall_pointer.h"
#include "user_processes/fd_syscalls.h"
#include "user_processes/vfs_elf.h"
#include "user_processes/waitpid.h"

#include "../core/panic.h"
#include "../process/process.h"
#include "../scheduler/scheduler.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static const struct vfs_mount_table *
user_process_test_mounts;

// Private helpers declarations
static void user_process_exec_terminated_handler(
    struct process *process);

static void user_process_elf_protection_terminated_handler(
    struct process *process
);

static void user_process_elf_isolation_terminated_handler(
    struct process *process
);

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

static void user_process_fd_syscalls_terminated_handler(
    struct process *process
);

static void user_process_waitpid_terminated_handler(
    struct process *process
);

static void user_process_fork_terminated_handler(
    struct process *process
);

static void user_process_vfs_elf_terminated_handler(
    struct process *process
);

static void user_process_runtime_string_terminated_handler(
    struct process *process
);

static void user_process_runtime_malloc_terminated_handler(
    struct process *process
);
// End private helpers declarations

void user_process_tests_prepare(
    const struct vfs_mount_table *mounts)
{
    if (mounts == NULL) {
        kernel_panic(
            "User-process tests received NULL mount topology"
        );
    }

    user_process_test_mounts =
        mounts;

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
        user_process_fork_terminated_handler
    );

    user_process_fork_test_prepare();
}

static void user_process_fork_terminated_handler(
    struct process *process)
{
    if (!user_process_fork_test_terminated(
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
        user_process_fd_syscalls_terminated_handler
    );

    user_process_fd_syscalls_test_prepare();
}

static void user_process_fd_syscalls_terminated_handler(
    struct process *process)
{
    user_process_fd_syscalls_test_terminated(
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
        user_process_elf_protection_terminated_handler
    );

    user_process_elf_protection_test_prepare();
}

static void user_process_elf_protection_terminated_handler(
    struct process *process)
{
    user_process_elf_protection_test_terminated(
        process
    );

    scheduler_set_terminated_handler(
        user_process_elf_isolation_terminated_handler
    );

    user_process_elf_isolation_test_prepare();
}

static void user_process_elf_isolation_terminated_handler(
    struct process *process)
{
    if (!user_process_elf_isolation_test_terminated(
        process
    )) {
        return;
    }

    scheduler_set_terminated_handler(
        user_process_vfs_elf_terminated_handler
    );

    user_process_vfs_elf_test_prepare(
        user_process_test_mounts
    );
}

static void user_process_vfs_elf_terminated_handler(
    struct process *process)
{
    user_process_vfs_elf_test_terminated(
        process
    );

    scheduler_set_terminated_handler(
        user_process_runtime_string_terminated_handler
    );

    user_process_runtime_string_test_prepare(
        user_process_test_mounts
    );
}

static void user_process_runtime_string_terminated_handler(
    struct process *process)
{
    user_process_runtime_string_test_terminated(
        process
    );

    scheduler_set_terminated_handler(
        user_process_runtime_malloc_terminated_handler
    );

    user_process_runtime_malloc_test_prepare(
        user_process_test_mounts
    );
}

static void user_process_runtime_malloc_terminated_handler(
    struct process *process)
{
    user_process_runtime_malloc_test_terminated(
        process
    );

    scheduler_set_terminated_handler(
        user_process_standard_test_terminated
    );

    user_process_standard_test_prepare();
}
