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
     * A spurious interrupt could wake HLT before the PIT does, so keep
     * waiting until the timer proves that an interrupt was delivered.
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
     * A timer-driven wait must reject entry with maskable interrupts
     * disabled rather than halt the CPU with no valid wakeup path.
     */
    if (timer_sleep_ticks(1)) {
        kernel_panic(
            "Timer wait accepted disabled interrupts"
        );
    }

    if (timer_sleep_ticks(0)) {
        kernel_panic(
            "Zero-tick timer wait accepted disabled interrupts"
        );
    }

    rflags = interrupt_wait_test_read_rflags();

    if (
        (rflags & X86_RFLAGS_INTERRUPT_FLAG) != 0
    ) {
        kernel_panic(
            "Rejected timer wait changed interrupt state"
        );
    }

    interrupts_enable();

    if (!timer_sleep_ticks(0)) {
        kernel_panic(
            "Zero-tick timer wait failed"
        );
    }

    /*
     * Reject out-of-range intervals without entering the
     * interruptible wait loop.
     */
    if (timer_sleep_ticks(TIMER_SLEEP_MAX_TICKS + 1U)) {
        kernel_panic(
            "Timer wait accepted an excessive interval"
        );
    }

    if (timer_sleep_ticks(UINT64_MAX)) {
        kernel_panic(
            "Timer wait accepted UINT64_MAX ticks"
        );
    }

    uint64_t rflags_after_rejection =
        interrupt_wait_test_read_rflags();

    if (
        (rflags_after_rejection &
        X86_RFLAGS_INTERRUPT_FLAG) == 0
    ) {
        kernel_panic(
            "Rejected timer wait disabled interrupts"
        );
    }

    /*
     * Each wait must observe its own starting tick.
     */
    uint64_t consecutive_start = timer_ticks();

    if (!timer_sleep_ticks(1)) {
        kernel_panic(
            "First consecutive timer wait failed"
        );
    }

    uint64_t consecutive_middle = timer_ticks();

    if (
        (uint64_t) (
            consecutive_middle - consecutive_start
        ) < 1
    ) {
        kernel_panic(
            "First consecutive timer wait returned early"
        );
    }

    if (!timer_sleep_ticks(1)) {
        kernel_panic(
            "Second consecutive timer wait failed"
        );
    }

    uint64_t consecutive_end = timer_ticks();

    if (
        (uint64_t) (
            consecutive_end - consecutive_middle
        ) < 1
    ) {
        kernel_panic(
            "Second consecutive timer wait returned early"
        );
    }

    uint64_t sleep_start = timer_ticks();

    if (!timer_sleep_ticks(3)) {
        kernel_panic(
            "Three-tick timer wait failed"
        );
    }

    uint64_t sleep_end = timer_ticks();

    if (
        (uint64_t) (sleep_end - sleep_start) < 3
    ) {
        kernel_panic(
            "Timer wait returned before requested ticks elapsed"
        );
    }

    rflags = interrupt_wait_test_read_rflags();

    if (
        (rflags & X86_RFLAGS_INTERRUPT_FLAG) == 0
    ) {
        kernel_panic(
            "Timer wait failed to restore enabled interrupts"
        );
    }

    diagnostics_printf(
        "[arch] Interruptible wait and timer sleep tests passed: "
        "elapsed=%u ticks\n",
        sleep_end - sleep_start
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
