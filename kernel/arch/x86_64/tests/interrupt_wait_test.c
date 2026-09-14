// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file interrupt_wait_test.c
 * @brief x86-64 interruptible wait regression tests.
 */

#include "interrupt_wait_test.h"

#include "../interrupts.h"
#include "../timer.h"
#include "../../../core/panic.h"
#include "../../../diagnostics/diagnostics.h"

#include <stdint.h>

#define X86_RFLAGS_INTERRUPT_FLAG (1ULL << 9)

static uint64_t interrupt_wait_test_read_rflags(void);

void interrupt_wait_test_run(void)
{
    /*
     * arch_init() has already installed and enabled the periodic timer.
     *
     * Disable maskable interrupts before taking the baseline so the timer
     * cannot advance between the observation and the first wait.
     */
    interrupts_disable();

    uint64_t tick_before =
        timer_ticks();

    /*
     * A spurious interrupt could wake HLT before the PIT does, so keep waiting
     * until the timer itself proves that an interrupt was delivered.
     */
    while (timer_ticks() == tick_before) {
        interrupts_wait();
    }

    uint64_t rflags =
        interrupt_wait_test_read_rflags();

    if (
        (rflags & X86_RFLAGS_INTERRUPT_FLAG) != 0
    ) {
        kernel_panic(
            "Interrupt wait returned with interrupts enabled"
        );
    }

    /*
     * Restore the normal post-arch_init kernel state for subsequent tests and
     * scheduler startup.
     */
    interrupts_enable();

    diagnostics_write(
        "[arch] Interruptible wait test passed\n"
    );
}

static uint64_t interrupt_wait_test_read_rflags(void)
{
    uint64_t rflags;

    __asm__ volatile (
        "pushfq\n"
        "popq %0"
        : "=r"(rflags)
    );

    return rflags;
}
