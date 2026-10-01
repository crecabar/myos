// SPDX-License-Identifier: GPL-2.0-only

#include "syscall.h"

#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../process/file.h"
#include "../process/user_copy.h"
#include "../process/wait.h"
#include "../scheduler/scheduler.h"
#include "../vfs/path.h"

#include <stddef.h>
#include <stdint.h>

#define SYSCALL_WRITE_MAX_SIZE 256
#define SYSCALL_FD_IO_BUFFER_SIZE 256

_Static_assert(
    SYSCALL_PATH_MAX <= VFS_PATH_MAX,
    "Syscall path limit exceeds VFS path capacity"
);

// Private functions and helpers declarations
static syscall_result_t syscall_write(
    uint64_t user_address,
    uint64_t length);

static uint64_t syscall_wait_termination_reason(
    enum process_termination_reason reason
);

static struct process_instance *syscall_current_instance(void);

static enum syscall_error syscall_file_map_error(
    enum process_file_result result
);

static syscall_result_t syscall_fd_close(
    uint64_t descriptor
);

static syscall_result_t syscall_fd_open(
    uint64_t user_path_address,
    uint64_t path_length,
    uint64_t access
);

static bool syscall_fd_open_access(
    uint64_t syscall_access,
    enum vfs_open_access *vfs_access
);

static syscall_result_t syscall_fd_write(
    uint64_t descriptor,
    uint64_t user_address,
    uint64_t length
);

static syscall_result_t syscall_fd_read(
    uint64_t descriptor,
    uint64_t user_address,
    uint64_t length
);

static syscall_result_t syscall_fd_fstat(
    uint64_t descriptor,
    uint64_t user_stat_address
);

static bool syscall_file_type(
    enum vfs_node_type type,
    uint64_t *syscall_type
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

        case SYSCALL_FD_CLOSE:
            return syscall_fd_close(
                argument0
            );

        case SYSCALL_FD_OPEN:
            return syscall_fd_open(
                argument0,
                argument1,
                argument2
            );

        case SYSCALL_FD_WRITE:
            return syscall_fd_write(
                argument0,
                argument1,
                argument2
            );

        case SYSCALL_FD_READ:
            return syscall_fd_read(
                argument0,
                argument1,
                argument2
            );

        case SYSCALL_FD_FSTAT:
            return syscall_fd_fstat(
                argument0,
                argument1
            );

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

static struct process_instance *syscall_current_instance(void)
{
    struct process *process =
        scheduler_current();

    if (
        process == NULL ||
        process->instance == NULL ||
        process->instance->process.instance !=
            process->instance
    ) {
        return NULL;
    }

    return
        process->instance;
}

static enum syscall_error syscall_file_map_error(
    enum process_file_result result)
{
    switch (result) {
        case PROCESS_FILE_RESULT_INVALID_ARGUMENT:
            return
                SYSCALL_ERROR_INVALID_ARGUMENT;

        case PROCESS_FILE_RESULT_BAD_DESCRIPTOR:
            return
                SYSCALL_ERROR_BAD_DESCRIPTOR;

        case PROCESS_FILE_RESULT_NOT_FOUND:
            return
                SYSCALL_ERROR_NOT_FOUND;

        case PROCESS_FILE_RESULT_NO_NAMESPACE_ROOT:
        case PROCESS_FILE_RESULT_NO_CURRENT_DIRECTORY:
            return
                SYSCALL_ERROR_INVALID_ARGUMENT;

        case PROCESS_FILE_RESULT_NOT_DIRECTORY:
            return
                SYSCALL_ERROR_NOT_DIRECTORY;

        case PROCESS_FILE_RESULT_NOT_SUPPORTED:
            return
                SYSCALL_ERROR_NOT_SUPPORTED;

        case PROCESS_FILE_RESULT_ACCESS_DENIED:
            return
                SYSCALL_ERROR_ACCESS_DENIED;

        case PROCESS_FILE_RESULT_RESOURCE_EXHAUSTED:
            return
                SYSCALL_ERROR_RESOURCE_EXHAUSTED;

        case PROCESS_FILE_RESULT_SUCCESS:
            kernel_panic(
                "Successful process file result cannot map to syscall error"
            );
    }

    kernel_panic(
        "Unknown process file result"
    );
}

static syscall_result_t syscall_fd_close(
    uint64_t descriptor)
{
    struct process_instance *instance =
        syscall_current_instance();

    if (instance == NULL) {
        return syscall_result_error(
            SYSCALL_ERROR_INVALID_ARGUMENT
        );
    }

    enum process_file_result result =
        process_file_close(
            instance,
            (size_t) descriptor
        );

    if (
        result !=
        PROCESS_FILE_RESULT_SUCCESS
    ) {
        return syscall_result_error(
            syscall_file_map_error(
                result
            )
        );
    }

    return 0;
}

static bool syscall_fd_open_access(
    uint64_t syscall_access,
    enum vfs_open_access *vfs_access)
{
    if (vfs_access == NULL) {
        return false;
    }

    if (
        syscall_access == 0 ||
        (
            syscall_access &
            ~(
                SYSCALL_OPEN_ACCESS_READ |
                SYSCALL_OPEN_ACCESS_WRITE
            )
        ) != 0
    ) {
        return false;
    }

    enum vfs_open_access result = 0;

    if (
        (syscall_access &
         SYSCALL_OPEN_ACCESS_READ) != 0
    ) {
        result |=
            VFS_OPEN_ACCESS_READ;
    }

    if (
        (syscall_access &
         SYSCALL_OPEN_ACCESS_WRITE) != 0
    ) {
        result |=
            VFS_OPEN_ACCESS_WRITE;
    }

    *vfs_access =
        result;

    return true;
}

static syscall_result_t syscall_fd_open(
    uint64_t user_path_address,
    uint64_t path_length,
    uint64_t access)
{
    if (
        path_length == 0 ||
        path_length > SYSCALL_PATH_MAX
    ) {
        return syscall_result_error(
            SYSCALL_ERROR_INVALID_ARGUMENT
        );
    }

    enum vfs_open_access vfs_access;

    if (!syscall_fd_open_access(
        access,
        &vfs_access
    )) {
        return syscall_result_error(
            SYSCALL_ERROR_INVALID_ARGUMENT
        );
    }

    struct process_instance *instance =
        syscall_current_instance();

    if (
        instance == NULL ||
        instance->process.memory == NULL
    ) {
        return syscall_result_error(
            SYSCALL_ERROR_INVALID_ARGUMENT
        );
    }

    char path[VFS_PATH_MAX];

    if (!copy_from_user(
        instance->process.memory,
        user_path_address,
        path,
        (size_t) path_length
    )) {
        return syscall_result_error(
            SYSCALL_ERROR_BAD_ADDRESS
        );
    }

    size_t descriptor;

    enum process_file_result result =
        process_file_open(
            instance,
            path,
            (size_t) path_length,
            vfs_access,
            &descriptor
        );

    if (
        result !=
        PROCESS_FILE_RESULT_SUCCESS
    ) {
        return syscall_result_error(
            syscall_file_map_error(
                result
            )
        );
    }

    return
        (syscall_result_t) descriptor;
}

static syscall_result_t syscall_fd_write(
    uint64_t descriptor,
    uint64_t user_address,
    uint64_t length)
{
    /*
     * Successful syscall results must be representable by syscall_result_t.
     */
    if (length > (uint64_t) INT64_MAX) {
        return syscall_result_error(
            SYSCALL_ERROR_OVERFLOW
        );
    }

    struct process_instance *instance =
        syscall_current_instance();

    if (
        instance == NULL ||
        instance->process.memory == NULL
    ) {
        return syscall_result_error(
            SYSCALL_ERROR_INVALID_ARGUMENT
        );
    }

    /*
     * A zero-length operation still validates the descriptor and access mode,
     * but intentionally does not inspect the userspace address.
     */
    if (length == 0) {
        size_t bytes_written;

        enum process_file_result result =
            process_file_write(
                instance,
                (size_t) descriptor,
                NULL,
                0,
                &bytes_written
            );

        if (
            result !=
            PROCESS_FILE_RESULT_SUCCESS
        ) {
            return syscall_result_error(
                syscall_file_map_error(
                    result
                )
            );
        }

        return 0;
    }

    /*
     * Validate the complete source before allowing any backend side effect.
     */
    if (!user_copy_range_readable(
        instance->process.memory,
        user_address,
        (size_t) length
    )) {
        return syscall_result_error(
            SYSCALL_ERROR_BAD_ADDRESS
        );
    }

    uint8_t buffer[SYSCALL_FD_IO_BUFFER_SIZE];

    size_t total_written =
        0;

    while (
        total_written <
        (size_t) length
    ) {
        size_t remaining =
            (size_t) length -
            total_written;

        size_t chunk_size =
            remaining <
                sizeof(buffer)
                ? remaining
                : sizeof(buffer);

        if (!copy_from_user(
            instance->process.memory,
            user_address +
                (uint64_t) total_written,
            buffer,
            chunk_size
        )) {
            /*
             * Full-range validation already succeeded. If mappings become
             * mutable concurrently in the future, preserve any completed
             * progress instead of reporting an error after side effects.
             */
            if (total_written != 0) {
                return
                    (syscall_result_t)
                        total_written;
            }

            return syscall_result_error(
                SYSCALL_ERROR_BAD_ADDRESS
            );
        }

        size_t chunk_written =
            0;

        enum process_file_result result =
            process_file_write(
                instance,
                (size_t) descriptor,
                buffer,
                chunk_size,
                &chunk_written
            );

        if (
            result !=
            PROCESS_FILE_RESULT_SUCCESS
        ) {
            if (total_written != 0) {
                return
                    (syscall_result_t)
                        total_written;
            }

            return syscall_result_error(
                syscall_file_map_error(
                    result
                )
            );
        }

        total_written +=
            chunk_written;

        /*
         * A short successful write is visible to userspace immediately.
         * Do not repeatedly call a backend that chose not to consume the
         * complete requested chunk.
         */
        if (chunk_written < chunk_size) {
            break;
        }
    }

    return
        (syscall_result_t)
            total_written;
}

static syscall_result_t syscall_fd_read(
    uint64_t descriptor,
    uint64_t user_address,
    uint64_t length)
{
    /*
     * Successful syscall results must be representable by syscall_result_t.
     */
    if (length > (uint64_t) INT64_MAX) {
        return syscall_result_error(
            SYSCALL_ERROR_OVERFLOW
        );
    }

    struct process_instance *instance =
        syscall_current_instance();

    if (
        instance == NULL ||
        instance->process.memory == NULL
    ) {
        return syscall_result_error(
            SYSCALL_ERROR_INVALID_ARGUMENT
        );
    }

    /*
     * A zero-length operation still validates the descriptor and access mode,
     * but intentionally does not inspect the userspace address.
     */
    if (length == 0) {
        size_t bytes_read;

        enum process_file_result result =
            process_file_read(
                instance,
                (size_t) descriptor,
                NULL,
                0,
                &bytes_read
            );

        if (
            result !=
            PROCESS_FILE_RESULT_SUCCESS
        ) {
            return syscall_result_error(
                syscall_file_map_error(
                    result
                )
            );
        }

        return 0;
    }

    /*
     * Validate the complete destination before allowing the backend to consume
     * any data or advance shared open-file state.
     */
    if (!user_copy_range_writable(
        instance->process.memory,
        user_address,
        (size_t) length
    )) {
        return syscall_result_error(
            SYSCALL_ERROR_BAD_ADDRESS
        );
    }

    uint8_t buffer[SYSCALL_FD_IO_BUFFER_SIZE];

    size_t total_read =
        0;

    while (
        total_read <
        (size_t) length
    ) {
        size_t remaining =
            (size_t) length -
            total_read;

        size_t chunk_size =
            remaining <
                sizeof(buffer)
                ? remaining
                : sizeof(buffer);

        size_t chunk_read =
            0;

        enum process_file_result result =
            process_file_read(
                instance,
                (size_t) descriptor,
                buffer,
                chunk_size,
                &chunk_read
            );

        if (
            result !=
            PROCESS_FILE_RESULT_SUCCESS
        ) {
            if (total_read != 0) {
                return
                    (syscall_result_t)
                        total_read;
            }

            return syscall_result_error(
                syscall_file_map_error(
                    result
                )
            );
        }

        /*
         * The complete destination range was validated before the first
         * backend operation. Under the current single-CPU memory model its
         * accessibility cannot change during this syscall.
         */
        if (
            chunk_read != 0 &&
            !copy_to_user(
                instance->process.memory,
                user_address +
                    (uint64_t) total_read,
                buffer,
                chunk_read
            )
        ) {
            kernel_panic(
                "Validated descriptor read destination became inaccessible"
            );
        }

        total_read +=
            chunk_read;

        /*
         * EOF is represented by a successful zero-byte read. Any other short
         * read is likewise returned immediately rather than issuing another
         * backend operation.
         */
        if (chunk_read < chunk_size) {
            break;
        }
    }

    return
        (syscall_result_t)
            total_read;
}

static bool syscall_file_type(
    enum vfs_node_type type,
    uint64_t *syscall_type)
{
    if (syscall_type == NULL) {
        return false;
    }

    switch (type) {
        case VFS_NODE_TYPE_REGULAR_FILE:
            *syscall_type =
                SYSCALL_FILE_TYPE_REGULAR_FILE;
            return true;

        case VFS_NODE_TYPE_DIRECTORY:
            *syscall_type =
                SYSCALL_FILE_TYPE_DIRECTORY;
            return true;

        case VFS_NODE_TYPE_CHARACTER_DEVICE:
            *syscall_type =
                SYSCALL_FILE_TYPE_CHARACTER_DEVICE;
            return true;
    }

    return false;
}

static syscall_result_t syscall_fd_fstat(
    uint64_t descriptor,
    uint64_t user_stat_address)
{
    struct process_instance *instance =
        syscall_current_instance();

    if (
        instance == NULL ||
        instance->process.memory == NULL
    ) {
        return syscall_result_error(
            SYSCALL_ERROR_INVALID_ARGUMENT
        );
    }

    /*
     * Validate the complete userspace destination before consulting the
     * filesystem backend.
     */
    if (!user_copy_range_writable(
        instance->process.memory,
        user_stat_address,
        sizeof(struct syscall_file_stat)
    )) {
        return syscall_result_error(
            SYSCALL_ERROR_BAD_ADDRESS
        );
    }

    struct vfs_stat metadata;

    enum process_file_result result =
        process_file_stat(
            instance,
            (size_t) descriptor,
            &metadata
        );

    if (
        result !=
        PROCESS_FILE_RESULT_SUCCESS
    ) {
        return syscall_result_error(
            syscall_file_map_error(
                result
            )
        );
    }

    uint64_t syscall_type;

    if (!syscall_file_type(
        metadata.type,
        &syscall_type
    )) {
        return syscall_result_error(
            SYSCALL_ERROR_NOT_SUPPORTED
        );
    }

    struct syscall_file_stat user_stat = {
        .type =
            syscall_type,
        .size =
            metadata.size,
    };

    /*
     * The complete destination was already validated. Under the current
     * single-CPU address-space model, failure here indicates a broken kernel
     * invariant rather than a normal userspace fault.
     */
    if (!copy_to_user(
        instance->process.memory,
        user_stat_address,
        &user_stat,
        sizeof(user_stat)
    )) {
        kernel_panic(
            "Validated descriptor stat destination became inaccessible"
        );
    }

    return 0;
}
