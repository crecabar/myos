// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include <myos/kernel/core/deferred_work/deferred_work.h>

#include <stddef.h>

bool deferred_work_init(
    struct deferred_work *work,
    deferred_work_handler_fn handler,
    void *context)
{
    if (work == NULL || handler == NULL) {
        return false;
    }

    *work = (struct deferred_work) {
        .handler = handler,
        .context = context,
    };

    return true;
}

void deferred_work_request(
    struct deferred_work *work)
{
    if (work == NULL) {
        return;
    }

    __atomic_store_n(
        &work->pending,
        true,
        __ATOMIC_RELEASE
    );

    /*
     * Notify only after publishing the request.
     * The consumer decides whether a worker needs
     * to transition from WAITING to READY.
     */
    if (work->notification != NULL) {
        work->notification(
            work->notification_context
        );
    }
}

size_t deferred_work_dispatch(
    struct deferred_work *work,
    size_t budget)
{
    if (
        work == NULL ||
        work->handler == NULL ||
        budget == 0
    ) {
        return 0;
    }

    /*
     * Only one dispatcher may execute this work item.
     */
    if (
        __atomic_exchange_n(
            &work->running,
            true,
            __ATOMIC_ACQUIRE
        )
    ) {
        return 0;
    }

    size_t executed = 0;

    while (executed < budget) {
        /*
         * Atomically claim a pending request.
         * A concurrent producer can publish another
         * request immediately after this exchange.
         */
        if (
            !__atomic_exchange_n(
                &work->pending,
                false,
                __ATOMIC_ACQUIRE
            )
        ) {
            break;
        }

        enum deferred_work_result result =
            work->handler(work->context);

        ++executed;

        switch (result) {
            case DEFERRED_WORK_COMPLETED:
                break;

            case DEFERRED_WORK_MORE:
                deferred_work_request(work);
                break;

            case DEFERRED_WORK_WAIT:
                break;

            default:
                __atomic_store_n(
                    &work->running,
                    false,
                    __ATOMIC_RELEASE
                );

                return executed;
        }
    }

    __atomic_store_n(
        &work->running,
        false,
        __ATOMIC_RELEASE
    );

    return executed;
}

bool deferred_work_pending(
    const struct deferred_work *work)
{
    if (work == NULL) {
        return false;
    }

    return __atomic_load_n(
        &work->pending,
        __ATOMIC_ACQUIRE
    );
}

bool deferred_work_notification_set(
    struct deferred_work *work,
    deferred_work_notification_fn notification,
    void *context)
{
    if (
        work == NULL ||
        work->handler == NULL
    ) {
        return false;
    }

    /*
     * This bootstrap setter requires serialization
     * with request producers. It does not support
     * concurrent reconfiguration.
     */
    work->notification_context = context;
    work->notification = notification;

    /*
     * The request may have arrived before the
     * notification was installed.
     */
    if (
        notification != NULL &&
        deferred_work_pending(work)
    ) {
        notification(context);
    }

    return true;
}
