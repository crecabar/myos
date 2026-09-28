// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file lifecycle.h
 * @brief Process lifecycle notifications after scheduler detachment.
 */

#ifndef MYOS_PROCESS_LIFECYCLE_H
#define MYOS_PROCESS_LIFECYCLE_H

#include "process.h"

/**
 * Notifies the process lifecycle layer that a terminated process has been
 * safely detached from the scheduler.
 *
 * Legacy processes without a process_instance require no lifecycle action.
 *
 * @param process Detached terminated process.
 */
void process_lifecycle_notify_terminated(
    struct process *process
);

#endif
