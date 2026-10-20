// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include "console_input_worker.h"

#include <myos/kernel/core/panic.h>
#include "../input/input.h"

#include <stddef.h>

static void console_input_worker_entry(
    void *argument
);

static void console_input_worker_notification(
    void *argument
);

bool console_input_worker_initialize(
    struct console_input_worker *worker,
    struct console_device *device,
    struct kernel_switch_context *supervisor)
{
    if (
        worker == NULL ||
        device == NULL ||
        supervisor == NULL ||
        device->device.state != DEVICE_STATE_ACTIVE ||
        device->input_work.handler == NULL ||
        device->input_work.notification != NULL
    ) {
        return false;
    }

    worker->device = device;
    worker->observer = NULL;
    worker->observer_context = NULL;

    return
        kernel_context_init(
            &worker->context,
            worker->stack,
            sizeof(worker->stack),
            console_input_worker_entry,
            worker
        ) &&
        kernel_context_prepare(
            &worker->context,
            kernel_context_bootstrap
        ) &&
        kernel_context_bind_supervisor(
            &worker->context,
            supervisor
        );
}

bool console_input_worker_notifications_enable(
    struct console_input_worker *worker)
{
    if (
        worker == NULL ||
        worker->device == NULL ||
        !worker->context.prepared ||
        worker->context.supervisor == NULL ||
        worker->context.state == KERNEL_CONTEXT_TERMINATED ||
        worker->device->input_work.notification != NULL
    ) {
        return false;
    }

    return deferred_work_notification_set(
        &worker->device->input_work,
        console_input_worker_notification,
        worker
    );
}

bool console_input_worker_notifications_disable(
    struct console_input_worker *worker)
{
    if (
        worker == NULL ||
        worker->device == NULL ||
        worker->device->input_work.notification !=
            console_input_worker_notification ||
        worker->device->input_work.notification_context !=
            worker
    ) {
        return false;
    }

    return deferred_work_notification_set(
        &worker->device->input_work,
        NULL,
        NULL
    );
}

static void console_input_worker_entry(
    void *argument)
{
    struct console_input_worker *worker = argument;

    if (
        worker == NULL ||
        worker->device == NULL
    ) {
        kernel_panic(
            "Console input worker received invalid argument"
        );
    }

    for (;;) {
        /*
         * Exactly one deferred-work handler invocation
         * per supervisor dispatch.
         *
         * The console handler itself limits the number
         * of normalized events processed.
         *
         * DEFERRED_WORK_MORE publishes another request;
         * the notification rearms this RUNNING worker.
         */
        size_t events_before = 0U;

        if (worker->observer != NULL) {
            events_before = input_event_pending();
        }

        size_t dispatched =
            console_device_dispatch_input(
                worker->device,
                1U
            );

        if (worker->observer != NULL) {
            worker->observer(
                worker->observer_context,
                worker->device,
                events_before,
                dispatched,
                input_event_pending()
            );
        }

        if (!kernel_context_yield_wait(
            &worker->context
        )) {
            kernel_panic(
                "Console input worker could not yield"
            );
        }
    }
}

static void console_input_worker_notification(
    void *argument)
{
    struct console_input_worker *worker = argument;

    /*
     * This callback may execute in IRQ context.
     * It only requests scheduling; no dispatch or
     * console processing occurs here.
     */
    if (
        worker == NULL ||
        !kernel_context_request_run(
            &worker->context
        )
    ) {
        kernel_panic(
            "Console input worker wake request rejected"
        );
    }
}

bool console_input_worker_observer_set(
    struct console_input_worker *worker,
    console_input_worker_observer_fn observer,
    void *context)
{
    if (
        worker == NULL ||
        worker->device == NULL ||
        !worker->context.prepared ||
        worker->context.state != KERNEL_CONTEXT_READY ||
        kernel_context_owner(&worker->context) !=
            KERNEL_CONTEXT_NO_CPU
    ) {
        return false;
    }

    worker->observer = observer;
    worker->observer_context = context;

    return true;
}
