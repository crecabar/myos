// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file pid.h
 * @brief Minimal process identifier allocation.
 */

#ifndef MYOS_PROCESS_PID_H
#define MYOS_PROCESS_PID_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Allocates the next process identifier.
 *
 * PID zero is reserved as invalid and is never returned.
 *
 * Identifiers are currently monotonic and are not reused.
 *
 * @param pid Receives the allocated process identifier.
 *
 * @return true when an identifier was allocated; false when the PID space has
 * been exhausted or pid is NULL.
 */
bool process_pid_allocate(uint64_t *pid);

/**
 * Rolls back the most recently allocated process identifier.
 *
 * This operation is valid only before the process using the identifier has
 * been published. The supplied PID must be the immediately preceding
 * allocation.
 *
 * @param pid Unpublished process identifier to return.
 *
 * @return true when the allocation was rolled back; false otherwise.
 */
bool process_pid_release(uint64_t pid);

#endif
