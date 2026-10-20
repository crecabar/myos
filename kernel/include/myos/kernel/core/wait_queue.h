// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#ifndef MYOS_KERNEL_CORE_WAIT_QUEUE_H
#define MYOS_KERNEL_CORE_WAIT_QUEUE_H

#include <stdbool.h>
#include <stddef.h>

struct wait_queue;
struct wait_queue_entry;

typedef void (*wait_queue_notification_fn)(
    void *context
);

/**
 * Caller-owned registration for one pending event.
 *
 * An entry belongs to at most one queue.
 * Its storage must remain valid while registered.
 */
struct wait_queue_entry {
    struct wait_queue *queue;
    struct wait_queue_entry *next;

    wait_queue_notification_fn notification;
    void *context;
};

/**
 * FIFO collection of pending event registrations.
 *
 * The queue does not own entry storage.
 *
 * All operations require serialization with producers
 * and consumers. The BSP implementation will use
 * the scheduler's interrupts-disabled critical section.
 */
struct wait_queue {
    struct wait_queue_entry *head;
    struct wait_queue_entry *tail;

    bool notifying;
};

/**
 * Initializes an unpublished, empty queue.
 *
 * Must not be called while registrations exist.
 */
void wait_queue_init(struct wait_queue *queue);

/**
 * Initializes an entry that is not registered.
 *
 * Must not be called while the entry belongs to a queue.
 */
void wait_queue_entry_init(
    struct wait_queue_entry *entry
);

/**
 * Appends an entry to a queue.
 *
 * Rejects duplicate registration and attempts to
 * modify a queue while notifications are dispatched.
 */
bool wait_queue_register(
    struct wait_queue *queue,
    struct wait_queue_entry *entry,
    wait_queue_notification_fn notification,
    void *context
);

/**
 * Removes a pending registration.
 *
 * Returns false if the entry is not registered.
 */
bool wait_queue_cancel(
    struct wait_queue_entry *entry
);

/**
 * Notifies and removes every registration still pending.
 *
 * Entries are detached before invoking their callbacks.
 * Callbacks may release their own detached entry storage.
 *
 * Callbacks must not block or switch execution context.
 * Registrations on this queue are rejected during dispatch.
 *
 * Returns the number of callbacks invoked.
 */
size_t wait_queue_notify_all(
    struct wait_queue *queue
);

bool wait_queue_entry_registered(
    const struct wait_queue_entry *entry
);

#endif
