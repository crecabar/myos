// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file programs.h
 * @brief Built-in user-mode programs used during early MyOS development.
 */

#ifndef MYOS_PROCESS_PROGRAMS_H
#define MYOS_PROCESS_PROGRAMS_H

#include <stddef.h>
#include <stdint.h>

/**
 * Describes a built-in user-mode program image.
 *
 * The program bytes contain raw x86-64 machine code that can be copied into
 * the executable region of a process address space.
 */
struct user_program {
    const uint8_t *data;
    size_t size;
};

/**
 * Returns the built-in "Hello world from ring3!" program.
 *
 * @return Pointer to the immutable program description.
 */
const struct user_program *user_program_hello(void);

/**
 * Returns the built-in counter program.
 *
 * The program repeatedly prints a numeric counter from user mode.
 *
 * @return Pointer to the immutable program description.
 */
const struct user_program *user_program_counter(void);

/**
 * Returns the built-in page-fault regression program.
 *
 * The program deliberately reads from unmapped virtual address zero to verify
 * that a user-mode page fault terminates only the offending process.
 *
 * @return Pointer to the immutable program description.
 */
const struct user_program *user_program_malicious_page_fault(void);

/**
 * Returns the built-in invalid-opcode regression program.
 *
 * The program deliberately executes UD2 from user mode to verify that an
 * invalid instruction terminates only the offending process.
 *
 * @return Pointer to the immutable program description.
 */
const struct user_program *user_program_malicious_ud2(void);

/**
 * Returns the built-in privileged-instruction regression program.
 *
 * The program deliberately executes HLT from user mode to verify that a
 * general-protection fault terminates only the offending process.
 *
 * @return Pointer to the immutable program description.
 */
const struct user_program *user_program_malicious_hlt(void);

/**
 * Returns a user-mode program that deliberately executes an x87 instruction.
 *
 * With the current FP/SIMD trap-on-use policy, the FLDZ instruction must
 * raise #NM before modifying x87 state.
 *
 * @return Pointer to the immutable program description.
 */
const struct user_program *user_program_malicious_x87(void);

/**
 * Returns the built-in exception-containment survivor program.
 *
 * The program prints a recognizable message and exits normally so tests can
 * verify that healthy user-mode execution continues after hostile processes
 * have been terminated.
 *
 * @return Pointer to the immutable program description.
 */
const struct user_program *user_program_survivor(void);

#endif
