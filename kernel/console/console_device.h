// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file console_device.h
 * @brief System console character-device implementation.
 *
 * The console device exposes the kernel's visible text console through the
 * generic device and character-device contracts.
 *
 * Output is written to the visible framebuffer console. Input is received
 * as normalized keyboard events and assembled into canonical lines by the
 * console input consumer.
 *
 * Completed input lines are exposed through the character-device read
 * operation. When no completed line is available, reads return WOULD_BLOCK
 * rather than EOF.
 *
 * Input-event processing is explicitly dispatched outside this interface.
 * The read operation does not poll hardware, drain input events, or wait
 * for input to arrive.
 *
 * Blocking reads, process wakeups, full terminal semantics, ioctl, and
 * controlling-terminal behavior remain outside this bootstrap layer.
 *
 * VFS publication is separate: device existence is not a pathname.
 */

#ifndef MYOS_CONSOLE_CONSOLE_DEVICE_H
#define MYOS_CONSOLE_CONSOLE_DEVICE_H

#include "console.h"

#include "console_input_consumer.h"
#include "line_buffer.h"

#include <myos/kernel/core/deferred_work/deferred_work.h>
#include <myos/kernel/core/device/character_device.h>
#include <myos/kernel/core/device/device.h>
#include <myos/kernel/core/device/registry.h>

#include <stdint.h>

/*
 * Stable kernel-internal identity for the system console device.
 *
 * This is not a userspace major/minor number and does not form part of the
 * userspace ABI.
 */
#define CONSOLE_DEVICE_IDENTIFIER 3ULL

enum console_device_initialize_result {
    CONSOLE_DEVICE_INITIALIZE_RESULT_INITIALIZED,
    CONSOLE_DEVICE_INITIALIZE_RESULT_INVALID_ARGUMENT,
    CONSOLE_DEVICE_INITIALIZE_RESULT_REGISTRY_REJECTED,
    CONSOLE_DEVICE_INITIALIZE_RESULT_ROLLBACK_FAILED,
};

struct console_device;

/**
 * Called when console input becomes readable.
 *
 * Executes synchronously in the input consumer's
 * serialized execution context, not from the PS/2 IRQ.
 *
 * The callback must not block, consume input,
 * or initiate a context switch.
 */
typedef void (*console_device_readable_notification_fn)(
    void *context,
    struct console_device *device
);

/**
 * Caller-owned system-console device.
 *
 * device owns the generic device identity and lifetime.
 * character borrows device and exposes readable/writable stream operations.
 * console is borrowed and must outlive this object while it remains reachable.
 *
 * input owns the canonical line-editing and completed-line buffers.
 * input_consumer owns the normalized input event currently being processed,
 * including an event retained when the line buffer cannot accept it.
 *
 * Both input and input_consumer belong to this console device, not to an
 * individual VFS open-file description or process file descriptor.
 *
 * The current bootstrap execution model assumes serialized access to the
 * console input state. Concurrent access requires explicit synchronization
 * before SMP execution is enabled.
 *
 * The generic device retains its original caller-owned reference and, while
 * published, the registry owns one additional reference.
 */
struct console_device {
    struct device device;

    struct character_device character;

    struct console *console;
    struct console_line_buffer input;
    struct console_input_consumer input_consumer;

    /*
     * Visible canonical-line editing state.
     *
     * These coordinates belong to the framebuffer,
     * not to a userspace file descriptor.
     *
     * Output written by another producer invalidates
     * the visible editing region without modifying
     * the canonical input buffer.
     */
    uint64_t echo_start_x;
    uint64_t echo_start_y;
    uint64_t echo_scroll_count;
    size_t echo_visible_characters;

    struct deferred_work input_work;

    console_device_readable_notification_fn
        readable_notification;

    void *readable_notification_context;
};

/**
 * Initializes and publishes one readable/writable system console device.
 *
 * console must refer to an initialized visible console with a framebuffer.
 * registry must already be initialized.
 *
 * On success:
 *
 * - device is ACTIVE with its original owning reference;
 * - registry owns one additional device reference;
 * - character exposes both read and write operations;
 * - console remains borrowed;
 * - input starts with an empty editing buffer and no completed lines;
 * - input_consumer starts without a retained pending event.
 *
 * Reads return completed canonical-line bytes, supporting partial reads.
 * An empty completed-line queue returns WOULD_BLOCK rather than EOF.
 *
 * Writes forward the supplied byte stream to the visible console.
 *
 * A registry publication failure retires the newly initialized device
 * without withdrawing or modifying existing registry entries.
 */
enum console_device_initialize_result console_device_initialize(
    struct console_device *console_device,
    struct console *console,
    struct device_registry *registry
);

/**
 * Processes normalized input events for this console.
 *
 * Transfers at most budget events from the global input queue through the
 * console input consumer into this device's canonical line buffer.
 *
 * The consumer preserves event ordering and retains an event that cannot
 * currently be accepted because the line buffer is full.
 *
 * A zero budget performs no work. This operation does not access hardware,
 * wait for events, or block the calling execution context.
 *
 * The caller must provide an appropriate execution context outside IRQ
 * handling and ensure exclusive access to the console input state.
 *
 * @param device Active system-console device.
 * @param budget Maximum number of events to process.
 *
 * @return Number of accepted or ignored events processed.
 */
size_t console_device_drain_input(
    struct console_device *device,
    size_t budget
);

/**
 * Requests deferred processing of console input.
 *
 * Safe as a lightweight notification; does not process events.
 */
void console_device_request_input(
    struct console_device *device
);

/**
 * Dispatches previously requested console input work.
 *
 * Must be called outside IRQ context with exclusive access
 * to the console input state.
 *
 * budget counts handler invocations, not input events.
 */
size_t console_device_dispatch_input(
    struct console_device *device,
    size_t budget
);

/**
 * Registers or removes the console readability notification.
 *
 * Only one subscriber is supported by this bootstrap device.
 * Registering while another subscriber exists is rejected.
 *
 * Registration is serialized with input processing.
 * If a completed line already exists, the newly registered
 * callback is invoked immediately.
 *
 * Passing NULL removes the notification.
 */
bool console_device_readable_notification_set(
    struct console_device *device,
    console_device_readable_notification_fn notification,
    void *context
);

#endif
