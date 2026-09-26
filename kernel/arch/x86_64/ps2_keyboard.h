// SPDX-License-Identifier: GPL-2.0-only

#ifndef MYOS_ARCH_X86_64_PS2_KEYBOARD_H
#define MYOS_ARCH_X86_64_PS2_KEYBOARD_H

#include <stdbool.h>

#define PS2_KEYBOARD_ISA_IRQ          1U
#define PS2_KEYBOARD_INTERRUPT_VECTOR 0x21U

struct interrupt_context;

/**
 * Initializes the first i8042 PS/2 port for keyboard input.
 *
 * The routine uses bounded waits and returns false when no usable
 * controller/keyboard is present.
 */
bool ps2_keyboard_init(void);

/**
 * Handles one PS/2 keyboard interrupt.
 */
void ps2_keyboard_handle_interrupt(
    struct interrupt_context *context
);

#endif
