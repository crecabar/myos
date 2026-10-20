
 // SPDX-License-Identifier: GPL-2.0-only
 /*
  * Copyright (C) 2026 Cristian Recabarren
  */

 #include <myos/kernel/core/wait_queue.h>

 #include <stddef.h>

 void wait_queue_init(struct wait_queue *queue)
 {
     if (queue == NULL) {
         return;
     }

     *queue = (struct wait_queue) {0};
 }

 void wait_queue_entry_init(
     struct wait_queue_entry *entry)
 {
     if (entry == NULL) {
         return;
     }

     *entry = (struct wait_queue_entry) {0};
 }

 bool wait_queue_register(
     struct wait_queue *queue,
     struct wait_queue_entry *entry,
     wait_queue_notification_fn notification,
     void *context)
 {
     if (
         queue == NULL ||
         entry == NULL ||
         notification == NULL ||
         queue->notifying ||
         entry->queue != NULL ||
         entry->next != NULL
     ) {
         return false;
     }

     entry->queue = queue;
     entry->next = NULL;
     entry->notification = notification;
     entry->context = context;

     if (queue->tail != NULL) {
         queue->tail->next = entry;
     } else {
         queue->head = entry;
     }

     queue->tail = entry;

     return true;
 }

 bool wait_queue_cancel(
     struct wait_queue_entry *entry)
 {
     if (
         entry == NULL ||
         entry->queue == NULL
     ) {
         return false;
     }

     struct wait_queue *queue =
         entry->queue;

     struct wait_queue_entry *previous = NULL;
     struct wait_queue_entry *current = queue->head;

     while (
         current != NULL &&
         current != entry
     ) {
         previous = current;
         current = current->next;
     }

     if (current == NULL) {
         return false;
     }

     if (previous == NULL) {
         queue->head = current->next;
     } else {
         previous->next = current->next;
     }

     if (queue->tail == current) {
         queue->tail = previous;
     }

     entry->queue = NULL;
     entry->next = NULL;
     entry->notification = NULL;
     entry->context = NULL;

     return true;
 }

 size_t wait_queue_notify_all(
     struct wait_queue *queue)
 {
     if (
         queue == NULL ||
         queue->notifying
     ) {
         return 0U;
     }

     queue->notifying = true;

     size_t notified = 0U;

     while (queue->head != NULL) {
         struct wait_queue_entry *entry =
             queue->head;

         queue->head = entry->next;

         if (queue->head == NULL) {
             queue->tail = NULL;
         }

         wait_queue_notification_fn notification =
             entry->notification;

         void *context = entry->context;

         /*
          * Detach before callback invocation.
          *
          * The callback may release the entry's
          * storage, so nothing may access entry
          * after notification returns.
          */
         entry->queue = NULL;
         entry->next = NULL;
         entry->notification = NULL;
         entry->context = NULL;

         notification(context);

         ++notified;
     }

     queue->notifying = false;

     return notified;
 }

 bool wait_queue_entry_registered(
     const struct wait_queue_entry *entry)
 {
     return
         entry != NULL &&
         entry->queue != NULL;
 }
