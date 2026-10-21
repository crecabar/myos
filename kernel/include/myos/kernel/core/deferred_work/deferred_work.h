// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#ifndef MYOS_KERNEL_CORE_DEFERRED_WORK_H
#define MYOS_KERNEL_CORE_DEFERRED_WORK_H

#include <stdbool.h>
#include <stddef.h>

enum deferred_work_result {
    DEFERRED_WORK_COMPLETED,
    DEFERRED_WORK_MORE,
    DEFERRED_WORK_WAIT,
};

typedef enum deferred_work_result
(*deferred_work_handler_fn)(void *context);

/**
 * Lightweight notification after deferred work is requested.
 *
 * May run in IRQ context. Must not block, dispatch
 * work, or perform a context switch.
 */
typedef void (*deferred_work_notification_fn)(
    void *context
);

struct deferred_work {
    deferred_work_handler_fn handler;
    void *context;

    bool pending;
    bool running;

    deferred_work_notification_fn notification;
    void *notification_context;
};

bool deferred_work_init(
    struct deferred_work *work,
    deferred_work_handler_fn handler,
    void *context
);

void deferred_work_request(
    struct deferred_work *work
);

size_t deferred_work_dispatch(
    struct deferred_work *work,
    size_t budget
);

bool deferred_work_pending(
    const struct deferred_work *work
);

/**
 * Installs an optional request notification.
 *
 * Configuration must be serialized with producers.
 * If work is already pending, the notification is
 * invoked immediately to avoid missing that request.
 *
 * Passing NULL disables notifications.
 *
 * Does not execute deferred work.
 */
bool deferred_work_notification_set(
    struct deferred_work *work,
    deferred_work_notification_fn notification,
    void *context
);

#endif
