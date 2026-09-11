// SPDX-License-Identifier: GPL-2.0-only
// file: kernel/arch/x86_64/interrupts.S

.section .text

// extern declarations for exception and interrupt handlers
.extern exception_handler
.extern syscall_handler
.extern timer_handle_interrupt

/*
 * Defines an exception stub for vectors whose architectural exception frame
 * does not contain a hardware-pushed error code.
 *
 * A synthetic zero is inserted so every exception reaches isr_common with:
 *
 *   vector
 *   error_code
 *   RIP
 *   CS
 *   RFLAGS
 *   RSP
 *   SS
 */
.macro ISR_NO_ERROR name, vector
.global \name
.type \name, @function

\name:
    pushq $0
    pushq $\vector
    jmp isr_common

.size \name, . - \name
.endm

/*
 * Defines an exception stub for vectors whose architectural exception frame
 * already contains a hardware-pushed error code.
 *
 * Only the vector is added before entering isr_common.
 */
.macro ISR_WITH_ERROR name, vector
.global \name
.type \name, @function

\name:
    pushq $\vector
    jmp isr_common

.size \name, . - \name
.endm

/*
 * CPU exception stubs.
 *
 * Keep these ordered by architectural vector number.
 */

/* #DE — Divide Error, vector 0, no hardware error code. */
ISR_NO_ERROR isr_divide_error, 0

/* #DB — Debug, vector 1, no hardware error code. */
ISR_NO_ERROR isr_debug, 1

/*
 * NMI — Non-Maskable Interrupt, vector 2, no hardware error code.
 *
 * NMI is not a CPU exception, but its architectural entry frame is
 * compatible with the normalized exception frame used by isr_common.
 */
ISR_NO_ERROR isr_nmi, 2

/* #BP — Breakpoint, vector 3, no hardware error code. */
ISR_NO_ERROR isr_breakpoint, 3

/* #OF — Overflow, vector 4, no hardware error code. */
ISR_NO_ERROR isr_overflow, 4

/* #BR — BOUND Range Exceeded, vector 5, no hardware error code. */
ISR_NO_ERROR isr_bound_range_exceeded, 5

/* #UD — Invalid Opcode, vector 6, no hardware error code. */
ISR_NO_ERROR isr_invalid_opcode, 6

/* #NM — Device Not Available, vector 7, no hardware error code. */
ISR_NO_ERROR isr_device_not_available, 7

/* #DF — Double Fault, vector 8, hardware error code. */
ISR_WITH_ERROR isr_double_fault, 8

/* #TS — Invalid TSS, vector 10, hardware error code. */
ISR_WITH_ERROR isr_invalid_tss, 10

/* #NP — Segment Not Present, vector 11, hardware error code. */
ISR_WITH_ERROR isr_segment_not_present, 11

/* #SS — Stack-Segment Fault, vector 12, hardware error code. */
ISR_WITH_ERROR isr_stack_segment_fault, 12

/* #GP — General Protection Fault, vector 13, hardware error code. */
ISR_WITH_ERROR isr_general_protection, 13

/* #PF — Page Fault, vector 14, hardware error code. */
ISR_WITH_ERROR isr_page_fault, 14

/* #MF — x87 Floating-Point Exception, vector 16, no hardware error code. */
ISR_NO_ERROR isr_x87_floating_point, 16

/* #AC — Alignment Check, vector 17, hardware error code. */
ISR_WITH_ERROR isr_alignment_check, 17

/* #MC — Machine Check, vector 18, no hardware error code. */
ISR_NO_ERROR isr_machine_check, 18

/* #XM — SIMD Floating-Point Exception, vector 19, no hardware error code. */
ISR_NO_ERROR isr_simd_floating_point, 19

/* #VE — Virtualization Exception, vector 20, no hardware error code. */
ISR_NO_ERROR isr_virtualization_exception, 20

/* #CP — Control Protection Exception, vector 21, hardware error code. */
ISR_WITH_ERROR isr_control_protection, 21

// isr_common: common normalized CPU exception entry and return path
.type isr_common, @function

isr_common:
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp

    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /*
     * RSP now points to struct interrupt_context.
     */
    movq %rsp, %rdi

    /*
     * Preserve the exact interrupt-frame address while giving C a correctly
     * aligned stack.
     *
     * The interrupted value of R12 is already saved in the frame, so R12 can
     * safely hold this temporary pointer.
     */
    movq %rsp, %r12
    andq $-16, %rsp

    cld

    call exception_handler

    /*
     * exception_handler may have rewritten the interrupt frame with another
     * process context. Restore the exact frame address before popping it.
     */
    movq %r12, %rsp

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8

    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax

    /*
     * Discard vector and error code.
     */
    addq $16, %rsp

    iretq

.size isr_common, . - isr_common

// isr_syscall global declaration
.global isr_syscall
.type isr_syscall, @function

// isr_syscall: System Call Interrupt (#SYSCALL)
isr_syscall:
    pushq $0
    pushq $0x80

    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rsp, %rdi

    subq $8, %rsp
    call syscall_handler
    addq $8, %rsp

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax

    addq $16, %rsp

    iretq

.size isr_syscall, .-isr_syscall

// isr_timer global declaration
.global isr_timer
.type isr_timer, @function

// isr_timer: Timer Interrupt (#IRQ0)
isr_timer:
    /*
     * Hardware IRQs do not push an error code.
     *
     * Normalize the interrupt frame:
     *   vector = 0x20
     *   error_code = 0
     */
    pushq $0
    pushq $0x20

    /*
     * Save every general-purpose register using exactly the same layout as
     * struct interrupt_context.
     */
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp

    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /*
     * RSP now points to struct interrupt_context.
     */
    movq %rsp, %rdi

    /*
     * Preserve the exact interrupt-frame address while giving C a correctly
     * aligned stack.
     *
     * The interrupted value of R12 is already saved in the frame, so R12 can
     * safely hold this temporary pointer.
     */
    movq %rsp, %r12
    andq $-16, %rsp

    cld

    call timer_handle_interrupt

    /*
     * Return to the potentially modified interrupt frame.
     */
    movq %r12, %rsp

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8

    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax

    /*
     * Discard vector and synthetic error code.
     */
    addq $16, %rsp

    iretq

.size isr_timer, .-isr_timer

// isr_spurious global declaration
.global isr_spurious
.type isr_spurious, @function

// isr_spurious: Spurious Interrupt (#IRQ7)
isr_spurious:
    iretq

.size isr_spurious, .-isr_spurious
