// SPDX-License-Identifier: GPL-2.0-only

#ifndef MYOS_ARCH_X86_64_PS2_MOUSE_H
#define MYOS_ARCH_X86_64_PS2_MOUSE_H

#include <stdbool.h>

#define PS2_MOUSE_ISA_IRQ          12U
#define PS2_MOUSE_INTERRUPT_VECTOR 0x2CU

struct interrupt_context;

/**
 * Initializes a standard PS/2 mouse on the second i8042 port.
 *
 * The mouse is configured for default three-byte packets and streaming
 * data reporting.
 */
bool ps2_mouse_init(void);

/**
 * Handles one PS/2 mouse interrupt.
 */
void ps2_mouse_handle_interrupt(
    struct interrupt_context *context
);

#endif
