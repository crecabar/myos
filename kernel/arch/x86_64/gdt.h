// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file gdt.h
 * @brief x86-64 global descriptor table and task state initialization.
 */

#ifndef MYOS_ARCH_X86_64_GDT_H
#define MYOS_ARCH_X86_64_GDT_H

#include <stdint.h>

/**
 * Kernel-mode 64-bit code segment selector.
 */
#define GDT_KERNEL_CODE_SELECTOR 0x08

/**
 * Kernel-mode data segment selector.
 */
#define GDT_KERNEL_DATA_SELECTOR 0x10

/**
 * User-mode data segment selector including RPL 3.
 */
#define GDT_USER_DATA_SELECTOR 0x1B

/**
 * User-mode 64-bit code segment selector including RPL 3.
 */
#define GDT_USER_CODE_SELECTOR 0x23

/**
 * Task State Segment selector.
 */
#define GDT_TSS_SELECTOR 0x28

/**
 * Initializes and activates the MyOS x86-64 GDT and TSS.
 *
 * Installs kernel and user code/data descriptors, configures a minimal
 * Task State Segment with a valid ring-0 stack, loads the GDT, reloads the
 * segment registers, and loads the task register.
 */
void gdt_init(void);

/**
 * Returns the ring-0 stack pointer configured in the active TSS.
 *
 * @return Virtual address used by the CPU when transitioning from user mode
 *         to kernel mode.
 */
uint64_t gdt_kernel_stack_top(void);

#endif
