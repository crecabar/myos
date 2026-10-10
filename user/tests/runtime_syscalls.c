// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file runtime_syscalls.c
 * @brief Ring-3 native userspace syscall-wrapper regression.
 */

#include <myos/abi/syscall.h>
#include <myos/file.h>
#include <myos/process.h>

#include <stddef.h>
#include <stdint.h>

#define RUNTIME_SYSCALLS_CHILD_EXIT_STATUS 42

static int runtime_test_uname(void)
{
    struct syscall_utsname identity = {0};

    syscall_result_t result =
        myos_uname(&identity);

    if (result != 0) {
        return 48;
    }

    static const char expected_sysname[] = "MyOS";
    static const char expected_release[] = "0.2.0";
    static const char expected_machine[] = "x86_64";

    const char *actual[] = {
        identity.sysname,
        identity.release,
        identity.machine,
    };

    const char *expected[] = {
        expected_sysname,
        expected_release,
        expected_machine,
    };

    const size_t lengths[] = {
        sizeof(expected_sysname),
        sizeof(expected_release),
        sizeof(expected_machine),
    };

    for (size_t field = 0; field < 3U; ++field) {
        for (size_t index = 0; index < lengths[field]; ++index) {
            if (actual[field][index] != expected[field][index]) {
                return 49;
            }
        }
    }

    if (
        identity.nodename[0] != '\0' ||
        identity.version[0] != '\0'
    ) {
        return 50;
    }

    return 0;
}

static int runtime_test_uname_invalid_destinations(void)
{
    syscall_result_t result =
        myos_uname(NULL);

    if (
        result != syscall_result_error(
            SYSCALL_ERROR_BAD_ADDRESS
        )
    ) {
        return 51;
    }

    /*
     * An address outside the canonical userspace range
     * must never be accepted as a destination.
     */
    result = myos_uname(
        (struct syscall_utsname *)
            (uintptr_t) UINT64_MAX
    );

    if (
        result != syscall_result_error(
            SYSCALL_ERROR_BAD_ADDRESS
        )
    ) {
        return 52;
    }

    /*
     * This object resides in the executable's read-only
     * data. The kernel must not write into that mapping.
     */
    static const char readonly_destination[] =
        "uname-readonly-probe";

    result = myos_uname(
        (struct syscall_utsname *)
            (uintptr_t) readonly_destination
    );

    if (
        result != syscall_result_error(
            SYSCALL_ERROR_BAD_ADDRESS
        )
    ) {
        return 53;
    }

    static const char expected[] =
        "uname-readonly-probe";

    for (
        size_t index = 0;
        index < sizeof(expected);
        ++index
    ) {
        if (
            readonly_destination[index] !=
            expected[index]
        ) {
            return 54;
        }
    }

    return 0;
}

int main(
    int argc,
    char *argv[],
    char *envp[])
{
    (void) argc;
    (void) argv;
    (void) envp;

    static const char executable_path[] =
        "/bin/runtime-syscalls";

    syscall_result_t descriptor =
        myos_fd_open(
            executable_path,
            sizeof(executable_path) - 1U,
            SYSCALL_OPEN_ACCESS_READ
        );

    if (descriptor < 0) {
        return 1;
    }

    struct syscall_file_stat status = {0};

    syscall_result_t result =
        myos_fd_fstat(
            (uint64_t) descriptor,
            &status
        );

    if (result != 0) {
        return 2;
    }

    if (
        status.type !=
            SYSCALL_FILE_TYPE_REGULAR_FILE ||
        status.size < 4U
    ) {
        return 3;
    }

    unsigned char elf_magic[4] = {0};

    result =
        myos_fd_read(
            (uint64_t) descriptor,
            elf_magic,
            sizeof(elf_magic)
        );

    if (
        result !=
        (syscall_result_t) sizeof(elf_magic)
    ) {
        return 4;
    }

    if (
        elf_magic[0] != 0x7fU ||
        elf_magic[1] != (unsigned char) 'E' ||
        elf_magic[2] != (unsigned char) 'L' ||
        elf_magic[3] != (unsigned char) 'F'
    ) {
        return 5;
    }

    result =
        myos_fd_lseek(
            (uint64_t) descriptor,
            0,
            SYSCALL_SEEK_ORIGIN_START
        );

    if (result != 0) {
        return 6;
    }

    unsigned char first_byte = 0;

    result =
        myos_fd_read(
            (uint64_t) descriptor,
            &first_byte,
            sizeof(first_byte)
        );

    if (
        result !=
            (syscall_result_t) sizeof(first_byte) ||
        first_byte != 0x7fU
    ) {
        return 7;
    }

    /*
     * The previous read advanced the shared open-file position to one.
     * Seeking back by one exercises the signed int64_t offset across the
     * unsigned syscall-register ABI boundary.
     */
    result =
        myos_fd_lseek(
            (uint64_t) descriptor,
            -1,
            SYSCALL_SEEK_ORIGIN_CURRENT
        );

    if (result != 0) {
        return 8;
    }

    first_byte = 0;

    result =
        myos_fd_read(
            (uint64_t) descriptor,
            &first_byte,
            sizeof(first_byte)
        );

    if (
        result !=
            (syscall_result_t) sizeof(first_byte) ||
        first_byte != 0x7fU
    ) {
        return 9;
    }

    static const unsigned char write_probe =
        0x5aU;

    /*
     * The executable was opened read-only. Verify that the native write
     * wrapper preserves the descriptor access-mode rejection.
     */
    result =
        myos_fd_write(
            (uint64_t) descriptor,
            &write_probe,
            sizeof(write_probe)
        );

    if (
        result !=
        syscall_result_error(
            SYSCALL_ERROR_ACCESS_DENIED
        )
    ) {
        return 10;
    }

    result =
        myos_fd_close(
            (uint64_t) descriptor
        );

    if (result != 0) {
        return 11;
    }

    result =
        myos_fd_close(
            (uint64_t) descriptor
        );

    if (
        result !=
        syscall_result_error(
            SYSCALL_ERROR_BAD_DESCRIPTOR
        )
    ) {
        return 12;
    }

    /*
     * Exercise a successful descriptor write through the real /dev mount,
     * devfs and null character-device path.
     */
    static const char null_path[] =
        "/dev/null";

    syscall_result_t null_descriptor =
        myos_fd_open(
            null_path,
            sizeof(null_path) - 1U,
            SYSCALL_OPEN_ACCESS_WRITE
        );

    if (null_descriptor < 0) {
        return 13;
    }

    result =
        myos_fd_write(
            (uint64_t) null_descriptor,
            &write_probe,
            sizeof(write_probe)
        );

    if (
        result !=
        (syscall_result_t) sizeof(write_probe)
    ) {
        return 14;
    }

    result =
        myos_fd_close(
            (uint64_t) null_descriptor
        );

    if (result != 0) {
        return 15;
    }

    /*
     * Change into the mounted devfs and exercise pathname resolution relative
     * to the new current directory.
     */
    static const char dev_path[] =
        "/dev";

    result =
        myos_chdir(
            dev_path,
            sizeof(dev_path) - 1U
        );

    if (result != 0) {
        return 16;
    }

    char cwd[16] = {0};

    result =
        myos_getcwd(
            cwd,
            sizeof("/dev")
        );

    if (
        result !=
            (syscall_result_t)
                (sizeof("/dev") - 1U) ||
        cwd[0] != '/' ||
        cwd[1] != 'd' ||
        cwd[2] != 'e' ||
        cwd[3] != 'v' ||
        cwd[4] != '\0'
    ) {
        return 33;
    }

    result =
        myos_getcwd(
            cwd,
            sizeof("/dev") - 1U
        );

    if (
        result !=
        syscall_result_error(
            SYSCALL_ERROR_OVERFLOW
        )
    ) {
        return 34;
    }

    result =
        myos_getcwd(
            NULL,
            sizeof("/dev")
        );

    if (
        result !=
        syscall_result_error(
            SYSCALL_ERROR_BAD_ADDRESS
        )
    ) {
        return 35;
    }

    /*
     * Failed directory changes must leave the existing CWD unchanged.
     */
    static const char missing_path[] =
        "missing";

    result =
        myos_chdir(
            missing_path,
            sizeof(missing_path) - 1U
        );

    if (
        result !=
        syscall_result_error(
            SYSCALL_ERROR_NOT_FOUND
        )
    ) {
        return 17;
    }

    static const char relative_null_path[] =
        "null";

    result =
        myos_chdir(
            relative_null_path,
            sizeof(relative_null_path) - 1U
        );

    if (
        result !=
        syscall_result_error(
            SYSCALL_ERROR_NOT_DIRECTORY
        )
    ) {
        return 18;
    }

    /*
     * Failed chdir operations above must not have changed the current
     * directory.
     */
    result =
        myos_getcwd(
            cwd,
            sizeof("/dev")
        );

    if (
        result !=
            (syscall_result_t)
                (sizeof("/dev") - 1U) ||
        cwd[0] != '/' ||
        cwd[1] != 'd' ||
        cwd[2] != 'e' ||
        cwd[3] != 'v' ||
        cwd[4] != '\0'
    ) {
        return 36;
    }

    syscall_result_t relative_null_descriptor =
        myos_fd_open(
            relative_null_path,
            sizeof(relative_null_path) - 1U,
            SYSCALL_OPEN_ACCESS_WRITE
        );

    if (relative_null_descriptor < 0) {
        return 19;
    }

    result =
        myos_fd_write(
            (uint64_t) relative_null_descriptor,
            &write_probe,
            sizeof(write_probe)
        );

    if (
        result !=
        (syscall_result_t) sizeof(write_probe)
    ) {
        return 20;
    }

    result =
        myos_fd_close(
            (uint64_t) relative_null_descriptor
        );

    if (result != 0) {
        return 21;
    }

    /*
     * Restore the root CWD and prove subsequent relative resolution begins
     * there again.
     */
    static const char root_path[] =
        "/";

    result =
        myos_chdir(
            root_path,
            sizeof(root_path) - 1U
        );

    if (result != 0) {
        return 22;
    }

    result =
        myos_getcwd(
            cwd,
            sizeof("/")
        );

    if (
        result !=
            (syscall_result_t)
                (sizeof("/") - 1U) ||
        cwd[0] != '/' ||
        cwd[1] != '\0'
    ) {
        return 37;
    }

    static const char relative_executable_path[] =
        "bin/runtime-syscalls";

    syscall_result_t relative_descriptor =
        myos_fd_open(
            relative_executable_path,
            sizeof(relative_executable_path) - 1U,
            SYSCALL_OPEN_ACCESS_READ
        );

    if (relative_descriptor < 0) {
        return 23;
    }

    result =
        myos_fd_close(
            (uint64_t) relative_descriptor
        );

    if (result != 0) {
        return 24;
    }

    syscall_result_t parent_pid =
        myos_getpid();

    if (parent_pid <= 0) {
        return 25;
    }

    syscall_result_t child_pid =
        myos_fork();

    if (child_pid < 0) {
        return 26;
    }

    if (child_pid == 0) {
        syscall_result_t child_self_pid =
            myos_getpid();

        if (
            child_self_pid <= 0 ||
            child_self_pid == parent_pid
        ) {
            return 41;
        }

        return
            RUNTIME_SYSCALLS_CHILD_EXIT_STATUS;
    }

    if (child_pid == parent_pid) {
        return 27;
    }

    if (myos_getpid() != parent_pid) {
        return 28;
    }

    struct syscall_wait_status wait_status = {0};

    result =
        myos_waitpid(
            (uint64_t) child_pid,
            &wait_status,
            0
        );

    if (result != child_pid) {
        return 29;
    }

    if (
        wait_status.termination_reason !=
        SYSCALL_WAIT_TERMINATION_EXITED
    ) {
        return 30;
    }

    if (
        wait_status.exit_status !=
        RUNTIME_SYSCALLS_CHILD_EXIT_STATUS
    ) {
        return 31;
    }

    result =
        myos_waitpid(
            (uint64_t) child_pid,
            &wait_status,
            0
        );

    if (
        result !=
        syscall_result_error(
            SYSCALL_ERROR_NO_CHILD
        )
    ) {
        return 32;
    }

    int uname_result = runtime_test_uname();

    if (uname_result != 0) {
        return uname_result;
    }

    int uname_invalid_result =
        runtime_test_uname_invalid_destinations();

    if (uname_invalid_result != 0) {
        return uname_invalid_result;
    }

    /*
     * Failed execve operations must return to the original image without
     * changing process identity.
     */
    static const char exec_path[] =
        "bin/exec-target";

    static const char missing_exec_path[] =
        "/bin/missing-exec-target";

    static const char exec_argument0[] =
        "exec-target";

    static const char exec_argument1[] =
        "replacement";

    static const char *const exec_argv[] = {
        exec_argument0,
        exec_argument1,
    };

    static const char exec_environment0[] =
        "EXEC=1";

    static const char *const exec_envp[] = {
        exec_environment0,
    };

    /*
     * A non-empty argv vector requires a readable userspace vector address.
     * The executable pathname itself is valid so this failure specifically
     * exercises argv import.
     */
    result =
        myos_execve(
            exec_path,
            sizeof(exec_path) - 1U,
            NULL,
            1,
            exec_envp,
            sizeof(exec_envp) /
                sizeof(exec_envp[0])
        );

    if (
        result !=
        syscall_result_error(
            SYSCALL_ERROR_BAD_ADDRESS
        )
    ) {
        return 43;
    }

    if (myos_getpid() != parent_pid) {
        return 44;
    }

    /*
     * A missing executable must fail before replacing the current image.
     */
    result =
        myos_execve(
            missing_exec_path,
            sizeof(missing_exec_path) - 1U,
            exec_argv,
            sizeof(exec_argv) /
                sizeof(exec_argv[0]),
            exec_envp,
            sizeof(exec_envp) /
                sizeof(exec_envp[0])
        );

    if (
        result !=
        syscall_result_error(
            SYSCALL_ERROR_NOT_FOUND
        )
    ) {
        return 45;
    }

    if (myos_getpid() != parent_pid) {
        return 46;
    }

    /*
     * This executable comes from the test initramfs through the normal
     * process namespace/VFS path. exec_path is relative and the current
     * directory is "/", so this also exercises CWD-relative executable
     * resolution.
     *
     * On success execve does not return here: IRETQ must enter
     * exec_target.elf using the replacement argc/argv/envp.
     */
    result =
        myos_execve(
            exec_path,
            sizeof(exec_path) - 1U,
            exec_argv,
            sizeof(exec_argv) /
                sizeof(exec_argv[0]),
            exec_envp,
            sizeof(exec_envp) /
                sizeof(exec_envp[0])
        );

    /*
     * Any return from a successful executable lookup means image replacement
     * failed. Preserve result only to make the call observable to the
     * compiler; every returning path is a regression failure.
     */
    (void) result;

    return 47;
}
