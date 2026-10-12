// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include "deferred_work_test.h"

#include "../core/deferred_work/deferred_work.h"
#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"

#include <stdbool.h>
#include <stddef.h>

struct deferred_work_test_context {
    struct deferred_work *work;
    enum deferred_work_result result;
    size_t calls;
    bool request_again;
    bool test_reentry;
    bool reentry_rejected;
};

struct deferred_work_notification_probe {
    struct deferred_work *work;
    size_t notifications;
};

static void deferred_work_test_notification_callback(
    void *argument)
{
    struct deferred_work_notification_probe *probe =
        argument;

    if (
        probe == NULL ||
        !deferred_work_pending(probe->work)
    ) {
        kernel_panic(
            "Deferred work notification preceded publication"
        );
    }

    ++probe->notifications;
}

static enum deferred_work_result deferred_work_test_handler(
    void *argument)
{
    struct deferred_work_test_context *context = argument;

    ++context->calls;

    if (context->test_reentry) {
        context->reentry_rejected =
            deferred_work_dispatch(
                context->work,
                1U
            ) == 0U;
    }

    if (context->request_again) {
        deferred_work_request(context->work);
    }

    return context->result;
}

static void deferred_work_test_requests(void)
{
    struct deferred_work work;
    struct deferred_work_test_context context = {
        .work = &work,
        .result = DEFERRED_WORK_COMPLETED,
    };

    if (
        !deferred_work_init(
            &work,
            deferred_work_test_handler,
            &context
        ) ||
        deferred_work_pending(&work) ||
        deferred_work_dispatch(&work, 4U) != 0U
    ) {
        kernel_panic("Deferred work initialization failed");
    }

    deferred_work_request(&work);
    deferred_work_request(&work);

    if (
        !deferred_work_pending(&work) ||
        deferred_work_dispatch(&work, 0U) != 0U ||
        deferred_work_dispatch(&work, 4U) != 1U ||
        context.calls != 1U ||
        deferred_work_pending(&work)
    ) {
        kernel_panic("Deferred work request coalescing failed");
    }
}

static void deferred_work_test_budget(void)
{
    struct deferred_work work;
    struct deferred_work_test_context context = {
        .work = &work,
        .result = DEFERRED_WORK_MORE,
    };

    if (!deferred_work_init(
        &work, deferred_work_test_handler, &context
    )) {
        kernel_panic("Deferred work budget setup failed");
    }

    deferred_work_request(&work);

    if (
        deferred_work_dispatch(&work, 2U) != 2U ||
        context.calls != 2U ||
        !deferred_work_pending(&work)
    ) {
        kernel_panic("Deferred work budget exceeded");
    }

    context.result = DEFERRED_WORK_COMPLETED;

    if (
        deferred_work_dispatch(&work, 1U) != 1U ||
        context.calls != 3U ||
        deferred_work_pending(&work)
    ) {
        kernel_panic("Deferred work continuation failed");
    }
}

static void deferred_work_test_wait(void)
{
    struct deferred_work work;
    struct deferred_work_test_context context = {
        .work = &work,
        .result = DEFERRED_WORK_WAIT,
    };

    if (!deferred_work_init(
        &work, deferred_work_test_handler, &context
    )) {
        kernel_panic("Deferred work wait setup failed");
    }

    deferred_work_request(&work);

    if (
        deferred_work_dispatch(&work, 8U) != 1U ||
        context.calls != 1U ||
        deferred_work_pending(&work) ||
        deferred_work_dispatch(&work, 8U) != 0U
    ) {
        kernel_panic("Deferred work wait contract failed");
    }

    deferred_work_request(&work);
    context.result = DEFERRED_WORK_COMPLETED;

    if (
        deferred_work_dispatch(&work, 8U) != 1U ||
        context.calls != 2U ||
        deferred_work_pending(&work)
    ) {
        kernel_panic("Deferred work wait recovery failed");
    }
}

static void deferred_work_test_reentry(void)
{
    struct deferred_work work;
    struct deferred_work_test_context context = {
        .work = &work,
        .result = DEFERRED_WORK_COMPLETED,
        .test_reentry = true,
    };

    if (!deferred_work_init(
        &work, deferred_work_test_handler, &context
    )) {
        kernel_panic("Deferred work reentry setup failed");
    }

    deferred_work_request(&work);

    if (
        deferred_work_dispatch(&work, 1U) != 1U ||
        context.calls != 1U ||
        !context.reentry_rejected ||
        deferred_work_pending(&work)
    ) {
        kernel_panic("Deferred work reentry contract failed");
    }
}

static void deferred_work_test_request_during_callback(void)
{
    struct deferred_work work;
    struct deferred_work_test_context context = {
        .work = &work,
        .result = DEFERRED_WORK_COMPLETED,
        .request_again = true,
    };

    if (!deferred_work_init(
        &work, deferred_work_test_handler, &context
    )) {
        kernel_panic("Deferred work callback request setup failed");
    }

    deferred_work_request(&work);

    if (
        deferred_work_dispatch(&work, 1U) != 1U ||
        context.calls != 1U ||
        !deferred_work_pending(&work)
    ) {
        kernel_panic("Deferred work lost callback request");
    }

    context.request_again = false;

    if (
        deferred_work_dispatch(&work, 1U) != 1U ||
        context.calls != 2U ||
        deferred_work_pending(&work)
    ) {
        kernel_panic("Deferred work callback request recovery failed");
    }
}

static void deferred_work_test_invalid(void)
{
    struct deferred_work work;

    if (
        deferred_work_init(NULL, deferred_work_test_handler, NULL) ||
        deferred_work_init(&work, NULL, NULL) ||
        deferred_work_pending(NULL) ||
        deferred_work_dispatch(NULL, 1U) != 0U
    ) {
        kernel_panic("Deferred work invalid argument contract failed");
    }

    deferred_work_request(NULL);
}

static void deferred_work_test_request_during_wait(void)
{
    struct deferred_work work;

    struct deferred_work_test_context context = {
        .work = &work,
        .result = DEFERRED_WORK_WAIT,
        .request_again = true,
    };

    if (
        !deferred_work_init(
            &work,
            deferred_work_test_handler,
            &context
        )
    ) {
        kernel_panic(
            "Deferred work wait request setup failed"
        );
    }

    deferred_work_request(&work);

    if (
        deferred_work_dispatch(&work, 1U) != 1U ||
        context.calls != 1U ||
        !deferred_work_pending(&work)
    ) {
        kernel_panic(
            "Deferred work lost request during wait"
        );
    }

    context.request_again = false;
    context.result = DEFERRED_WORK_COMPLETED;

    if (
        deferred_work_dispatch(&work, 1U) != 1U ||
        context.calls != 2U ||
        deferred_work_pending(&work)
    ) {
        kernel_panic(
            "Deferred work wait request recovery failed"
        );
    }
}

static void deferred_work_test_notification(void)
{
    struct deferred_work work;

    struct deferred_work_test_context context = {
        .work = &work,
        .result = DEFERRED_WORK_COMPLETED,
    };

    struct deferred_work_notification_probe probe = {
        .work = &work,
    };

    if (
        !deferred_work_init(
            &work,
            deferred_work_test_handler,
            &context
        ) ||
        !deferred_work_notification_set(
            &work,
            deferred_work_test_notification_callback,
            &probe
        )
    ) {
        kernel_panic(
            "Deferred work notification setup failed"
        );
    }

    deferred_work_request(&work);
    deferred_work_request(&work);

    if (
        probe.notifications != 2U ||
        deferred_work_dispatch(&work, 1U) != 1U ||
        context.calls != 1U ||
        deferred_work_pending(&work)
    ) {
        kernel_panic(
            "Deferred work notification coalescing failed"
        );
    }

    /*
     * Publish before installing a notification.
     * Installing it must report the outstanding work.
     */
    if (!deferred_work_notification_set(
        &work,
        NULL,
        NULL
    )) {
        kernel_panic(
            "Deferred work notification removal failed"
        );
    }

    deferred_work_request(&work);

    if (
        probe.notifications != 2U ||
        !deferred_work_notification_set(
            &work,
            deferred_work_test_notification_callback,
            &probe
        ) ||
        probe.notifications != 3U ||
        !deferred_work_pending(&work)
    ) {
        kernel_panic(
            "Deferred work pending notification lost"
        );
    }

    if (
        deferred_work_dispatch(&work, 1U) != 1U ||
        context.calls != 2U
    ) {
        kernel_panic(
            "Deferred work pending recovery failed"
        );
    }

    /*
     * Requests made by a handler returning MORE must
     * also generate notifications.
     */
    context.result = DEFERRED_WORK_MORE;

    deferred_work_request(&work);

    if (
        deferred_work_dispatch(&work, 1U) != 1U ||
        probe.notifications != 5U ||
        !deferred_work_pending(&work)
    ) {
        kernel_panic(
            "Deferred work continuation notification lost"
        );
    }

    if (!deferred_work_notification_set(
        &work,
        NULL,
        NULL
    )) {
        kernel_panic(
            "Deferred work final notification removal failed"
        );
    }

    diagnostics_write(
        "[deferred] Work notification tests passed\n"
    );
}

void deferred_work_test_run(void)
{
    deferred_work_test_requests();
    deferred_work_test_budget();
    deferred_work_test_wait();
    deferred_work_test_reentry();
    deferred_work_test_request_during_callback();
    deferred_work_test_invalid();
    deferred_work_test_request_during_wait();
    deferred_work_test_notification();

    diagnostics_write(
        "[deferred] Work state tests passed\n"
    );
}
