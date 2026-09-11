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

#endif
