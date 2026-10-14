// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#ifndef MYOS_CONSOLE_CONSOLE_INPUT_WORKER_H
#define MYOS_CONSOLE_CONSOLE_INPUT_WORKER_H

#include "console_device.h"

#include "../core/execution/kernel_context.h"

#include <stdbool.h>

#define CONSOLE_INPUT_WORKER_STACK_SIZE 8192U

/*
 * Persistent execution context for deferred console input.
 *
 * Owns its descriptor and execution stack.
 * Borrows the console device and supervisor context.
 *
 * The caller owns scheduler registration.
 */
struct console_input_worker {
    struct console_device *device;
    struct kernel_context context;

    _Alignas(KERNEL_CONTEXT_STACK_ALIGNMENT)
    unsigned char stack[CONSOLE_INPUT_WORKER_STACK_SIZE];
};

/**
 * Prepares a console input worker without registering it
 * with the scheduler or installing notifications.
 */
bool console_input_worker_initialize(
    struct console_input_worker *worker,
    struct console_device *device,
    struct kernel_switch_context *supervisor
);

/**
 * Connects deferred-work requests to worker wakeups.
 *
 * Requires serialized producer configuration on the BSP.
 * Outstanding requests are notified during registration.
 */
bool console_input_worker_notifications_enable(
    struct console_input_worker *worker
);

/**
 * Detaches notifications owned by this worker.
 *
 * The caller must prevent concurrent producers and ensure
 * the worker is not executing before retiring its storage.
 */
bool console_input_worker_notifications_disable(
    struct console_input_worker *worker
);

#endif