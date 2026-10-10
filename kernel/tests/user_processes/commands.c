// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file commands.c
 * @brief Ring-3 Unix-style userspace command regression.
 */

#include "commands.h"

#include "../../core/panic.h"
#include "../../diagnostics/diagnostics.h"
#include "../../process/create.h"
#include "../../process/executable.h"
#include "../../process/fd_table.h"
#include "../../process/instance.h"
#include "../../process/path.h"
#include "../../process/process.h"
#include "../../scheduler/scheduler.h"
#include "../../vfs/root.h"
#include "../../vfs/vfs.h"

#include <stddef.h>
#include <stdint.h>

struct command_test_case {
    const char *path;
    size_t path_length;
    uint64_t expected_exit_status;

    size_t argument_count;
    const char *const *arguments;

    bool capture_stdout;

    const char *initial_cwd;

    const char *expected_stdout;
    size_t expected_stdout_length;
};

static const char *const command_test_echo_arguments[] = {
    "/bin/echo",
    "Hola",
    "desde",
    "MyOS",
};

static const struct command_test_case
command_test_cases[] = {
    {
        .path = "/bin/true",
        .path_length = sizeof("/bin/true") - 1U,
        .expected_exit_status = 0,
        .argument_count = 0,
        .arguments = NULL,
    },
    {
        .path = "/bin/false",
        .path_length = sizeof("/bin/false") - 1U,
        .expected_exit_status = 1,
        .argument_count = 0,
        .arguments = NULL,
    },
    {
        .path = "/bin/echo",
        .path_length = sizeof("/bin/echo") - 1U,
        .expected_exit_status = 0,
        .argument_count =
            sizeof(command_test_echo_arguments) /
            sizeof(command_test_echo_arguments[0]),
        .arguments = command_test_echo_arguments,
    },
    {
        .path = "/bin/pwd",
        .path_length = sizeof("/bin/pwd") - 1U,
        .expected_exit_status = 0,
        .argument_count = 0,
        .arguments = NULL,
        .capture_stdout = true,
        .initial_cwd = "/",
        .expected_stdout = "/\n",
        .expected_stdout_length = sizeof("/\n") - 1U,
    },
    {
        .path = "/bin/pwd",
        .path_length = sizeof("/bin/pwd") - 1U,
        .expected_exit_status = 0,
        .argument_count = 0,
        .arguments = NULL,
        .capture_stdout = true,
        .initial_cwd = "/bin",
        .expected_stdout = "/bin\n",
        .expected_stdout_length = sizeof("/bin\n") - 1U,
    },
};

#define COMMAND_TEST_COUNT \
    (sizeof(command_test_cases) / sizeof(command_test_cases[0]))

#define COMMAND_CAPTURE_CAPACITY 256U

static char command_capture_buffer[
    COMMAND_CAPTURE_CAPACITY
];

static size_t command_capture_length;

static bool command_capture_active;

static struct vfs_node command_capture_node;

static struct vfs_file command_capture_file;

static enum vfs_io_result command_capture_write(
    struct vfs_file *file,
    uint64_t offset,
    const void *buffer,
    size_t size,
    size_t *bytes_written)
{
    if (
        !command_capture_active ||
        file != &command_capture_file ||
        bytes_written == NULL ||
        (size != 0 && buffer == NULL)
    ) {
        return VFS_IO_RESULT_INVALID_ARGUMENT;
    }

    if (
        offset != command_capture_length ||
        size > COMMAND_CAPTURE_CAPACITY - command_capture_length
    ) {
        return VFS_IO_RESULT_RESOURCE_EXHAUSTED;
    }

    const unsigned char *source = buffer;

    for (size_t index = 0; index < size; ++index) {
        command_capture_buffer[
            command_capture_length + index
        ] = (char) source[index];
    }

    command_capture_length += size;
    *bytes_written = size;

    return VFS_IO_RESULT_SUCCESS;
}

static const struct vfs_file_operations
command_capture_operations = {
    .write = command_capture_write,
};

static struct process_instance *
command_test_instance;

static const struct vfs_mount_table *
command_test_mounts;

static size_t command_test_index;

static size_t
command_test_scheduler_count_baseline;

static uint64_t
command_test_pid;

static void command_test_start_current(void);

static void command_test_install_capture(void);

static void command_test_validate_capture(void);

static void command_test_release_capture(void);

void user_process_commands_test_prepare(
    const struct vfs_mount_table *mounts)
{
    if (mounts == NULL) {
        kernel_panic(
            "Command test received NULL mount topology"
        );
    }

    if (command_test_instance != NULL) {
        kernel_panic(
            "Command test already has an active process"
        );
    }

    command_test_mounts = mounts;
    command_test_index = 0;

    command_test_scheduler_count_baseline =
        scheduler_test_process_count();

    command_test_start_current();
}

bool user_process_commands_test_terminated(
    struct process *process)
{
    if (
        command_test_instance == NULL ||
        process == NULL ||
        process != &command_test_instance->process ||
        process->id != command_test_pid
    ) {
        kernel_panic(
            "Command test received unexpected process"
        );
    }

    const struct command_test_case *test_case =
        &command_test_cases[command_test_index];

    if (
        process->state != PROCESS_STATE_TERMINATED ||
        process->termination_reason !=
            PROCESS_TERMINATION_EXITED ||
        process->exit_status !=
            test_case->expected_exit_status
    ) {
        diagnostics_printf(
            "[userland] Command regression failed: "
            "PID=%u reason=%u status=%u\n",
            process->id,
            (uint64_t) process->termination_reason,
            process->exit_status
        );

        kernel_panic(
            "Userspace command produced unexpected result"
        );
    }

    if (test_case->capture_stdout) {
        command_test_validate_capture();
    }

    if (!process_release_terminated(
        command_test_instance
    )) {
        kernel_panic(
            "Unable to release userspace command process"
        );
    }

    if (test_case->capture_stdout) {
        command_test_release_capture();
    }

    command_test_instance = NULL;
    command_test_pid = 0;

    if (
        scheduler_test_process_count() !=
        command_test_scheduler_count_baseline
    ) {
        kernel_panic(
            "Userspace command leaked scheduler registration"
        );
    }

    diagnostics_printf(
        "[userland] %s exited with expected status\n",
        test_case->path
    );

    ++command_test_index;

    if (command_test_index < COMMAND_TEST_COUNT) {
        command_test_start_current();
        return false;
    }

    diagnostics_write(
        "[userland] Unix-style command regression passed\n"
    );

    return true;
}

static void command_test_start_current(void)
{
    if (
        command_test_index >= COMMAND_TEST_COUNT ||
        command_test_instance != NULL
    ) {
        kernel_panic(
            "Invalid userspace command test state"
        );
    }

    struct vfs_node *root = vfs_root_get();

    if (root == NULL) {
        kernel_panic(
            "Command test has no system root"
        );
    }

    const struct command_test_case *test_case =
        &command_test_cases[command_test_index];

    struct process_executable executable = {0};

    if (
        process_executable_open_elf64_from_namespace(
            root,
            command_test_mounts,
            test_case->path,
            test_case->path_length,
            &executable
        ) != PROCESS_EXECUTABLE_RESULT_SUCCESS
    ) {
        kernel_panic(
            "Unable to open userspace command executable"
        );
    }

    struct process_initial_vfs initial_vfs = {
        .namespace_root = root,
        .namespace_mounts = command_test_mounts,
        .provision_standard_descriptors = true,
    };

    const char *const default_argv[] = {
        test_case->path,
    };

    size_t argc = 1U;
    const char *const *argv = default_argv;

    if (test_case->arguments != NULL) {
        argc = test_case->argument_count;
        argv = test_case->arguments;
    }

    command_test_instance =
        process_create_elf64_with_vfs(
            &executable.image,
            &initial_vfs,
            argc,
            argv,
            0,
            NULL
        );

    if (command_test_instance == NULL) {
        if (!process_executable_close(&executable)) {
            kernel_panic(
                "Unable to close command ELF after creation failure"
            );
        }

        kernel_panic(
            "Unable to create userspace command process"
        );
    }

    if (test_case->initial_cwd != NULL) {
        size_t cwd_length = 0;

        while (test_case->initial_cwd[cwd_length] != '\0') {
            ++cwd_length;
        }

        if (
            process_chdir(
                command_test_instance,
                test_case->initial_cwd,
                cwd_length
            ) != PROCESS_PATH_RESULT_RESOLVED
        ) {
            kernel_panic(
                "Unable to establish command initial CWD"
            );
        }
    }

    command_test_pid = command_test_instance->process.id;

    /*
     * The initial process environment must provide the
     * conventional descriptors before its first instruction.
     */
    struct process_fd_table *descriptors =
        &command_test_instance->file_descriptors;

    struct vfs_file *stdin_file =
        process_fd_table_get(descriptors, 0);

    struct vfs_file *stdout_file =
        process_fd_table_get(descriptors, 1);

    struct vfs_file *stderr_file =
        process_fd_table_get(descriptors, 2);

    if (
        stdin_file == NULL ||
        stdout_file == NULL ||
        stderr_file == NULL ||
        stdin_file->access != VFS_OPEN_ACCESS_READ ||
        stdout_file->access != VFS_OPEN_ACCESS_WRITE ||
        stderr_file->access != VFS_OPEN_ACCESS_WRITE ||
        stdout_file == stderr_file
    ) {
        kernel_panic(
            "Userspace command received invalid standard descriptors"
        );
    }

    if (test_case->capture_stdout) {
        command_test_install_capture();
    }

    if (!process_executable_close(&executable)) {
        kernel_panic(
            "Unable to close command ELF after materialization"
        );
    }

    if (
        scheduler_test_process_count() !=
        command_test_scheduler_count_baseline + 1U
    ) {
        kernel_panic(
            "Userspace command was not registered exactly once"
        );
    }
}

static void command_test_install_capture(void)
{
    if (
        command_test_instance == NULL ||
        command_capture_active
    ) {
        kernel_panic(
            "Invalid command stdout capture state"
        );
    }

    command_capture_length = 0;

    if (!vfs_node_initialize(
        &command_capture_node,
        VFS_NODE_TYPE_REGULAR_FILE,
        NULL,
        NULL
    )) {
        kernel_panic(
            "Unable to initialize stdout capture node"
        );
    }

    if (!vfs_file_initialize(
        &command_capture_file,
        &command_capture_node,
        VFS_OPEN_ACCESS_WRITE,
        &command_capture_operations,
        NULL
    )) {
        kernel_panic(
            "Unable to initialize stdout capture file"
        );
    }

    struct process_fd_table *descriptors =
        &command_test_instance->file_descriptors;

    if (!process_fd_table_close(
        descriptors,
        1U
    )) {
        kernel_panic(
            "Unable to close original stdout descriptor"
        );
    }

    command_capture_active = true;

    if (!process_fd_table_install_at(
        descriptors,
        1U,
        &command_capture_file
    )) {
        kernel_panic(
            "Unable to install stdout capture descriptor"
        );
    }

    if (
        process_fd_table_get(descriptors, 1U) !=
        &command_capture_file ||
        command_capture_file.reference_count != 2U ||
        command_capture_node.reference_count != 2U
    ) {
        kernel_panic(
            "Invalid stdout capture ownership"
        );
    }
}

static void command_test_validate_capture(void)
{
    const struct command_test_case *test_case =
        &command_test_cases[command_test_index];

    if (
        !command_capture_active ||
        !test_case->capture_stdout ||
        test_case->expected_stdout == NULL
    ) {
        kernel_panic(
            "Invalid command stdout capture validation state"
        );
    }

    if (
        command_capture_length !=
        test_case->expected_stdout_length
    ) {
        kernel_panic(
            "Command produced unexpected stdout length"
        );
    }

    for (
        size_t index = 0;
        index < test_case->expected_stdout_length;
        ++index
    ) {
        if (
            command_capture_buffer[index] !=
            test_case->expected_stdout[index]
        ) {
            kernel_panic(
                "Command produced unexpected stdout bytes"
            );
        }
    }

    diagnostics_printf(
        "[userland] %s stdout content verified: cwd=%s\n",
        test_case->path,
        test_case->initial_cwd != NULL
            ? test_case->initial_cwd
            : "(default)"
    );
}

static void command_test_release_capture(void)
{
    if (
        !command_capture_active ||
        command_capture_file.reference_count != 1U ||
        command_capture_node.reference_count != 2U
    ) {
        kernel_panic(
            "Stdout capture leaked descriptor ownership"
        );
    }

    if (!vfs_file_release(
        &command_capture_file
    )) {
        kernel_panic(
            "Unable to release stdout capture file"
        );
    }

    if (
        command_capture_file.reference_count != 0U ||
        command_capture_node.reference_count != 1U
    ) {
        kernel_panic(
            "Stdout capture file release contract failed"
        );
    }

    if (!vfs_node_release(
        &command_capture_node
    )) {
        kernel_panic(
            "Unable to release stdout capture node"
        );
    }

    command_capture_active = false;
    command_capture_length = 0;
}
