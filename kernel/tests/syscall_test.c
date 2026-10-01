// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include "syscall_test.h"

#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../syscall/syscall.h"

#include <stdint.h>

_Static_assert(
    SYSCALL_DEBUG_PUTC == 1,
    "SYSCALL_DEBUG_PUTC ABI number changed"
);

_Static_assert(
    SYSCALL_EXIT == 2,
    "SYSCALL_EXIT ABI number changed"
);

_Static_assert(
    SYSCALL_YIELD == 3,
    "SYSCALL_YIELD ABI number changed"
);

_Static_assert(
    SYSCALL_WRITE == 4,
    "SYSCALL_WRITE ABI number changed"
);

_Static_assert(
    SYSCALL_WAITPID == 5,
    "SYSCALL_WAITPID ABI number changed"
);

_Static_assert(
    SYSCALL_FORK == 6,
    "SYSCALL_FORK ABI number changed"
);

_Static_assert(
    SYSCALL_FD_OPEN == 7,
    "SYSCALL_FD_OPEN ABI number changed"
);

_Static_assert(
    SYSCALL_FD_CLOSE == 8,
    "SYSCALL_FD_CLOSE ABI number changed"
);

_Static_assert(
    SYSCALL_FD_READ == 9,
    "SYSCALL_FD_READ ABI number changed"
);

_Static_assert(
    SYSCALL_FD_WRITE == 10,
    "SYSCALL_FD_WRITE ABI number changed"
);

_Static_assert(
    SYSCALL_FD_LSEEK == 11,
    "SYSCALL_FD_LSEEK ABI number changed"
);

_Static_assert(
    SYSCALL_FD_FSTAT == 12,
    "SYSCALL_FD_FSTAT ABI number changed"
);

_Static_assert(
    SYSCALL_OPEN_ACCESS_READ == 1 &&
    SYSCALL_OPEN_ACCESS_WRITE == 2,
    "Syscall open access ABI changed"
);

_Static_assert(
    SYSCALL_SEEK_ORIGIN_START == 0 &&
    SYSCALL_SEEK_ORIGIN_CURRENT == 1 &&
    SYSCALL_SEEK_ORIGIN_END == 2,
    "Syscall seek-origin ABI changed"
);

_Static_assert(
    SYSCALL_FILE_TYPE_REGULAR_FILE == 1 &&
    SYSCALL_FILE_TYPE_DIRECTORY == 2 &&
    SYSCALL_FILE_TYPE_CHARACTER_DEVICE == 3,
    "Syscall file-type ABI changed"
);

void syscall_test_run(void)
{
    if (SYSCALL_ABI_VERSION != 1U) {
        kernel_panic(
            "Unexpected syscall ABI version"
        );
    }

    if (
        syscall_result_error(
            SYSCALL_ERROR_INVALID_ARGUMENT
        ) != -1 ||
        syscall_result_error(
            SYSCALL_ERROR_BAD_ADDRESS
        ) != -2 ||
        syscall_result_error(
            SYSCALL_ERROR_NOT_IMPLEMENTED
        ) != -3 ||
        syscall_result_error(
            SYSCALL_ERROR_NO_CHILD
        ) != -4 ||
        syscall_result_error(
            SYSCALL_ERROR_RESOURCE_EXHAUSTED
        ) != -5 ||
        syscall_result_error(
            SYSCALL_ERROR_BAD_DESCRIPTOR
        ) != -6 ||
        syscall_result_error(
            SYSCALL_ERROR_NOT_FOUND
        ) != -7 ||
        syscall_result_error(
            SYSCALL_ERROR_NOT_DIRECTORY
        ) != -8 ||
        syscall_result_error(
            SYSCALL_ERROR_NOT_SUPPORTED
        ) != -9 ||
        syscall_result_error(
            SYSCALL_ERROR_ACCESS_DENIED
        ) != -10 ||
        syscall_result_error(
            SYSCALL_ERROR_OVERFLOW
        ) != -11
    ) {
        kernel_panic(
            "Syscall error encoding is unstable"
        );
    }

    if (
        !syscall_result_is_error(
            syscall_result_error(
                SYSCALL_ERROR_INVALID_ARGUMENT
            )
        ) ||
        !syscall_result_is_error(
            syscall_result_error(
                SYSCALL_ERROR_BAD_ADDRESS
            )
        ) ||
        syscall_result_is_error(0) ||
        syscall_result_is_error(1)
    ) {
        kernel_panic(
            "Syscall error classification failed"
        );
    }

    syscall_result_t result =
        syscall_dispatch(
            UINT64_MAX,
            0,
            0,
            0,
            0,
            0,
            0
        );

    if (
        result !=
        syscall_result_error(
            SYSCALL_ERROR_NOT_IMPLEMENTED
        )
    ) {
        kernel_panic(
            "Unknown syscall did not return NOT_IMPLEMENTED"
        );
    }

    /*
     * A zero-length write succeeds without dereferencing the supplied
     * userspace address.
     */
    result =
        syscall_dispatch(
            SYSCALL_WRITE,
            0,
            0,
            0,
            0,
            0,
            0
        );

    if (result != 0) {
        kernel_panic(
            "Zero-length write did not succeed"
        );
    }

    /*
     * The current write implementation has a deliberately bounded
     * kernel-side staging buffer. Oversized requests are arguments the
     * syscall cannot accept, independently of the user pointer.
     */
    result =
        syscall_dispatch(
            SYSCALL_WRITE,
            0,
            UINT64_MAX,
            0,
            0,
            0,
            0
        );

    if (
        result !=
        syscall_result_error(
            SYSCALL_ERROR_INVALID_ARGUMENT
        )
    ) {
        kernel_panic(
            "Oversized write did not return INVALID_ARGUMENT"
        );
    }

    /*
     * Descriptor syscalls require a valid current process instance.
     */
    result =
        syscall_dispatch(
            SYSCALL_FD_CLOSE,
            0,
            0,
            0,
            0,
            0,
            0
        );

    if (
        result !=
        syscall_result_error(
            SYSCALL_ERROR_INVALID_ARGUMENT
        )
    ) {
        kernel_panic(
            "FD_CLOSE accepted missing current process"
        );
    }

    /*
     * FD_OPEN validates ABI-level arguments before consulting userspace or
     * process namespace state.
     */
    result =
        syscall_dispatch(
            SYSCALL_FD_OPEN,
            0,
            0,
            SYSCALL_OPEN_ACCESS_READ,
            0,
            0,
            0
        );

    if (
        result !=
        syscall_result_error(
            SYSCALL_ERROR_INVALID_ARGUMENT
        )
    ) {
        kernel_panic(
            "FD_OPEN accepted empty pathname"
        );
    }

    result =
        syscall_dispatch(
            SYSCALL_FD_OPEN,
            0,
            1,
            0,
            0,
            0,
            0
        );

    if (
        result !=
        syscall_result_error(
            SYSCALL_ERROR_INVALID_ARGUMENT
        )
    ) {
        kernel_panic(
            "FD_OPEN accepted empty access mode"
        );
    }

    result =
        syscall_dispatch(
            SYSCALL_FD_OPEN,
            0,
            1,
            1ULL << 63,
            0,
            0,
            0
        );

    if (
        result !=
        syscall_result_error(
            SYSCALL_ERROR_INVALID_ARGUMENT
        )
    ) {
        kernel_panic(
            "FD_OPEN accepted unknown access flags"
        );
    }

    result =
        syscall_dispatch(
            SYSCALL_FD_WRITE,
            0,
            0,
            (uint64_t) INT64_MAX + 1ULL,
            0,
            0,
            0
        );

    if (
        result !=
        syscall_result_error(
            SYSCALL_ERROR_OVERFLOW
        )
    ) {
        kernel_panic(
            "FD_WRITE accepted unrepresentable length"
        );
    }

    result =
        syscall_dispatch(
            SYSCALL_FD_WRITE,
            0,
            0,
            1,
            0,
            0,
            0
        );

    if (
        result !=
        syscall_result_error(
            SYSCALL_ERROR_INVALID_ARGUMENT
        )
    ) {
        kernel_panic(
            "FD_WRITE accepted missing current process"
        );
    }

    result =
        syscall_dispatch(
            SYSCALL_FD_READ,
            0,
            0,
            (uint64_t) INT64_MAX + 1ULL,
            0,
            0,
            0
        );

    if (
        result !=
        syscall_result_error(
            SYSCALL_ERROR_OVERFLOW
        )
    ) {
        kernel_panic(
            "FD_READ accepted unrepresentable length"
        );
    }

    result =
        syscall_dispatch(
            SYSCALL_FD_READ,
            0,
            0,
            1,
            0,
            0,
            0
        );

    if (
        result !=
        syscall_result_error(
            SYSCALL_ERROR_INVALID_ARGUMENT
        )
    ) {
        kernel_panic(
            "FD_READ accepted missing current process"
        );
    }

    result =
        syscall_dispatch(
            SYSCALL_FD_FSTAT,
            0,
            0,
            0,
            0,
            0,
            0
        );

    if (
        result !=
        syscall_result_error(
            SYSCALL_ERROR_INVALID_ARGUMENT
        )
    ) {
        kernel_panic(
            "FD_FSTAT accepted missing current process"
        );
    }

    /*
     * FD_LSEEK rejects unknown ABI origins before consulting process state.
     */
    result =
        syscall_dispatch(
            SYSCALL_FD_LSEEK,
            0,
            0,
            UINT64_MAX,
            0,
            0,
            0
        );

    if (
        result !=
        syscall_result_error(
            SYSCALL_ERROR_INVALID_ARGUMENT
        )
    ) {
        kernel_panic(
            "FD_LSEEK accepted unknown seek origin"
        );
    }

    /*
    * A valid seek request still requires a current process instance.
    */
    result =
        syscall_dispatch(
            SYSCALL_FD_LSEEK,
            0,
            0,
            SYSCALL_SEEK_ORIGIN_START,
            0,
            0,
            0
        );

    if (
        result !=
        syscall_result_error(
            SYSCALL_ERROR_INVALID_ARGUMENT
        )
    ) {
        kernel_panic(
            "FD_LSEEK accepted missing current process"
        );
    }

    diagnostics_write(
        "[syscall] ABI version and error contract tests passed\n"
    );
}
