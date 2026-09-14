// SPDX-License-Identifier: GPL-2.0-only

/**
 *
 */

#ifndef MYOS_ARCH_X86_64_INTERRUPTS_H
#define MYOS_ARCH_X86_64_INTERRUPTS_H

#include <stddef.h>
#include <stdint.h>

/**
 *
 */
struct interrupt_context {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;

    uint64_t rbp;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;

    uint64_t vector;
    uint64_t error_code;

    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

_Static_assert(
    sizeof(struct interrupt_context) == 176,
    "struct interrupt_context must be 176 bytes"
);

_Static_assert(
    _Alignof(struct interrupt_context) == 8,
    "struct interrupt_context must be 8-byte aligned"
);

_Static_assert(offsetof(struct interrupt_context, r15) == 0, "interrupt_context.r15 offset mismatch");
_Static_assert(offsetof(struct interrupt_context, r14) == 8, "interrupt_context.r14 offset mismatch");
_Static_assert(offsetof(struct interrupt_context, r13) == 16, "interrupt_context.r13 offset mismatch");
_Static_assert(offsetof(struct interrupt_context, r12) == 24, "interrupt_context.r12 offset mismatch");
_Static_assert(offsetof(struct interrupt_context, r11) == 32, "interrupt_context.r11 offset mismatch");
_Static_assert(offsetof(struct interrupt_context, r10) == 40, "interrupt_context.r10 offset mismatch");
_Static_assert(offsetof(struct interrupt_context, r9) == 48, "interrupt_context.r9 offset mismatch");
_Static_assert(offsetof(struct interrupt_context, r8) == 56, "interrupt_context.r8 offset mismatch");

_Static_assert(offsetof(struct interrupt_context, rbp) == 64, "interrupt_context.rbp offset mismatch");
_Static_assert(offsetof(struct interrupt_context, rdi) == 72, "interrupt_context.rdi offset mismatch");
_Static_assert(offsetof(struct interrupt_context, rsi) == 80, "interrupt_context.rsi offset mismatch");
_Static_assert(offsetof(struct interrupt_context, rdx) == 88, "interrupt_context.rdx offset mismatch");
_Static_assert(offsetof(struct interrupt_context, rcx) == 96, "interrupt_context.rcx offset mismatch");
_Static_assert(offsetof(struct interrupt_context, rbx) == 104, "interrupt_context.rbx offset mismatch");
_Static_assert(offsetof(struct interrupt_context, rax) == 112, "interrupt_context.rax offset mismatch");

_Static_assert(offsetof(struct interrupt_context, vector) == 120, "interrupt_context.vector offset mismatch");
_Static_assert(offsetof(struct interrupt_context, error_code) == 128, "interrupt_context.error_code offset mismatch");

_Static_assert(offsetof(struct interrupt_context, rip) == 136, "interrupt_context.rip offset mismatch");
_Static_assert(offsetof(struct interrupt_context, cs) == 144, "interrupt_context.cs offset mismatch");
_Static_assert(offsetof(struct interrupt_context, rflags) == 152, "interrupt_context.rflags offset mismatch");
_Static_assert(offsetof(struct interrupt_context, rsp) == 160, "interrupt_context.rsp offset mismatch");
_Static_assert(offsetof(struct interrupt_context, ss) == 168, "interrupt_context.ss offset mismatch");

/**
 * Disables maskable CPU interrupts.
 */
void interrupts_disable(void);

/**
 * Enables maskable CPU interrupts.
 */
void interrupts_enable(void);

/**
 * Waits for an interrupt and returns with maskable interrupts disabled.
 *
 * The caller must enter with maskable interrupts disabled. Interrupts are
 * enabled immediately before HLT so a pending interrupt cannot be lost between
 * enabling interrupts and entering the halted state.
 *
 * Once execution resumes after HLT, maskable interrupts are disabled again
 * before returning to the caller.
 */
void interrupts_wait(void);

/**
 *
 * @param context
 */
void syscall_handler(struct interrupt_context *context);

#endif
