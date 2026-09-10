// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file timer.h
 * @brief x86 PIT timer support.
 */

#ifndef MYOS_ARCH_X86_64_TIMER_H
#define MYOS_ARCH_X86_64_TIMER_H

#include "interrupts.h"

#include <stdint.h>

#define TIMER_INTERRUPT_VECTOR 0x20

/**
 * Initializes the periodic PIT timer.
 *
 * @param frequency Requested timer frequency in Hz.
 */
void timer_init(uint32_t frequency);

/**
 * Handles one PIT timer interrupt.
 *
 * The supplied interrupt frame contains the CPU state interrupted by the
 * timer. The current implementation only accounts for the tick and
 * acknowledges the Local APIC; later this context will be used for process
 * preemption.
 *
 * @param context CPU state captured when the timer interrupt occurred.
 */
void timer_handle_interrupt(struct interrupt_context *context);

/**
 * Returns the number of timer ticks received since initialization.
 *
 * @return Monotonic PIT tick count.
 */
uint64_t timer_ticks(void);

/**
 * Returns the number of PIT interrupts received while the CPU was executing
 * in user mode.
 *
 * @return Number of timer ticks that interrupted ring 3 execution.
 */
uint64_t timer_user_ticks(void);

#endif
