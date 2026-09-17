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

#endif
