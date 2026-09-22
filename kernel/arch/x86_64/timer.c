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
     * interrupts_wait() requires an interrupt-disabled caller and
     * relies on an interrupt to resume execution. Reject callers
     * that cannot satisfy the wait contract.
     */
    if ((rflags & X86_RFLAGS_INTERRUPT_FLAG) == 0) {
        return false;
    }

    if (duration_ticks == 0) {
        return true;
    }

    /*
     * On the current single-CPU kernel, disabling interrupts makes
     * the starting tick snapshot and the subsequent wait atomic
     * with respect to timer interrupt delivery.
     */
    interrupts_disable();

    uint64_t start = timer_ticks();

    while (
        (uint64_t) (timer_ticks() - start) <
        (uint64_t) duration_ticks
    ) {
        /*
         * STI; HLT; CLI. A timer interrupt may advance tick_count,
         * while an unrelated interrupt may wake us early. In either
         * case, recheck the elapsed ticks with interrupts disabled.
         */
        interrupts_wait();
    }

    interrupts_enable();

    return true;
}
