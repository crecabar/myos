// SPDX-License-Identifier: GPL-2.0-only

#include "timer.h"
#include "interrupts.h"
#include "lapic.h"
#include "../../scheduler/scheduler.h"

#include <stdbool.h>
#include <stddef.h>

#define PIT_CHANNEL_0_DATA 0x40
#define PIT_COMMAND        0x43

#define PIT_BASE_FREQUENCY 1193182U
#define PIT_MODE_SQUARE_WAVE 0x36

static volatile uint64_t tick_count;
static volatile uint64_t user_tick_count;

static bool timer_initialized;

#define X86_RFLAGS_INTERRUPT_FLAG (1ULL << 9)

static void timer_out8(uint16_t port, uint8_t value);

static void timer_out8(uint16_t port, uint8_t value)
{
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

void timer_init(uint32_t frequency)
{
    if (frequency == 0) {
        return;
    }

    uint32_t divisor = PIT_BASE_FREQUENCY / frequency;

    timer_out8(PIT_COMMAND, PIT_MODE_SQUARE_WAVE);
    timer_out8(PIT_CHANNEL_0_DATA, (uint8_t) (divisor & 0xFF));
    timer_out8(PIT_CHANNEL_0_DATA, (uint8_t) ((divisor >> 8) & 0xFF));

    tick_count = 0;
    user_tick_count = 0;
    timer_initialized = true;
}

void timer_handle_interrupt(struct interrupt_context *context)
{
    ++tick_count;

    bool user_mode =
        context != NULL &&
        (context->cs & 0x3) == 3;

    if (user_mode) {
        ++user_tick_count;
    }

    /*
     * Acknowledge the hardware interrupt before potentially switching
     * process context.
     */
    lapic_send_eoi();

    if (user_mode) {
        scheduler_tick(context);
    }
}

uint64_t timer_ticks(void)
{
    return tick_count;
}

uint64_t timer_user_ticks(void)
{
    return user_tick_count;
}

bool timer_sleep_ticks(uint64_t duration_ticks)
{
    if (!timer_initialized) {
        return false;
    }

    uint64_t rflags;

    __asm__ volatile (
        "pushfq\n\t"
        "popq %0"
        : "=r"(rflags)
        :
        : "memory"
    );

    /*
     * Waiting for an interrupt with maskable interrupts disabled
     * could leave the CPU halted indefinitely.
     *
     * Reject the request without modifying the caller's state.
     */
    if ((rflags & X86_RFLAGS_INTERRUPT_FLAG) == 0) {
        return false;
    }

    /*
     * An active user process cannot be suspended through this
     * primitive. Process sleep/wakeup belongs to the scheduler.
     */
    if (scheduler_current() != NULL) {
        return false;
    }

    /*
     * Restrict the requested interval to the supported range
     * of the wrapping tick counter.
     */
    if (duration_ticks > TIMER_SLEEP_MAX_TICKS) {
        return false;
    }

    if (duration_ticks == 0) {
        return true;
    }

    /*
     * On the current single-CPU kernel, disabling interrupts
     * makes the initial tick snapshot and subsequent wait atomic
     * with respect to timer interrupt delivery.
     */
    interrupts_disable();

    uint64_t start = timer_ticks();

    while (
        (uint64_t) (timer_ticks() - start) < duration_ticks
    ) {
        /*
         * interrupts_wait() executes STI; HLT; CLI.
         *
         * Unrelated interrupts may wake the CPU before the
         * requested interval has elapsed. Always recheck the
         * timer counter before completing the wait.
         */
        interrupts_wait();
    }

    /*
     * The caller entered with interrupts enabled.
     * Restore that state before returning.
     */
    interrupts_enable();

    return true;
}
