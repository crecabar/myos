
 // SPDX-License-Identifier: GPL-2.0-only
 /*
  * Copyright (C) 2026 Cristian Recabarren
  */

 #include "wait_queue_test.h"

 #include "../core/wait_queue.h"
 #include "../core/panic.h"
 #include "../diagnostics/diagnostics.h"

 #include <stdbool.h>
 #include <stddef.h>

 struct wait_queue_test_probe {
     size_t calls;
     size_t order;
     bool attempted_registration;

     struct wait_queue *queue;
     struct wait_queue_entry *entry;
 };

 static size_t wait_queue_test_sequence;

 static void wait_queue_test_notification(
     void *context)
 {
     struct wait_queue_test_probe *probe =
         context;

     if (probe == NULL) {
         kernel_panic(
             "Wait queue notification has invalid context"
         );
     }

     ++probe->calls;
     probe->order = ++wait_queue_test_sequence;

     /*
      * A callback may not append a new entry while
      * the queue is notifying existing subscribers.
      */
     if (probe->attempted_registration) {
         if (
             wait_queue_register(
                 probe->queue,
                 probe->entry,
                 wait_queue_test_notification,
                 probe
             )
         ) {
             kernel_panic(
                 "Wait queue allowed registration during notification"
             );
         }
     }
 }

 void wait_queue_test_run(void)
 {
     struct wait_queue queue;
     struct wait_queue other;

     struct wait_queue_entry first;
     struct wait_queue_entry second;
     struct wait_queue_entry third;

     wait_queue_init(&queue);
     wait_queue_init(&other);

     wait_queue_entry_init(&first);
     wait_queue_entry_init(&second);
     wait_queue_entry_init(&third);

     wait_queue_test_sequence = 0U;

     struct wait_queue_test_probe a = {
         .queue = &queue,
         .entry = &first,
     };

     struct wait_queue_test_probe b = {
         .queue = &queue,
         .entry = &second,
     };

     struct wait_queue_test_probe c = {
         .queue = &queue,
         .entry = &third,
         .attempted_registration = true,
     };

     /*
      * Empty queues have no work to dispatch.
      */
     if (
         wait_queue_notify_all(&queue) != 0U ||
         queue.head != NULL ||
         queue.tail != NULL
     ) {
         kernel_panic(
             "Empty wait queue notification failed"
         );
     }

     /*
      * Register three independent waiters.
      */
     if (
         !wait_queue_register(
             &queue, &first,
             wait_queue_test_notification, &a
         ) ||
         !wait_queue_register(
             &queue, &second,
             wait_queue_test_notification, &b
         ) ||
         !wait_queue_register(
             &queue, &third,
             wait_queue_test_notification, &c
         )
     ) {
         kernel_panic(
             "Wait queue registration failed"
         );
     }

     if (
         !wait_queue_entry_registered(&first) ||
         !wait_queue_entry_registered(&second) ||
         !wait_queue_entry_registered(&third) ||
         queue.head != &first ||
         queue.tail != &third
     ) {
         kernel_panic(
             "Wait queue registration state invalid"
         );
     }

     /*
      * One entry may not belong to multiple queues.
      */
     if (
         wait_queue_register(
             &other, &first,
             wait_queue_test_notification, &a
         )
     ) {
         kernel_panic(
             "Wait queue accepted duplicate registration"
         );
     }

     /*
      * Cancel a middle entry without disturbing
      * registrations before or after it.
      */
     if (
         !wait_queue_cancel(&second) ||
         wait_queue_entry_registered(&second) ||
         queue.head != &first ||
         queue.tail != &third ||
         first.next != &third
     ) {
         kernel_panic(
             "Wait queue middle cancellation failed"
         );
     }

     /*
      * Re-registering appends at the FIFO tail.
      */
     if (
         !wait_queue_register(
             &queue, &second,
             wait_queue_test_notification, &b
         ) ||
         queue.tail != &second ||
         third.next != &second
     ) {
         kernel_panic(
             "Wait queue re-registration failed"
         );
     }

     /*
      * The expected callback order is:
      *
      * first -> third -> second
      */
     if (
         wait_queue_notify_all(&queue) != 3U ||
         a.calls != 1U ||
         b.calls != 1U ||
         c.calls != 1U ||
         a.order != 1U ||
         c.order != 2U ||
         b.order != 3U
     ) {
         kernel_panic(
             "Wait queue FIFO notification failed"
         );
     }

     /*
      * All entries were detached before callback
      * dispatch completed.
      */
     if (
         queue.head != NULL ||
         queue.tail != NULL ||
         queue.notifying ||
         wait_queue_entry_registered(&first) ||
         wait_queue_entry_registered(&second) ||
         wait_queue_entry_registered(&third) ||
         wait_queue_notify_all(&queue) != 0U
     ) {
         kernel_panic(
             "Wait queue notification leaked registrations"
         );
     }

     /*
      * A delivered entry cannot be cancelled twice.
      */
     if (
         wait_queue_cancel(&first) ||
         wait_queue_cancel(&second) ||
         wait_queue_cancel(&third)
     ) {
         kernel_panic(
             "Wait queue accepted cancellation after delivery"
         );
     }

     /*
      * Verify cancellation of head and last entry.
      */
     if (
         !wait_queue_register(
             &queue, &first,
             wait_queue_test_notification, &a
         ) ||
         !wait_queue_register(
             &queue, &second,
             wait_queue_test_notification, &b
         ) ||
         !wait_queue_cancel(&first) ||
         queue.head != &second ||
         !wait_queue_cancel(&second) ||
         queue.head != NULL ||
         queue.tail != NULL
     ) {
         kernel_panic(
             "Wait queue boundary cancellation failed"
         );
     }

     diagnostics_write(
         "[wait] Generic event wait queue tests passed\n"
     );
 }
