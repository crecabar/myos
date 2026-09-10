// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file pic.h
 * @brief Legacy x86 programmable interrupt controller support.
 */

#ifndef MYOS_ARCH_X86_64_PIC_H
#define MYOS_ARCH_X86_64_PIC_H

#include <stdint.h>

/**
 * First interrupt vector used by the remapped master PIC.
 */
#define PIC_MASTER_VECTOR_BASE 0x20

/**
 * First interrupt vector used by the remapped slave PIC.
 */
#define PIC_SLAVE_VECTOR_BASE 0x28

/**
 * Masks every interrupt input on both legacy PIC controllers.
 */
void pic_disable(void);

/**
 * Initializes and remaps the legacy 8259 PIC.
 *
 * The master PIC is mapped to vectors 0x20-0x27 and the slave PIC to
 * vectors 0x28-0x2F.
 */
void pic_init(void);

/**
 * Sends end-of-interrupt acknowledgement for an IRQ.
 *
 * @param irq Hardware IRQ number in the range 0-15.
 */
void pic_send_eoi(uint8_t irq);

/**
 * Returns the interrupt mask register of the master PIC.
 *
 * A set bit means the corresponding IRQ is masked.
 *
 * @return Current master PIC interrupt mask.
 */
uint8_t pic_master_mask(void);

/**
 * Returns the interrupt request register of the master PIC.
 *
 * A set bit represents a pending hardware interrupt request.
 *
 * @return Current master PIC IRR value.
 */
uint8_t pic_master_irr(void);

#endif
