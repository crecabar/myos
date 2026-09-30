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
        ) != -5
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

    diagnostics_write(
        "[syscall] ABI version and error contract tests passed\n"
    );
}
