// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file scheduler_context.S
 * @brief Ring-3 timer-preemption and context-switch regression workload.
 *
 * Two instances of this ELF execute concurrently in independent address
 * spaces. argv[0] selects workload A or B.
 *
 * Both instances deliberately use the same virtual stack addresses while
 * storing different process-local markers there. Repeated timer preemption
 * must preserve GPRs, RSP, RFLAGS, control flow and private memory.
 */

.section .text
.global _start
.type _start, @function

.set SYSCALL_EXIT, 2

.set LOCAL_SIZE,             64
.set LOCAL_EXPECTED_RSP,      0
.set LOCAL_MARKER,            8
.set LOCAL_PROGRESS,         16
.set LOCAL_RFLAGS,           24

.set MARKER_A,       0x1a1a1a1a
.set MARKER_B,       0x2b2b2b2b

.set PROGRESS_PHASE1,         1
.set PROGRESS_PHASE2,         2

/*
 * Workload A intentionally finishes before workload B.
 *
 * This gives the kernel harness a survivor interval in which B must continue
 * executing after A has terminated and been detached.
 */
.set A_PHASE1_ITERATIONS, 0x06000000
.set A_PHASE2_ITERATIONS, 0x02000000

.set B_PHASE1_ITERATIONS, 0x0c000000
.set B_PHASE2_ITERATIONS, 0x04000000

/*
 * Phase-one sentinels.
 *
 * RCX is reserved for LOOP and is therefore checked separately in phase two.
 */
.set A_RAX, 0x1101000000000001
.set A_RBX, 0x1102000000000002
.set A_RDX, 0x1103000000000003
.set A_RSI, 0x1104000000000004
.set A_RDI, 0x1105000000000005
.set A_RBP, 0x1106000000000006
.set A_RCX, 0x1107000000000007
.set A_R8,  0x1108000000000008
.set A_R9,  0x1109000000000009
.set A_R10, 0x110a00000000000a
.set A_R11, 0x110b00000000000b
.set A_R12, 0x110c00000000000c
.set A_R13, 0x110d00000000000d
.set A_R14, 0x110e00000000000e
.set A_R15, 0x110f00000000000f

.set B_RAX, 0x2201000000000001
.set B_RBX, 0x2202000000000002
.set B_RDX, 0x2203000000000003
.set B_RSI, 0x2204000000000004
.set B_RDI, 0x2205000000000005
.set B_RBP, 0x2206000000000006
.set B_RCX, 0x2207000000000007
.set B_R8,  0x2208000000000008
.set B_R9,  0x2209000000000009
.set B_R10, 0x220a00000000000a
.set B_R11, 0x220b00000000000b
.set B_R12, 0x220c00000000000c
.set B_R13, 0x220d00000000000d
.set B_R14, 0x220e00000000000e
.set B_R15, 0x220f00000000000f

_start:
    /*
     * Minimal MyOS entry ABI:
     *
     *   [RSP + 0] = argc
     *   [RSP + 8] = argv[0]
     */
    cmpq $1, (%rsp)
    jne .Lfail

    movq 8(%rsp), %rax

    cmpb $'A', (%rax)
    je .Lrun_a

    cmpb $'B', (%rax)
    je .Lrun_b

    jmp .Lfail


/*
 * ---------------------------------------------------------------------------
 * Workload A
 * ---------------------------------------------------------------------------
 */

.Lrun_a:
    subq $LOCAL_SIZE, %rsp

    movq %rsp, LOCAL_EXPECTED_RSP(%rsp)

    movl $MARKER_A, LOCAL_MARKER(%rsp)
    movl $PROGRESS_PHASE1, LOCAL_PROGRESS(%rsp)

    /*
     * Keep every GPR except RCX stable while timer interrupts repeatedly
     * preempt this process.
     */
    movabsq $A_RAX, %rax
    movabsq $A_RBX, %rbx
    movabsq $A_RDX, %rdx
    movabsq $A_RSI, %rsi
    movabsq $A_RDI, %rdi
    movabsq $A_RBP, %rbp

    movabsq $A_R8,  %r8
    movabsq $A_R9,  %r9
    movabsq $A_R10, %r10
    movabsq $A_R11, %r11
    movabsq $A_R12, %r12
    movabsq $A_R13, %r13
    movabsq $A_R14, %r14
    movabsq $A_R15, %r15

    /*
     * CF and DF are deliberately set. IF is already enabled in the normal
     * MyOS userspace context.
     *
     * MOV, NOP and LOOP do not modify these flags.
     */
    stc
    std

    movq $A_PHASE1_ITERATIONS, %rcx

.La_phase1_loop:
    nop
    loop .La_phase1_loop

    /*
     * Capture the restored userspace flags before comparisons modify them.
     * RCX is free now because LOOP completed with RCX == 0.
     */
    pushfq
    popq %rcx
    movq %rcx, LOCAL_RFLAGS(%rsp)

    /*
     * Verify every stable GPR from phase one.
     */
    movabsq $A_RAX, %rcx
    cmpq %rcx, %rax
    jne .Lfail

    movabsq $A_RBX, %rcx
    cmpq %rcx, %rbx
    jne .Lfail

    movabsq $A_RDX, %rcx
    cmpq %rcx, %rdx
    jne .Lfail

    movabsq $A_RSI, %rcx
    cmpq %rcx, %rsi
    jne .Lfail

    movabsq $A_RDI, %rcx
    cmpq %rcx, %rdi
    jne .Lfail

    movabsq $A_RBP, %rcx
    cmpq %rcx, %rbp
    jne .Lfail

    movabsq $A_R8, %rcx
    cmpq %rcx, %r8
    jne .Lfail

    movabsq $A_R9, %rcx
    cmpq %rcx, %r9
    jne .Lfail

    movabsq $A_R10, %rcx
    cmpq %rcx, %r10
    jne .Lfail

    movabsq $A_R11, %rcx
    cmpq %rcx, %r11
    jne .Lfail

    movabsq $A_R12, %rcx
    cmpq %rcx, %r12
    jne .Lfail

    movabsq $A_R13, %rcx
    cmpq %rcx, %r13
    jne .Lfail

    movabsq $A_R14, %rcx
    cmpq %rcx, %r14
    jne .Lfail

    movabsq $A_R15, %rcx
    cmpq %rcx, %r15
    jne .Lfail

    /*
     * RSP and process-private memory must also survive.
     */
    cmpq LOCAL_EXPECTED_RSP(%rsp), %rsp
    jne .Lfail

    cmpl $MARKER_A, LOCAL_MARKER(%rsp)
    jne .Lfail

    cmpl $PROGRESS_PHASE1, LOCAL_PROGRESS(%rsp)
    jne .Lfail

    /*
     * Verify the flag state that existed during the preemptible loop.
     *
     * CF = 1
     * IF = 1
     * DF = 1
     */
    movq LOCAL_RFLAGS(%rsp), %rcx

    testq $0x001, %rcx
    jz .Lfail

    testq $0x200, %rcx
    jz .Lfail

    testq $0x400, %rcx
    jz .Lfail

    movl $PROGRESS_PHASE2, LOCAL_PROGRESS(%rsp)

    /*
     * Phase-one verification changed arithmetic flags. Re-establish CF before
     * the second preemptible phase. DF remains set and IF must remain enabled.
     *
     * Phase two specifically checks RCX preservation. RAX is the loop counter
     * and was already verified as a stable sentinel during phase one.
     */
    stc

    movabsq $A_RCX, %rcx
    movq $A_PHASE2_ITERATIONS, %rax

.La_phase2_loop:
    decq %rax
    jnz .La_phase2_loop

    /*
     * Capture the flags restored across the second preemption interval before
     * the verification comparisons modify them.
     */
    pushfq
    popq %rax
    movq %rax, LOCAL_RFLAGS(%rsp)

    movabsq $A_RCX, %rax
    cmpq %rax, %rcx
    jne .Lfail

    movabsq $A_RBX, %rax
    cmpq %rax, %rbx
    jne .Lfail

    movabsq $A_RDX, %rax
    cmpq %rax, %rdx
    jne .Lfail

    movabsq $A_RSI, %rax
    cmpq %rax, %rsi
    jne .Lfail

    movabsq $A_RDI, %rax
    cmpq %rax, %rdi
    jne .Lfail

    movabsq $A_RBP, %rax
    cmpq %rax, %rbp
    jne .Lfail

    movabsq $A_R8, %rax
    cmpq %rax, %r8
    jne .Lfail

    movabsq $A_R9, %rax
    cmpq %rax, %r9
    jne .Lfail

    movabsq $A_R10, %rax
    cmpq %rax, %r10
    jne .Lfail

    movabsq $A_R11, %rax
    cmpq %rax, %r11
    jne .Lfail

    movabsq $A_R12, %rax
    cmpq %rax, %r12
    jne .Lfail

    movabsq $A_R13, %rax
    cmpq %rax, %r13
    jne .Lfail

    movabsq $A_R14, %rax
    cmpq %rax, %r14
    jne .Lfail

    movabsq $A_R15, %rax
    cmpq %rax, %r15
    jne .Lfail

    cmpq LOCAL_EXPECTED_RSP(%rsp), %rsp
    jne .Lfail

    cmpl $MARKER_A, LOCAL_MARKER(%rsp)
    jne .Lfail

    cmpl $PROGRESS_PHASE2, LOCAL_PROGRESS(%rsp)
    jne .Lfail

    movq LOCAL_RFLAGS(%rsp), %rax

    testq $0x001, %rax
    jz .Lfail

    testq $0x200, %rax
    jz .Lfail

    testq $0x400, %rax
    jz .Lfail

    jmp .Lsuccess


/*
 * ---------------------------------------------------------------------------
 * Workload B
 * ---------------------------------------------------------------------------
 */

.Lrun_b:
    subq $LOCAL_SIZE, %rsp

    movq %rsp, LOCAL_EXPECTED_RSP(%rsp)

    movl $MARKER_B, LOCAL_MARKER(%rsp)
    movl $PROGRESS_PHASE1, LOCAL_PROGRESS(%rsp)

    movabsq $B_RAX, %rax
    movabsq $B_RBX, %rbx
    movabsq $B_RDX, %rdx
    movabsq $B_RSI, %rsi
    movabsq $B_RDI, %rdi
    movabsq $B_RBP, %rbp

    movabsq $B_R8,  %r8
    movabsq $B_R9,  %r9
    movabsq $B_R10, %r10
    movabsq $B_R11, %r11
    movabsq $B_R12, %r12
    movabsq $B_R13, %r13
    movabsq $B_R14, %r14
    movabsq $B_R15, %r15

    stc
    std

    movq $B_PHASE1_ITERATIONS, %rcx

.Lb_phase1_loop:
    nop
    loop .Lb_phase1_loop

    pushfq
    popq %rcx
    movq %rcx, LOCAL_RFLAGS(%rsp)

    movabsq $B_RAX, %rcx
    cmpq %rcx, %rax
    jne .Lfail

    movabsq $B_RBX, %rcx
    cmpq %rcx, %rbx
    jne .Lfail

    movabsq $B_RDX, %rcx
    cmpq %rcx, %rdx
    jne .Lfail

    movabsq $B_RSI, %rcx
    cmpq %rcx, %rsi
    jne .Lfail

    movabsq $B_RDI, %rcx
    cmpq %rcx, %rdi
    jne .Lfail

    movabsq $B_RBP, %rcx
    cmpq %rcx, %rbp
    jne .Lfail

    movabsq $B_R8, %rcx
    cmpq %rcx, %r8
    jne .Lfail

    movabsq $B_R9, %rcx
    cmpq %rcx, %r9
    jne .Lfail

    movabsq $B_R10, %rcx
    cmpq %rcx, %r10
    jne .Lfail

    movabsq $B_R11, %rcx
    cmpq %rcx, %r11
    jne .Lfail

    movabsq $B_R12, %rcx
    cmpq %rcx, %r12
    jne .Lfail

    movabsq $B_R13, %rcx
    cmpq %rcx, %r13
    jne .Lfail

    movabsq $B_R14, %rcx
    cmpq %rcx, %r14
    jne .Lfail

    movabsq $B_R15, %rcx
    cmpq %rcx, %r15
    jne .Lfail

    cmpq LOCAL_EXPECTED_RSP(%rsp), %rsp
    jne .Lfail

    cmpl $MARKER_B, LOCAL_MARKER(%rsp)
    jne .Lfail

    cmpl $PROGRESS_PHASE1, LOCAL_PROGRESS(%rsp)
    jne .Lfail

    movq LOCAL_RFLAGS(%rsp), %rcx

    testq $0x001, %rcx
    jz .Lfail

    testq $0x200, %rcx
    jz .Lfail

    testq $0x400, %rcx
    jz .Lfail

    movl $PROGRESS_PHASE2, LOCAL_PROGRESS(%rsp)

    /*
     * Restore the flag pattern expected throughout this second preemptible
     * phase. DF remains set from phase one; CF must be explicitly restored.
     */
    stc

    movabsq $B_RCX, %rcx
    movq $B_PHASE2_ITERATIONS, %rax

.Lb_phase2_loop:
    decq %rax
    jnz .Lb_phase2_loop

    pushfq
    popq %rax
    movq %rax, LOCAL_RFLAGS(%rsp)

    movabsq $B_RCX, %rax
    cmpq %rax, %rcx
    jne .Lfail

    movabsq $B_RBX, %rax
    cmpq %rax, %rbx
    jne .Lfail

    movabsq $B_RDX, %rax
    cmpq %rax, %rdx
    jne .Lfail

    movabsq $B_RSI, %rax
    cmpq %rax, %rsi
    jne .Lfail

    movabsq $B_RDI, %rax
    cmpq %rax, %rdi
    jne .Lfail

    movabsq $B_RBP, %rax
    cmpq %rax, %rbp
    jne .Lfail

    movabsq $B_R8, %rax
    cmpq %rax, %r8
    jne .Lfail

    movabsq $B_R9, %rax
    cmpq %rax, %r9
    jne .Lfail

    movabsq $B_R10, %rax
    cmpq %rax, %r10
    jne .Lfail

    movabsq $B_R11, %rax
    cmpq %rax, %r11
    jne .Lfail

    movabsq $B_R12, %rax
    cmpq %rax, %r12
    jne .Lfail

    movabsq $B_R13, %rax
    cmpq %rax, %r13
    jne .Lfail

    movabsq $B_R14, %rax
    cmpq %rax, %r14
    jne .Lfail

    movabsq $B_R15, %rax
    cmpq %rax, %r15
    jne .Lfail

    cmpq LOCAL_EXPECTED_RSP(%rsp), %rsp
    jne .Lfail

    cmpl $MARKER_B, LOCAL_MARKER(%rsp)
    jne .Lfail

    cmpl $PROGRESS_PHASE2, LOCAL_PROGRESS(%rsp)
    jne .Lfail

    movq LOCAL_RFLAGS(%rsp), %rax

    testq $0x001, %rax
    jz .Lfail

    testq $0x200, %rax
    jz .Lfail

    testq $0x400, %rax
    jz .Lfail


/*
 * ---------------------------------------------------------------------------
 * Result
 * ---------------------------------------------------------------------------
 */

.Lsuccess:
    /*
     * The fixture deliberately kept DF set throughout the preemption test.
     * Restore the normal userspace convention before making the final syscall.
     */
    cld

    movq $SYSCALL_EXIT, %rax
    xorq %rdi, %rdi
    int $0x80

.Lfail:
    cld

    movq $SYSCALL_EXIT, %rax
    movq $1, %rdi
    int $0x80

.Lhang:
    jmp .Lhang

.size _start, . - _start
