// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file console_device_test.c
 * @brief System console character-device regression tests.
 */

#include "console_device_test.h"

#include "../arch/x86_64/cpu_identity.h"
#include "../arch/x86_64/interrupts.h"
#include "../console/console.h"
#include "../console/console_device.h"
#include "../console/console_input_worker.h"
#include <myos/kernel/core/panic.h>
#include <myos/kernel/core/wait_queue.h>
#include <myos/kernel/core/cpu/cpu_local.h>
#include <myos/kernel/core/device/character_device.h>
#include <myos/kernel/core/device/device.h>
#include <myos/kernel/core/device/registry.h>
#include <myos/kernel/core/execution/kernel_dispatch.h>
#include "../diagnostics/diagnostics.h"
#include "../drivers/framebuffer.h"
#include <myos/kernel/input/input.h>

#include <stddef.h>
#include <stdint.h>

#define CONSOLE_DEVICE_TEST_WIDTH 64U
#define CONSOLE_DEVICE_TEST_HEIGHT 32U

/*
 * Borrowed only while the automatic-notification fixture is active.
 */
static struct console_device *console_device_test_notification_target;

struct console_worker_observation {
    const struct console_device *device;

    size_t calls;
    size_t before[4];
    size_t dispatched[4];
    size_t after[4];
};

static void console_device_test_worker_observer(
    void *context,
    const struct console_device *device,
    size_t before,
    size_t dispatched,
    size_t after)
{
    struct console_worker_observation *probe =
        context;

    if (
        probe == NULL ||
        device != probe->device ||
        probe->calls >= 4U
    ) {
        kernel_panic(
            "Console worker observer received invalid state"
        );
    }

    size_t index = probe->calls++;

    probe->before[index] = before;
    probe->dispatched[index] = dispatched;
    probe->after[index] = after;
}

struct console_readability_probe {
    struct console_device *device;
    size_t calls;
};

struct console_read_wait_probe {
    size_t calls;
};

static void console_device_test_read_wait_notification(
    void *context)
{
    struct console_read_wait_probe *probe =
        context;

    if (probe == NULL) {
        kernel_panic(
            "Console read waiter has invalid context"
        );
    }

    ++probe->calls;
}

static void console_device_test_readable_notification(
    void *context,
    struct console_device *device)
{
    struct console_readability_probe *probe =
        context;

    if (
        probe == NULL ||
        probe->device != device
    ) {
        kernel_panic(
            "Console readability notification received invalid state"
        );
    }

    ++probe->calls;
}

static bool console_device_test_glyph_visible(
    const uint32_t *pixels,
    uint64_t x,
    uint64_t y)
{
    for (uint64_t row = 0U; row < 8U; ++row) {
        for (
            uint64_t column = 0U;
            column < 8U;
            ++column
        ) {
            if (
                pixels[
                    (y + row) *
                    CONSOLE_DEVICE_TEST_WIDTH +
                    x + column
                ] != 0U
            ) {
                return true;
            }
        }
    }

    return false;
}

// Private functions and helpers declarations
static void console_device_test_unregister(
    struct device_registry *registry,
    struct device *device
);

static void console_device_test_registration_and_write(void);

static void console_device_test_deferred_input(void);

static void console_device_test_input_notification(void);

static void console_device_test_kernel_worker(
    struct console_device *device
    );

static void console_device_test_registration_rollback(void);

static void console_device_test_readability(
    struct console_device *device
);

static void console_device_test_expect_read_ready(
    struct console_device *device,
    bool expected);

static void console_device_test_read_waiters(
    struct console_device *device
    );

static void console_device_test_readability_key(
    struct console_device *device,
    enum input_key_code code
);

static void console_device_test_ctrl_d(
    struct console_device *device);

static void console_device_test_eof_readiness(
    struct console_device *device);

static void console_device_test_eof_edit_boundary(
    struct console_device *device);

static void console_device_test_readability_expect_line(
    struct console_device *device,
    const char *expected,
    size_t expected_length
);

// Public functions implementations
void console_device_test_run(void)
{
    console_device_test_registration_and_write();
    console_device_test_deferred_input();
    console_device_test_registration_rollback();

    diagnostics_write(
        "[device] System console device tests passed\n"
    );
}

// Private functions and helpers implementations
static void console_device_test_unregister(
    struct device_registry *registry,
    struct device *device)
{
    if (
        registry == NULL ||
        device == NULL
    ) {
        kernel_panic(
            "Console-device cleanup received invalid arguments"
        );
    }

    struct device *detached =
        NULL;

    if (
        device_registry_unregister(
            registry,
            device,
            &detached
        ) !=
            DEVICE_REGISTRY_UNREGISTER_RESULT_UNREGISTERED ||
        detached !=
            device ||
        detached->state !=
            DEVICE_STATE_REMOVING ||
        !device_finish_removal(
            detached
        ) ||
        !device_release(
            detached
        ) ||
        !device_release(
            device
        )
    ) {
        kernel_panic(
            "Console-device fixture cleanup failed"
        );
    }
}

static void console_device_test_input_notification(void)
{
    if (console_device_test_notification_target == NULL) {
        kernel_panic(
            "Console input notification has no target"
        );
    }

    /*
     * Mirror the production notification callback:
     * request deferred work without consuming input.
     */
    console_device_request_input(
        console_device_test_notification_target
    );
}

static void console_device_test_read_waiters(
    struct console_device *device)
{
    if (
        device == NULL ||
        device->input.completed_count != 0U ||
        device->input.editing_length != 0U ||
        device->readable_notification != NULL ||
        device->character.read_waiters.head != NULL ||
        device->character.read_waiters.tail != NULL ||
        input_event_pending() != 0U
    ) {
        kernel_panic(
            "Console read waiters fixture has dirty initial state"
        );
    }

    struct wait_queue_entry first;
    struct wait_queue_entry second;

    wait_queue_entry_init(&first);
    wait_queue_entry_init(&second);

    struct console_read_wait_probe a = {0};
    struct console_read_wait_probe b = {0};

    struct console_readability_probe legacy = {
        .device = device,
        .calls = 0U,
    };

    /*
     * Both notification interfaces coexist.
     *
     * Registration does not consume or query input.
     */
    if (
        !console_device_readable_notification_set(
            device,
            console_device_test_readable_notification,
            &legacy
        ) ||
        !character_device_read_wait_register(
            &device->character,
            &first,
            console_device_test_read_wait_notification,
            &a
        ) ||
        !character_device_read_wait_register(
            &device->character,
            &second,
            console_device_test_read_wait_notification,
            &b
        )
    ) {
        kernel_panic(
            "Console read waiters registration failed"
        );
    }

    /*
     * Ordinary characters are not yet readable.
     */
    console_device_test_readability_key(
        device,
        INPUT_KEY_H
    );

    console_device_test_readability_key(
        device,
        INPUT_KEY_I
    );

    console_device_test_expect_read_ready(
        device,
        false
    );

    if (
        a.calls != 0U ||
        b.calls != 0U ||
        legacy.calls != 0U
    ) {
        kernel_panic(
            "Console read waiters notified before Enter"
        );
    }

    /*
     * Enter makes the canonical line readable.
     *
     * Both waiters receive one notification, while
     * the original observer is also notified.
     */
    console_device_test_readability_key(
        device,
        INPUT_KEY_ENTER
    );

    console_device_test_expect_read_ready(
        device,
        true
    );

    if (
        a.calls != 1U ||
        b.calls != 1U ||
        legacy.calls != 1U ||
        wait_queue_entry_registered(&first) ||
        wait_queue_entry_registered(&second)
    ) {
        kernel_panic(
            "Console read waiters broadcast failed"
        );
    }

    /*
     * Start another line while the first completed
     * line remains unread.
     *
     * No new empty-to-readable transition occurs.
     */
    console_device_test_readability_key(
        device,
        INPUT_KEY_H
    );

    if (
        a.calls != 1U ||
        b.calls != 1U ||
        legacy.calls != 1U
    ) {
        kernel_panic(
            "Console read waiters duplicated notification"
        );
    }

    static const char first_line[] = "hi\n";

    console_device_test_readability_expect_line(
        device,
        first_line,
        sizeof(first_line) - 1U
    );

    console_device_test_expect_read_ready(
        device,
        false
    );

    /*
     * Register a second round, but cancel waiter A.
     *
     * Only waiter B should receive the next event.
     */
    if (
        !character_device_read_wait_register(
            &device->character,
            &first,
            console_device_test_read_wait_notification,
            &a
        ) ||
        !character_device_read_wait_register(
            &device->character,
            &second,
            console_device_test_read_wait_notification,
            &b
        ) ||
        !wait_queue_cancel(&first)
    ) {
        kernel_panic(
            "Console read waiters cancellation setup failed"
        );
    }

    console_device_test_readability_key(
        device,
        INPUT_KEY_ENTER
    );

    if (
        a.calls != 1U ||
        b.calls != 2U ||
        legacy.calls != 2U ||
        wait_queue_entry_registered(&first) ||
        wait_queue_entry_registered(&second)
    ) {
        kernel_panic(
            "Console read waiters cancellation contract failed"
        );
    }

    static const char second_line[] = "h\n";

    console_device_test_readability_expect_line(
        device,
        second_line,
        sizeof(second_line) - 1U
    );

    console_device_test_expect_read_ready(
        device,
        false
    );

    /*
     * Remove the legacy observer and register both
     * waiters again.
     *
     * An empty canonical line is still readable.
     */
    if (
        !console_device_readable_notification_set(
            device,
            NULL,
            NULL
        ) ||
        !character_device_read_wait_register(
            &device->character,
            &first,
            console_device_test_read_wait_notification,
            &a
        ) ||
        !character_device_read_wait_register(
            &device->character,
            &second,
            console_device_test_read_wait_notification,
            &b
        )
    ) {
        kernel_panic(
            "Console read waiters final registration failed"
        );
    }

    console_device_test_readability_key(
        device,
        INPUT_KEY_ENTER
    );

    if (
        a.calls != 2U ||
        b.calls != 3U ||
        legacy.calls != 2U ||
        wait_queue_entry_registered(&first) ||
        wait_queue_entry_registered(&second)
    ) {
        kernel_panic(
            "Console read waiters final broadcast failed"
        );
    }

    static const char empty_line[] = "\n";

    console_device_test_readability_expect_line(
        device,
        empty_line,
        sizeof(empty_line) - 1U
    );

    /*
     * Leave the shared console fixture clean.
     */
    if (
        device->input.completed_count != 0U ||
        device->input.editing_length != 0U ||
        device->readable_notification != NULL ||
        device->readable_notification_context != NULL ||
        device->character.read_waiters.head != NULL ||
        device->character.read_waiters.tail != NULL ||
        device->character.read_waiters.notifying ||
        input_event_pending() != 0U ||
        deferred_work_pending(&device->input_work)
    ) {
        kernel_panic(
            "Console read waiters fixture leaked state"
        );
    }
}

static void console_device_test_readability_key(
    struct console_device *device,
    enum input_key_code code)
{
    struct input_event event = {
        .type = INPUT_EVENT_KEY,
        .key = {
            .code = code,
            .state = INPUT_KEY_PRESSED,
        },
    };

    if (
        !input_event_submit(&event) ||
        console_device_drain_input(device, 1U) != 1U ||
        input_event_pending() != 0U
    ) {
        kernel_panic(
            "Console readability key dispatch failed"
        );
    }
}

static void console_device_test_ctrl_d(
    struct console_device *device)
{
    const struct {
        enum input_key_code code;
        enum input_key_state state;
    } keys[] = {
        { INPUT_KEY_LEFT_CTRL, INPUT_KEY_PRESSED },
        { INPUT_KEY_D, INPUT_KEY_PRESSED },
        { INPUT_KEY_D, INPUT_KEY_RELEASED },
        { INPUT_KEY_LEFT_CTRL, INPUT_KEY_RELEASED },
    };

    for (
        size_t index = 0U;
        index < sizeof(keys) / sizeof(keys[0]);
        ++index
    ) {
        struct input_event event = {
            .type = INPUT_EVENT_KEY,
            .key = {
                .code = keys[index].code,
                .state = keys[index].state,
            },
        };

        if (
            !input_event_submit(&event) ||
            console_device_drain_input(
                device, 1U
            ) != 1U ||
            input_event_pending() != 0U
        ) {
            kernel_panic(
                "Console Ctrl+D device dispatch failed"
            );
        }
    }
}

static void console_device_test_eof_readiness(
    struct console_device *device)
{
    if (
        device == NULL ||
        device->input.completed_count != 0U ||
        device->input.editing_length != 0U ||
        device->echo_visible_characters != 0U ||
        device->readable_notification != NULL ||
        device->character.read_waiters.head != NULL ||
        input_event_pending() != 0U
    ) {
        kernel_panic(
            "Console EOF device fixture is not clean"
        );
    }

    console_device_test_expect_read_ready(
        device, false
    );

    uint8_t output[4] = {0};
    size_t bytes_read = SIZE_MAX;

    if (
        character_device_read(
            &device->character,
            output,
            sizeof(output),
            &bytes_read
        ) != CHARACTER_DEVICE_IO_RESULT_WOULD_BLOCK ||
        bytes_read != SIZE_MAX
    ) {
        kernel_panic(
            "Console empty input was confused with EOF"
        );
    }

    struct wait_queue_entry waiter;
    wait_queue_entry_init(&waiter);

    struct console_read_wait_probe probe = {0};

    struct console_readability_probe legacy = {
        .device = device,
        .calls = 0U,
    };

    if (
        !console_device_readable_notification_set(
            device,
            console_device_test_readable_notification,
            &legacy
        ) ||
        !character_device_read_wait_register(
            &device->character,
            &waiter,
            console_device_test_read_wait_notification,
            &probe
        )
    ) {
        kernel_panic(
            "Console EOF waiter registration failed"
        );
    }

    uint64_t cursor_x = device->console->cursor_x;
    uint64_t cursor_y = device->console->cursor_y;

    console_device_test_ctrl_d(device);

    console_device_test_expect_read_ready(
        device, true
    );

    if (
        probe.calls != 1U ||
        legacy.calls != 1U ||
        wait_queue_entry_registered(&waiter) ||
        device->input.completed_count != 1U ||
        !device->input.completed_eof[
            device->input.completed_head
        ] ||
        device->console->cursor_x != cursor_x ||
        device->console->cursor_y != cursor_y ||
        device->echo_visible_characters != 0U
    ) {
        kernel_panic(
            "Console Ctrl+D readiness or echo failed"
        );
    }

    if (
        character_device_read(
            &device->character,
            output,
            sizeof(output),
            &bytes_read
        ) != CHARACTER_DEVICE_IO_RESULT_SUCCESS ||
        bytes_read != 0U
    ) {
        kernel_panic(
            "Console Ctrl+D did not deliver zero-byte EOF"
        );
    }

    console_device_test_expect_read_ready(
        device, false
    );

    bytes_read = SIZE_MAX;

    if (
        character_device_read(
            &device->character,
            output,
            sizeof(output),
            &bytes_read
        ) != CHARACTER_DEVICE_IO_RESULT_WOULD_BLOCK ||
        bytes_read != SIZE_MAX
    ) {
        kernel_panic(
            "Console EOF was not consumed exactly once"
        );
    }

    /*
     * The same console must accept new input
     * after EOF has been consumed.
     */
    if (
        !console_device_readable_notification_set(
            device, NULL, NULL
        ) ||
        !character_device_read_wait_register(
            &device->character,
            &waiter,
            console_device_test_read_wait_notification,
            &probe
        )
    ) {
        kernel_panic(
            "Console EOF recovery registration failed"
        );
    }

    console_device_test_readability_key(
        device, INPUT_KEY_A
    );

    console_device_test_expect_read_ready(
        device, false
    );

    if (probe.calls != 1U) {
        kernel_panic(
            "Console notified before new line completion"
        );
    }

    console_device_test_readability_key(
        device, INPUT_KEY_ENTER
    );

    console_device_test_expect_read_ready(
        device, true
    );

    if (
        probe.calls != 2U ||
        wait_queue_entry_registered(&waiter) ||
        character_device_read(
            &device->character,
            output,
            sizeof(output),
            &bytes_read
        ) != CHARACTER_DEVICE_IO_RESULT_SUCCESS ||
        bytes_read != 2U ||
        output[0] != 'a' ||
        output[1] != '\n'
    ) {
        kernel_panic(
            "Console failed to resume normal input after EOF"
        );
    }

    console_device_test_expect_read_ready(
        device, false
    );

    if (
        device->input.completed_count != 0U ||
        device->input.editing_length != 0U ||
        device->input_consumer.has_pending_event ||
        device->character.read_waiters.head != NULL ||
        input_event_pending() != 0U ||
        deferred_work_pending(&device->input_work)
    ) {
        kernel_panic(
            "Console EOF device test leaked state"
        );
    }
}

static void console_device_test_eof_edit_boundary(
    struct console_device *device)
{
    if (
        device == NULL ||
        device->input.completed_count != 0U ||
        device->input.editing_length != 0U ||
        device->echo_visible_characters != 0U
    ) {
        kernel_panic(
            "Console EOF editing fixture is not clean"
        );
    }

    /*
     * Start at a text-row boundary, avoiding
     * an unrelated automatic wrap.
     */
    console_put_char(device->console, '\n');

    uint64_t start_x = device->console->cursor_x;
    uint64_t start_y = device->console->cursor_y;

    console_device_test_readability_key(
        device, INPUT_KEY_A
    );

    console_device_test_readability_key(
        device, INPUT_KEY_B
    );

    if (
        device->input.editing_length != 2U ||
        device->echo_visible_characters != 2U
    ) {
        kernel_panic(
            "Console EOF visual fixture setup failed"
        );
    }

    uint64_t end_x = device->console->cursor_x;
    uint64_t end_y = device->console->cursor_y;

    console_device_test_ctrl_d(device);

    if (
        device->input.editing_length != 0U ||
        device->input.completed_count != 1U ||
        device->echo_visible_characters != 0U ||
        device->console->cursor_x != end_x ||
        device->console->cursor_y != end_y
    ) {
        kernel_panic(
            "Console EOF did not close editing segment"
        );
    }

    /*
     * Backspace after EOF must not erase
     * characters from the completed segment.
     */
    console_device_test_readability_key(
        device, INPUT_KEY_BACKSPACE
    );

    if (
        device->echo_visible_characters != 0U ||
        device->console->cursor_x != end_x ||
        device->console->cursor_y != end_y
    ) {
        kernel_panic(
            "Console Backspace crossed EOF boundary"
        );
    }

    uint8_t output[4] = {0};
    size_t bytes_read = 0U;

    if (
        character_device_read(
            &device->character,
            output,
            sizeof(output),
            &bytes_read
        ) != CHARACTER_DEVICE_IO_RESULT_SUCCESS ||
        bytes_read != 2U ||
        output[0] != 'a' ||
        output[1] != 'b'
    ) {
        kernel_panic(
            "Console EOF lost completed text"
        );
    }

    /*
     * A new character starts a fresh editable
     * segment at the current cursor position.
     */
    console_device_test_readability_key(
        device, INPUT_KEY_C
    );

    if (
        device->input.editing_length != 1U ||
        device->echo_visible_characters != 1U
    ) {
        kernel_panic(
            "Console failed to start editing after EOF"
        );
    }

    console_device_test_readability_key(
        device, INPUT_KEY_BACKSPACE
    );

    if (
        device->input.editing_length != 0U ||
        device->echo_visible_characters != 0U ||
        device->console->cursor_x != end_x ||
        device->console->cursor_y != end_y ||
        device->input.completed_count != 0U
    ) {
        kernel_panic(
            "Console new editing segment erased incorrectly"
        );
    }

    /*
     * The original visible text remains intact.
     */
    if (
        !console_device_test_glyph_visible(
            device->console->framebuffer->address,
            start_x,
            start_y
        ) ||
        !console_device_test_glyph_visible(
            device->console->framebuffer->address,
            start_x + 9U,
            start_y
        )
    ) {
        kernel_panic(
            "Console EOF erased completed visual text"
        );
    }
}

static void console_device_test_readability_expect_line(
    struct console_device *device,
    const char *expected,
    size_t expected_length)
{
    uint8_t buffer[4] = {0};
    size_t bytes_read = SIZE_MAX;

    if (
        expected == NULL ||
        expected_length > sizeof(buffer)
    ) {
        kernel_panic(
            "Console readability expected line is invalid"
        );
    }

    if (
        character_device_read(
            &device->character,
            buffer,
            sizeof(buffer),
            &bytes_read
        ) != CHARACTER_DEVICE_IO_RESULT_SUCCESS ||
        bytes_read != expected_length
    ) {
        kernel_panic(
            "Console readability line read failed"
        );
    }

    for (
        size_t index = 0;
        index < expected_length;
        ++index
    ) {
        if (
            buffer[index] !=
                (uint8_t) expected[index]
        ) {
            kernel_panic(
                "Console readability line contents mismatch"
            );
        }
    }
}

static void console_device_test_expect_read_ready(
    struct console_device *device,
    bool expected)
{
    bool ready = !expected;

    if (
        !character_device_read_ready(
            &device->character,
            &ready
        ) ||
        ready != expected
    ) {
        kernel_panic(
            "Console character readiness state mismatch"
        );
    }
}

static void console_device_test_readability(
    struct console_device *device)
{
    if (
        device == NULL ||
        device->input.completed_count != 0U ||
        device->input.editing_length != 0U ||
        input_event_pending() != 0U ||
        deferred_work_pending(&device->input_work)
    ) {
        kernel_panic(
            "Console readability fixture has dirty initial state"
        );
    }

    console_device_test_expect_read_ready(
        device,
        false
    );

    struct console_readability_probe probe = {
        .device = device,
        .calls = 0U,
    };

    /*
     * 1. Register on an empty queue.
     *
     * No notification should occur.
     */
    if (
        !console_device_readable_notification_set(
            device,
            console_device_test_readable_notification,
            &probe
        ) ||
        probe.calls != 0U
    ) {
        kernel_panic(
            "Console readability empty registration failed"
        );
    }

    /*
     * 2. Ordinary key presses are not yet readable.
     *
     * Canonical input requires Enter.
     */
    console_device_test_readability_key(
        device,
        INPUT_KEY_H
    );

    console_device_test_readability_key(
        device,
        INPUT_KEY_I
    );

    if (
        probe.calls != 0U ||
        device->input.completed_count != 0U ||
        device->input.editing_length != 2U
    ) {
        kernel_panic(
            "Console readability notified before Enter"
        );
    }

    console_device_test_expect_read_ready(
        device,
        false
    );

    /*
     * 3. Enter completes the first canonical line.
     *
     * The empty-to-readable transition must notify
     * exactly once.
     */
    console_device_test_readability_key(
        device,
        INPUT_KEY_ENTER
    );

    if (
        probe.calls != 1U ||
        device->input.completed_count != 1U ||
        device->input.editing_length != 0U
    ) {
        kernel_panic(
            "Console readability first transition failed"
        );
    }

    console_device_test_expect_read_ready(
        device,
        true
    );

    /*
     * 4. Additional input while a completed line remains
     * available must not produce a new notification.
     *
     * Start editing the next line without Enter.
     */
    console_device_test_readability_key(
        device,
        INPUT_KEY_H
    );

    if (
        probe.calls != 1U ||
        device->input.completed_count != 1U ||
        device->input.editing_length != 1U
    ) {
        kernel_panic(
            "Console readability duplicated notification"
        );
    }

    /*
     * 5. A second subscriber must be rejected.
     *
     * The original callback and context remain installed.
     */
    struct console_readability_probe rejected = {
        .device = device,
        .calls = 0U,
    };

    if (
        console_device_readable_notification_set(
            device,
            console_device_test_readable_notification,
            &rejected
        ) ||
        probe.calls != 1U ||
        rejected.calls != 0U
    ) {
        kernel_panic(
            "Console readability accepted duplicate subscriber"
        );
    }

    /*
     * 6. Unregister and register again while readable.
     *
     * The replacement registration must immediately
     * observe the already completed line.
     */
    if (
        !console_device_readable_notification_set(
            device,
            NULL,
            NULL
        ) ||
        !console_device_readable_notification_set(
            device,
            console_device_test_readable_notification,
            &probe
        ) ||
        probe.calls != 2U ||
        rejected.calls != 0U
    ) {
        kernel_panic(
            "Console readability immediate notification failed"
        );
    }

    /*
     * Consume the first line completely.
     *
     * The second line still has one character being
     * edited, but no completed line remains readable.
     */
    static const char first_line[] = "hi\n";

    console_device_test_readability_expect_line(
        device,
        first_line,
        sizeof(first_line) - 1U
    );

    if (
        device->input.completed_count != 0U ||
        device->input.editing_length != 1U ||
        probe.calls != 2U
    ) {
        kernel_panic(
            "Console readability first line cleanup failed"
        );
    }

    console_device_test_expect_read_ready(
        device,
        false
    );

    /*
     * 7. Enter completes the second line.
     *
     * Since readability returned to empty, the next
     * completed line must produce a new notification.
     */
    console_device_test_readability_key(
        device,
        INPUT_KEY_ENTER
    );

    if (
        probe.calls != 3U ||
        device->input.completed_count != 1U
    ) {
        kernel_panic(
            "Console readability second transition failed"
        );
    }

    /*
     * 8. Remove the subscriber.
     *
     * Subsequent completed lines must not invoke the
     * old callback.
     */
    if (
        !console_device_readable_notification_set(
            device,
            NULL,
            NULL
        )
    ) {
        kernel_panic(
            "Console readability notification removal failed"
        );
    }

    static const char second_line[] = "h\n";

    console_device_test_readability_expect_line(
        device,
        second_line,
        sizeof(second_line) - 1U
    );

    /*
     * An empty canonical line is still readable.
     * No observer is installed now.
     */
    console_device_test_readability_key(
        device,
        INPUT_KEY_ENTER
    );

    if (
        probe.calls != 3U ||
        rejected.calls != 0U ||
        device->input.completed_count != 1U
    ) {
        kernel_panic(
            "Console readability notified after removal"
        );
    }

    static const char empty_line[] = "\n";

    console_device_test_readability_expect_line(
        device,
        empty_line,
        sizeof(empty_line) - 1U
    );

    /*
     * Leave the shared deferred-input fixture clean.
     *
     * The notification was removed before this
     * function's stack-owned probe goes out of scope.
     */
    if (
        device->readable_notification != NULL ||
        device->readable_notification_context != NULL ||
        device->input.completed_count != 0U ||
        device->input.editing_length != 0U ||
        device->input_consumer.has_pending_event ||
        input_event_pending() != 0U ||
        deferred_work_pending(&device->input_work)
    ) {
        kernel_panic(
            "Console readability fixture leaked state"
        );
    }
}


static void console_device_test_registration_and_write(void)
{
    static uint32_t pixels[
        CONSOLE_DEVICE_TEST_WIDTH *
        CONSOLE_DEVICE_TEST_HEIGHT
    ];

    for (
        size_t index = 0;
        index <
            CONSOLE_DEVICE_TEST_WIDTH *
            CONSOLE_DEVICE_TEST_HEIGHT;
        ++index
    ) {
        pixels[index] =
            0;
    }

    struct framebuffer framebuffer = {
        .address =
            pixels,
        .width =
            CONSOLE_DEVICE_TEST_WIDTH,
        .height =
            CONSOLE_DEVICE_TEST_HEIGHT,
        .pitch =
            CONSOLE_DEVICE_TEST_WIDTH *
            sizeof(uint32_t),
        .bpp =
            32,
        .red_mask_size =
            8,
        .red_mask_shift =
            16,
        .green_mask_size =
            8,
        .green_mask_shift =
            8,
        .blue_mask_size =
            8,
        .blue_mask_shift =
            0,
    };

    struct console console;

    console_init(
        &console,
        &framebuffer,
        0,
        0,
        0x00ffffffU,
        1
    );

    struct device_registry registry;

    if (!device_registry_initialize(
        &registry
    )) {
        kernel_panic(
            "Console-device registry initialization failed"
        );
    }

    struct console_device device;

    if (
        console_device_initialize(
            &device,
            &console,
            &registry
        ) !=
            CONSOLE_DEVICE_INITIALIZE_RESULT_INITIALIZED
    ) {
        kernel_panic(
            "Console-device initialization failed"
        );
    }

    if (
        registry.count != 1 ||
        device.device.identifier !=
            CONSOLE_DEVICE_IDENTIFIER ||
        device.device.kind !=
            DEVICE_KIND_VIRTUAL ||
        device.device.state !=
            DEVICE_STATE_ACTIVE ||
        device.device.reference_count != 2 ||
        device.character.device !=
            &device.device ||
        device.console !=
            &console ||
        !character_device_can_read(
            &device.character
        ) ||
        !character_device_can_write(
            &device.character
        )
    ) {
        kernel_panic(
            "Console-device published state is invalid"
        );
    }

    struct device *lookup =
        NULL;

    if (
        device_registry_lookup_name(
            &registry,
            "console",
            sizeof("console") - 1U,
            &lookup
        ) !=
            DEVICE_REGISTRY_LOOKUP_RESULT_FOUND ||
        lookup !=
            &device.device ||
        lookup->reference_count != 3 ||
        !device_release(
            lookup
        ) ||
        device.device.reference_count != 2
    ) {
        kernel_panic(
            "Console-device registry lookup contract failed"
        );
    }

    /*
     * A missing read capability is explicit. In particular, an empty input
     * queue is not represented as EOF by this bootstrap implementation.
     */
    uint8_t read_buffer =
        0xa5U;

    size_t bytes_read =
        SIZE_MAX;

    if (
        character_device_read(
            &device.character,
            &read_buffer,
            sizeof(read_buffer),
            &bytes_read
        ) !=
            CHARACTER_DEVICE_IO_RESULT_WOULD_BLOCK ||
        bytes_read !=
            SIZE_MAX ||
        read_buffer !=
            0xa5U
    ) {
        kernel_panic(
            "Console-device unexpectedly supported reads"
        );
    }

    /*
     * Feed normalized keyboard events and drain them into
     * this console's canonical line buffer.
     *
     * The global input queue is reset for test isolation.
     */
    input_init();

    struct input_event events[] = {
        {
            .type = INPUT_EVENT_KEY,
            .key = {
                .code = INPUT_KEY_H,
                .state = INPUT_KEY_PRESSED,
            },
        },
        {
            .type = INPUT_EVENT_KEY,
            .key = {
                .code = INPUT_KEY_I,
                .state = INPUT_KEY_PRESSED,
            },
        },
        {
            .type = INPUT_EVENT_KEY,
            .key = {
                .code = INPUT_KEY_ENTER,
                .state = INPUT_KEY_PRESSED,
            },
        },
    };

    for (size_t index = 0; index < 3U; ++index) {
        if (!input_event_submit(&events[index])) {
            kernel_panic(
                "Console normalized input submission failed"
            );
        }
    }

    if (
        console_device_drain_input(&device, 0U) != 0U ||
        input_event_pending() != 3U ||
        console_device_drain_input(&device, 2U) != 2U ||
        input_event_pending() != 1U ||
        console_device_drain_input(&device, 2U) != 1U ||
        input_event_pending() != 0U ||
        device.input_consumer.has_pending_event
    ) {
        kernel_panic(
            "Console device input drain contract failed"
        );
    }

    uint8_t normalized_line[4] = {0};
    size_t normalized_length = SIZE_MAX;

    if (
        character_device_read(
            &device.character,
            normalized_line,
            sizeof(normalized_line),
            &normalized_length
        ) != CHARACTER_DEVICE_IO_RESULT_SUCCESS ||
        normalized_length != 3U ||
        normalized_line[0] != 'h' ||
        normalized_line[1] != 'i' ||
        normalized_line[2] != '\n'
    ) {
        kernel_panic(
            "Console normalized input line mismatch"
        );
    }

    /*
     * The accepted input was echoed as "hi"
     * followed by a visual newline.
     */
    if (
        console.cursor_x != 0U ||
        console.cursor_y != 9U ||
        !console_device_test_glyph_visible(
            pixels, 0U, 0U
        ) ||
        !console_device_test_glyph_visible(
            pixels, 9U, 0U
        )
    ) {
        kernel_panic(
            "Console accepted input was not echoed"
        );
    }

    static const uint8_t prompt[] = {
        '>',
        ' ',
    };

    size_t prompt_written = 0U;

    if (
        character_device_write(
            &device.character,
            prompt,
            sizeof(prompt),
            &prompt_written
        ) != CHARACTER_DEVICE_IO_RESULT_SUCCESS ||
        prompt_written != sizeof(prompt) ||
        console.cursor_x != 18U ||
        console.cursor_y != 9U
    ) {
        kernel_panic(
            "Console echo prompt setup failed"
        );
    }

    /*
     * Backspace on an empty editing buffer must
     * not erase any part of the prompt.
     */
    console_device_test_readability_key(
        &device, INPUT_KEY_BACKSPACE
    );

    console_device_test_readability_key(
        &device, INPUT_KEY_A
    );

    console_device_test_readability_key(
        &device, INPUT_KEY_B
    );

    console_device_test_readability_key(
        &device, INPUT_KEY_BACKSPACE
    );

    console_device_test_readability_key(
        &device, INPUT_KEY_ENTER
    );

    if (
        console.cursor_x != 0U ||
        console.cursor_y != 18U ||
        !console_device_test_glyph_visible(
            pixels, 0U, 9U
        ) ||
        !console_device_test_glyph_visible(
            pixels, 18U, 9U
        ) ||
        console_device_test_glyph_visible(
            pixels, 27U, 9U
        )
    ) {
        kernel_panic(
            "Console visual Backspace or prompt protection failed"
        );
    }

    uint8_t edited_line[4] = {0};
    size_t edited_length = 0U;

    if (
        character_device_read(
            &device.character,
            edited_line,
            sizeof(edited_line),
            &edited_length
        ) != CHARACTER_DEVICE_IO_RESULT_SUCCESS ||
        edited_length != 2U ||
        edited_line[0] != 'a' ||
        edited_line[1] != '\n'
    ) {
        kernel_panic(
            "Console visual editing changed canonical input"
        );
    }

    /*
     * Embedded NUL proves that character-device writes are length-delimited
     * byte streams rather than C strings.
     *
     * At scale 1 each ordinary byte advances the cursor by 8 glyph pixels plus
     * one spacing pixel, regardless of whether a glyph exists.
     */
    static const uint8_t payload[] = {
        'A',
        0,
        'B',
    };

    size_t bytes_written =
        SIZE_MAX;

    if (
        character_device_write(
            &device.character,
            payload,
            sizeof(payload),
            &bytes_written
        ) !=
            CHARACTER_DEVICE_IO_RESULT_SUCCESS ||
        bytes_written != sizeof(payload) ||
        console.cursor_x != 27 ||
        console.cursor_y != 18U
    ) {
        kernel_panic(
            "Console-device byte-stream write failed"
        );
    }

    /*
     * Supported zero-length writes succeed in the generic character layer
     * without invoking the backend or requiring a buffer.
     */
    uint64_t cursor_x_before =
        console.cursor_x;

    uint64_t cursor_y_before =
        console.cursor_y;

    bytes_written =
        SIZE_MAX;

    if (
        character_device_write(
            &device.character,
            NULL,
            0,
            &bytes_written
        ) !=
            CHARACTER_DEVICE_IO_RESULT_SUCCESS ||
        bytes_written != 0 ||
        console.cursor_x !=
            cursor_x_before ||
        console.cursor_y !=
            cursor_y_before
    ) {
        kernel_panic(
            "Console-device zero-length write contract failed"
        );
    }

    if (
        console_line_buffer_submit(&device.input, 'h') !=
            CONSOLE_LINE_INPUT_ACCEPTED ||
        console_line_buffer_submit(&device.input, 'i') !=
            CONSOLE_LINE_INPUT_ACCEPTED ||
        console_line_buffer_submit(&device.input, '\n') !=
            CONSOLE_LINE_INPUT_ACCEPTED
    ) {
        kernel_panic("Console input fixture submission failed");
    }

    uint8_t first[2] = {0};
    size_t first_read = SIZE_MAX;

    if (
        character_device_read(
            &device.character,
            first,
            sizeof(first),
            &first_read
        ) != CHARACTER_DEVICE_IO_RESULT_SUCCESS ||
        first_read != 2U ||
        first[0] != 'h' ||
        first[1] != 'i'
    ) {
        kernel_panic("Console partial line read failed");
    }

    uint8_t second[2] = {0};
    size_t second_read = SIZE_MAX;

    if (
        character_device_read(
            &device.character,
            second,
            sizeof(second),
            &second_read
        ) != CHARACTER_DEVICE_IO_RESULT_SUCCESS ||
        second_read != 1U ||
        second[0] != '\n'
    ) {
        kernel_panic("Console remaining line read failed");
    }

    uint8_t empty_buffer = 0xa5U;
    size_t empty_read = SIZE_MAX;

    if (
        character_device_read(
            &device.character,
            &empty_buffer,
            1U,
            &empty_read
        ) != CHARACTER_DEVICE_IO_RESULT_WOULD_BLOCK ||
        empty_buffer != 0xa5U ||
        empty_read != SIZE_MAX
    ) {
        kernel_panic("Console consumed line remained readable");
    }

    console_device_test_unregister(
        &registry,
        &device.device
    );

    if (
        registry.count != 0 ||
        device.device.reference_count != 0
    ) {
        kernel_panic(
            "Console-device cleanup leaked ownership"
        );
    }
}

static void console_device_test_deferred_input(void)
{
    /*
     * Use an independent device fixture so deferred input tests
     * do not inherit previously consumed lines or events.
     */
    static uint32_t pixels[
        CONSOLE_DEVICE_TEST_WIDTH *
        CONSOLE_DEVICE_TEST_HEIGHT
    ];

    struct framebuffer framebuffer = {
        .address = pixels,
        .width = CONSOLE_DEVICE_TEST_WIDTH,
        .height = CONSOLE_DEVICE_TEST_HEIGHT,
        .pitch =
            CONSOLE_DEVICE_TEST_WIDTH * sizeof(uint32_t),
        .bpp = 32,
        .red_mask_size = 8,
        .red_mask_shift = 16,
        .green_mask_size = 8,
        .green_mask_shift = 8,
        .blue_mask_size = 8,
        .blue_mask_shift = 0,
    };

    struct console console;

    console_init(
        &console,
        &framebuffer,
        0,
        0,
        0x00ffffffU,
        1
    );

    struct device_registry registry;

    if (!device_registry_initialize(&registry)) {
        kernel_panic(
            "Deferred console registry initialization failed"
        );
    }

    struct console_device device;

    if (
        console_device_initialize(
            &device,
            &console,
            &registry
        ) != CONSOLE_DEVICE_INITIALIZE_RESULT_INITIALIZED
    ) {
        kernel_panic(
            "Deferred console device initialization failed"
        );
    }

    /*
     * Reset the global normalized input queue.
     * No hardware events are involved in this fixture.
     */
    input_init();

    /*
     * A dispatch without a request must not execute work.
     */
    if (
        deferred_work_pending(&device.input_work) ||
        console_device_dispatch_input(&device, 1U) != 0U
    ) {
        kernel_panic(
            "Deferred console executed unrequested work"
        );
    }

    /*
     * Verify canonical readability transitions,
     * subscriber ownership and notification lifecycle.
     *
     * This test leaves the device input state empty,
     * preserving the original deferred-input fixture.
     */
    console_device_test_readability(&device);

    console_device_test_read_waiters(&device);

    console_device_test_eof_readiness(&device);

    console_device_test_eof_edit_boundary(&device);

    struct input_event events[] = {
        {
            .type = INPUT_EVENT_KEY,
            .key = {
                .code = INPUT_KEY_H,
                .state = INPUT_KEY_PRESSED,
            },
        },
        {
            .type = INPUT_EVENT_KEY,
            .key = {
                .code = INPUT_KEY_I,
                .state = INPUT_KEY_PRESSED,
            },
        },
        {
            .type = INPUT_EVENT_KEY,
            .key = {
                .code = INPUT_KEY_ENTER,
                .state = INPUT_KEY_PRESSED,
            },
        },
    };

    for (size_t index = 0; index < 3U; ++index) {
        if (!input_event_submit(&events[index])) {
            kernel_panic(
                "Deferred console input submission failed"
            );
        }
    }

    /*
     * Events alone do not execute deferred work.
     * The work item must first be requested.
     */
    if (
        input_event_pending() != 3U ||
        console_device_dispatch_input(&device, 1U) != 0U ||
        input_event_pending() != 3U
    ) {
        kernel_panic(
            "Deferred console processed events without request"
        );
    }

    /*
     * Repeated requests coalesce into one pending work item.
     */
    console_device_request_input(&device);
    console_device_request_input(&device);

    if (
        !deferred_work_pending(&device.input_work) ||
        console_device_dispatch_input(&device, 0U) != 0U ||
        !deferred_work_pending(&device.input_work) ||
        input_event_pending() != 3U
    ) {
        kernel_panic(
            "Deferred console request or zero-budget contract failed"
        );
    }

    /*
     * One handler invocation can drain up to the internal
     * input-event budget.
     */
    if (
        console_device_dispatch_input(&device, 1U) != 1U ||
        deferred_work_pending(&device.input_work) ||
        input_event_pending() != 0U ||
        device.input_consumer.has_pending_event
    ) {
        kernel_panic(
            "Deferred console input dispatch failed"
        );
    }

    uint8_t line[4] = {0};
    size_t bytes_read = SIZE_MAX;

    if (
        character_device_read(
            &device.character,
            line,
            sizeof(line),
            &bytes_read
        ) != CHARACTER_DEVICE_IO_RESULT_SUCCESS ||
        bytes_read != 3U ||
        line[0] != 'h' ||
        line[1] != 'i' ||
        line[2] != '\n'
    ) {
        kernel_panic(
            "Deferred console produced incorrect input line"
        );
    }

    /*
     * The completed line was consumed exactly once.
     */
    uint8_t empty = 0xa5U;
    bytes_read = SIZE_MAX;

    if (
        character_device_read(
            &device.character,
            &empty,
            sizeof(empty),
            &bytes_read
        ) != CHARACTER_DEVICE_IO_RESULT_WOULD_BLOCK ||
        empty != 0xa5U ||
        bytes_read != SIZE_MAX ||
        deferred_work_pending(&device.input_work) ||
        console_device_dispatch_input(&device, 1U) != 0U
    ) {
        kernel_panic(
            "Deferred console repeated consumed input"
        );
    }

    /*
     * Fill the completed-line queue to its exact capacity.
     * Each line contains one character followed by newline.
     */
    for (
        size_t index = 0;
        index < CONSOLE_LINE_QUEUE_CAPACITY;
        ++index
    ) {
        if (
            console_line_buffer_submit(
                &device.input,
                (char) ('a' + index)
            ) != CONSOLE_LINE_INPUT_ACCEPTED ||
            console_line_buffer_submit(
                &device.input,
                '\n'
            ) != CONSOLE_LINE_INPUT_ACCEPTED
        ) {
            kernel_panic(
                "Deferred console queue fill failed"
            );
        }
    }

    if (
        device.input.completed_count !=
            CONSOLE_LINE_QUEUE_CAPACITY
    ) {
        kernel_panic(
            "Deferred console queue capacity mismatch"
        );
    }

    /*
     * Submit one additional line through the normalized
     * input queue rather than directly into the line buffer.
     *
     * The first event must be retained when the completed
     * queue is full. Enter remains in the global input queue.
     */
    input_init();

    struct input_event overflow_events[] = {
        {
            .type = INPUT_EVENT_KEY,
            .key = {
                .code = INPUT_KEY_H,
                .state = INPUT_KEY_PRESSED,
            },
        },
        {
            .type = INPUT_EVENT_KEY,
            .key = {
                .code = INPUT_KEY_ENTER,
                .state = INPUT_KEY_PRESSED,
            },
        },
    };

    for (size_t index = 0; index < 2U; ++index) {
        if (!input_event_submit(&overflow_events[index])) {
            kernel_panic(
                "Deferred console overflow event submission failed"
            );
        }
    }

    console_device_request_input(&device);

    if (
        console_device_dispatch_input(&device, 1U) != 1U ||
        !device.input_consumer.has_pending_event ||
        input_event_pending() != 1U ||
        deferred_work_pending(&device.input_work) ||
        device.input.completed_count !=
            CONSOLE_LINE_QUEUE_CAPACITY
    ) {
        kernel_panic(
            "Deferred console failed to wait on full queue"
        );
    }

    /*
     * Reading only the first byte of the oldest line does
     * not release a completed-line queue entry.
     *
     * The read requests deferred processing, but another
     * dispatch must return to WAIT without losing the event.
     */
    uint8_t partial_byte = 0;
    size_t partial_length = SIZE_MAX;

    if (
        character_device_read(
            &device.character,
            &partial_byte,
            1U,
            &partial_length
        ) != CHARACTER_DEVICE_IO_RESULT_SUCCESS ||
        partial_byte != 'a' ||
        partial_length != 1U ||
        device.input.completed_count !=
            CONSOLE_LINE_QUEUE_CAPACITY ||
        !deferred_work_pending(&device.input_work)
    ) {
        kernel_panic(
            "Deferred console partial read contract failed"
        );
    }

    if (
        console_device_dispatch_input(&device, 1U) != 1U ||
        !device.input_consumer.has_pending_event ||
        input_event_pending() != 1U ||
        deferred_work_pending(&device.input_work)
    ) {
        kernel_panic(
            "Deferred console retried full queue incorrectly"
        );
    }

    /*
     * Consuming the newline completes the oldest line,
     * releases one queue entry, and requests deferred work.
     */
    uint8_t newline_byte = 0;
    size_t newline_length = SIZE_MAX;

    if (
        character_device_read(
            &device.character,
            &newline_byte,
            1U,
            &newline_length
        ) != CHARACTER_DEVICE_IO_RESULT_SUCCESS ||
        newline_byte != '\n' ||
        newline_length != 1U ||
        device.input.completed_count !=
            CONSOLE_LINE_QUEUE_CAPACITY - 1U ||
        !deferred_work_pending(&device.input_work)
    ) {
        kernel_panic(
            "Deferred console failed to release completed line"
        );
    }

    /*
     * The retained H event must be consumed before Enter.
     * Together they create the fifth line: "h\n".
     */
    if (
        console_device_dispatch_input(&device, 1U) != 1U ||
        device.input_consumer.has_pending_event ||
        input_event_pending() != 0U ||
        deferred_work_pending(&device.input_work) ||
        device.input.completed_count !=
            CONSOLE_LINE_QUEUE_CAPACITY
    ) {
        kernel_panic(
            "Deferred console failed to resume retained input"
        );
    }

    /*
     * Verify FIFO ordering. The remaining original lines
     * precede the newly completed fifth line.
     */
    for (
        size_t index = 1U;
        index < CONSOLE_LINE_QUEUE_CAPACITY;
        ++index
    ) {
        uint8_t completed_line[2] = {0};
        size_t completed_length = SIZE_MAX;

        if (
            character_device_read(
                &device.character,
                completed_line,
                sizeof(completed_line),
                &completed_length
            ) != CHARACTER_DEVICE_IO_RESULT_SUCCESS ||
            completed_length != 2U ||
            completed_line[0] != (uint8_t) ('a' + index) ||
            completed_line[1] != '\n'
        ) {
            kernel_panic(
                "Deferred console completed-line ordering failed"
            );
        }
    }

    uint8_t resumed_line[2] = {0};
    size_t resumed_length = SIZE_MAX;

    if (
        character_device_read(
            &device.character,
            resumed_line,
            sizeof(resumed_line),
            &resumed_length
        ) != CHARACTER_DEVICE_IO_RESULT_SUCCESS ||
        resumed_length != 2U ||
        resumed_line[0] != 'h' ||
        resumed_line[1] != '\n' ||
        device.input.completed_count != 0U
    ) {
        kernel_panic(
            "Deferred console retained line was lost"
        );
    }

    uint8_t exhausted_byte = 0xa5U;
    size_t exhausted_length = SIZE_MAX;

    if (
        character_device_read(
            &device.character,
            &exhausted_byte,
            1U,
            &exhausted_length
        ) != CHARACTER_DEVICE_IO_RESULT_WOULD_BLOCK ||
        exhausted_byte != 0xa5U ||
        exhausted_length != SIZE_MAX ||
        deferred_work_pending(&device.input_work) ||
        console_device_dispatch_input(&device, 1U) != 0U
    ) {
        kernel_panic(
            "Deferred console overflow recovery left stale input"
        );
    }

    /*
     * Verify automatic deferred-work notification.
     *
     * Publishing a normalized event must request console work
     * without consuming the event or invoking the worker.
     */
    if (
        input_event_pending() != 0U ||
        deferred_work_pending(&device.input_work) ||
        device.input.completed_count != 0U ||
        console_device_test_notification_target != NULL
    ) {
        kernel_panic(
            "Console notification fixture has dirty initial state"
        );
    }

    console_device_test_notification_target = &device;

    if (
        input_event_notification_handler_set(
            console_device_test_input_notification
        ) != NULL
    ) {
        kernel_panic(
            "Console notification handler already registered"
        );
    }

    struct input_event notified_events[] = {
        {
            .type = INPUT_EVENT_KEY,
            .key = {
                .code = INPUT_KEY_H,
                .state = INPUT_KEY_PRESSED,
            },
        },
        {
            .type = INPUT_EVENT_KEY,
            .key = {
                .code = INPUT_KEY_ENTER,
                .state = INPUT_KEY_PRESSED,
            },
        },
    };

    for (size_t index = 0; index < 2U; ++index) {
        if (!input_event_submit(&notified_events[index])) {
            kernel_panic(
                "Console notified event submission failed"
            );
        }
    }

    /*
     * Notification coalesces requests but does not consume
     * the input queue or produce a completed line.
     */
    if (
        input_event_pending() != 2U ||
        !deferred_work_pending(&device.input_work) ||
        device.input.completed_count != 0U ||
        device.input_consumer.has_pending_event
    ) {
        kernel_panic(
            "Console notification executed work prematurely"
        );
    }

    /*
     * Only explicit dispatch may process the queued events.
     */
    if (
        console_device_dispatch_input(&device, 1U) != 1U ||
        input_event_pending() != 0U ||
        deferred_work_pending(&device.input_work) ||
        device.input.completed_count != 1U
    ) {
        kernel_panic(
            "Console notification dispatch contract failed"
        );
    }

    uint8_t notified_line[2] = {0};
    size_t notified_length = SIZE_MAX;

    if (
        character_device_read(
            &device.character,
            notified_line,
            sizeof(notified_line),
            &notified_length
        ) != CHARACTER_DEVICE_IO_RESULT_SUCCESS ||
        notified_length != 2U ||
        notified_line[0] != 'h' ||
        notified_line[1] != '\n'
    ) {
        kernel_panic(
            "Console notified input line mismatch"
        );
    }

    /*
     * Unregister before retiring the borrowed device.
     * The global input subsystem must not retain a pointer
     * to a fixture that is about to leave scope.
     */
    if (
        input_event_notification_handler_set(NULL) !=
            console_device_test_input_notification
    ) {
        kernel_panic(
            "Console notification handler cleanup failed"
        );
    }

    console_device_test_notification_target = NULL;

    /*
     * The global notification handler has been removed,
     * and this fixture still owns a live console device.
     */
    console_device_test_kernel_worker(&device);

    console_device_test_unregister(
        &registry,
        &device.device
    );

    if (
        registry.count != 0 ||
        device.device.reference_count != 0
    ) {
        kernel_panic(
            "Deferred console device cleanup leaked ownership"
        );
    }
}

static void console_device_test_kernel_worker(
    struct console_device *device)
{
    /*
     * Static lifetime avoids allocating the worker's
     * execution stack on the kernel test runner stack.
     */
    static struct console_input_worker worker;

    struct console_worker_observation observation = {
        .device = device,
    };

    struct cpu_local cpu = {0};
    struct kernel_switch_context supervisor = {0};

    uint32_t apic_id;

    if (
        device == NULL ||
        !arch_cpu_apic_id(&apic_id) ||
        !cpu_local_init(&cpu, 0U, apic_id) ||
        !console_input_worker_initialize(
            &worker,
            device,
            &supervisor
        )
    ) {
        kernel_panic(
            "Console worker fixture initialization failed"
        );
    }

    if (!console_input_worker_observer_set(
        &worker,
        console_device_test_worker_observer,
        &observation
    )) {
            kernel_panic(
                "Console worker observer registration failed"
            );
    }

    uint64_t original_rflags;

    __asm__ volatile (
        "pushfq\n\t"
        "popq %0"
        : "=r"(original_rflags)
        :
        : "memory"
    );

    interrupts_disable();

    /*
     * First dispatch without pending work.
     *
     * The worker must suspend and remain available
     * for subsequent notifications.
     */
    if (
        !kernel_context_dispatch(
            &cpu,
            &worker.context
        ) ||
        worker.context.state != KERNEL_CONTEXT_WAITING ||
        kernel_context_owner(&worker.context) !=
            KERNEL_CONTEXT_NO_CPU
    ) {
        kernel_panic(
            "Console worker initial suspension failed"
        );
    }

    /*
     * Publish more events than one console work
     * invocation can process.
     *
     * Notification is deliberately not installed yet.
     */
    struct input_event letter = {
        .type = INPUT_EVENT_KEY,
        .key = {
            .code = INPUT_KEY_H,
            .state = INPUT_KEY_PRESSED,
        },
    };

    struct input_event enter = {
        .type = INPUT_EVENT_KEY,
        .key = {
            .code = INPUT_KEY_ENTER,
            .state = INPUT_KEY_PRESSED,
        },
    };

    for (size_t index = 0; index < 17U; ++index) {
        if (!input_event_submit(&letter)) {
            kernel_panic(
                "Console worker letter submission failed"
            );
        }
    }

    if (!input_event_submit(&enter)) {
        kernel_panic(
            "Console worker Enter submission failed"
        );
    }

    console_device_request_input(device);

    if (
        input_event_pending() != 18U ||
        !deferred_work_pending(&device->input_work) ||
        worker.context.state != KERNEL_CONTEXT_WAITING
    ) {
        kernel_panic(
            "Console worker lost request before registration"
        );
    }

    /*
     * Late notification registration must wake the
     * suspended worker without processing events.
     */
    if (
        !console_input_worker_notifications_enable(
            &worker
        ) ||
        worker.context.state != KERNEL_CONTEXT_READY ||
        input_event_pending() != 18U
    ) {
        kernel_panic(
            "Console worker late notification failed"
        );
    }

    /*
     * The first actual work dispatch processes 16
     * events and publishes DEFERRED_WORK_MORE.
     *
     * That notification occurs while the worker is
     * RUNNING and must rearm it after yielding.
     */
    if (
        !kernel_context_dispatch(
            &cpu,
            &worker.context
        ) ||
        worker.context.state != KERNEL_CONTEXT_READY ||
        worker.context.reschedule_requested ||
        input_event_pending() != 2U ||
        !deferred_work_pending(&device->input_work) ||
        device->input.completed_count != 0U
    ) {
        kernel_panic(
            "Console worker bounded continuation failed"
        );
    }

    /*
     * Second work dispatch consumes the remaining
     * events and returns the worker to WAITING.
     */
    if (
        !kernel_context_dispatch(
            &cpu,
            &worker.context
        ) ||
        worker.context.state != KERNEL_CONTEXT_WAITING ||
        worker.context.reschedule_requested ||
        input_event_pending() != 0U ||
        deferred_work_pending(&device->input_work) ||
        device->input.completed_count != 1U ||
        kernel_context_owner(&worker.context) !=
            KERNEL_CONTEXT_NO_CPU
    ) {
        kernel_panic(
            "Console worker continuation completion failed"
        );
    }

    uint8_t line[20] = {0};
    size_t bytes_read = SIZE_MAX;

    if (
        character_device_read(
            &device->character,
            line,
            sizeof(line),
            &bytes_read
        ) != CHARACTER_DEVICE_IO_RESULT_SUCCESS ||
        bytes_read != 18U ||
        line[17] != '\n'
    ) {
        kernel_panic(
            "Console worker produced incorrect canonical line"
        );
    }

    for (size_t index = 0; index < 17U; ++index) {
        if (line[index] != 'h') {
            kernel_panic(
                "Console worker corrupted canonical input"
            );
        }
    }

    /*
     * A new request must wake the same suspended
     * worker, not create another execution context.
     */
    if (
        !input_event_submit(&letter) ||
        !input_event_submit(&enter)
    ) {
        kernel_panic(
            "Console worker repeated input submission failed"
        );
    }

    console_device_request_input(device);

    if (
        worker.context.state != KERNEL_CONTEXT_READY ||
        !kernel_context_dispatch(
            &cpu,
            &worker.context
        ) ||
        worker.context.state != KERNEL_CONTEXT_WAITING ||
        device->input.completed_count != 1U
    ) {
        kernel_panic(
            "Console worker repeated wake failed"
        );
    }

    uint8_t repeated_line[2] = {0};
    size_t repeated_length = SIZE_MAX;

    if (
        character_device_read(
            &device->character,
            repeated_line,
            sizeof(repeated_line),
            &repeated_length
        ) != CHARACTER_DEVICE_IO_RESULT_SUCCESS ||
        repeated_length != 2U ||
        repeated_line[0] != 'h' ||
        repeated_line[1] != '\n'
    ) {
        kernel_panic(
            "Console worker repeated input mismatch"
        );
    }

    if (
        observation.calls != 4U ||

        observation.before[0] != 0U ||
        observation.dispatched[0] != 0U ||
        observation.after[0] != 0U ||

        observation.before[1] != 18U ||
        observation.dispatched[1] != 1U ||
        observation.after[1] != 2U ||

        observation.before[2] != 2U ||
        observation.dispatched[2] != 1U ||
        observation.after[2] != 0U ||

        observation.before[3] != 2U ||
        observation.dispatched[3] != 1U ||
        observation.after[3] != 0U
    ) {
            kernel_panic(
                "Console worker dispatch observation mismatch"
            );
    }

    /*
     * The fixture must not leave a callback borrowing
     * a console device that will soon be retired.
     */
    if (
        !console_input_worker_notifications_disable(
            &worker
        ) ||
        device->input_work.notification != NULL ||
        worker.context.state != KERNEL_CONTEXT_WAITING ||
        kernel_context_owner(&worker.context) !=
            KERNEL_CONTEXT_NO_CPU
    ) {
        kernel_panic(
            "Console worker fixture cleanup failed"
        );
    }

    if (
        (original_rflags & (1ULL << 9)) != 0
    ) {
        interrupts_enable();
    }

    diagnostics_write(
        "[console] Persistent input worker tests passed\n"
    );
}

static void console_device_test_registration_rollback(void)
{
    static uint32_t pixels[
        CONSOLE_DEVICE_TEST_WIDTH *
        CONSOLE_DEVICE_TEST_HEIGHT
    ];

    struct framebuffer framebuffer = {
        .address =
            pixels,
        .width =
            CONSOLE_DEVICE_TEST_WIDTH,
        .height =
            CONSOLE_DEVICE_TEST_HEIGHT,
        .pitch =
            CONSOLE_DEVICE_TEST_WIDTH *
            sizeof(uint32_t),
        .bpp =
            32,
        .red_mask_size =
            8,
        .red_mask_shift =
            16,
        .green_mask_size =
            8,
        .green_mask_shift =
            8,
        .blue_mask_size =
            8,
        .blue_mask_shift =
            0,
    };

    struct console console;

    console_init(
        &console,
        &framebuffer,
        0,
        0,
        0x00ffffffU,
        1
    );

    struct device_registry registry;

    if (!device_registry_initialize(
        &registry
    )) {
        kernel_panic(
            "Console rollback registry initialization failed"
        );
    }

    /*
     * Reserve the canonical internal name. The console initializer must fail
     * publication without mutating or withdrawing the preexisting device.
     */
    struct device preexisting;

    if (
        !device_initialize(
            &preexisting,
            6501,
            "console",
            sizeof("console") - 1U,
            DEVICE_KIND_VIRTUAL,
            NULL,
            NULL
        ) ||
        device_registry_register(
            &registry,
            &preexisting
        ) !=
            DEVICE_REGISTRY_REGISTER_RESULT_REGISTERED
    ) {
        kernel_panic(
            "Console rollback preexisting device setup failed"
        );
    }

    uint64_t generation_before =
        registry.generation;

    struct console_device console_device;

    if (
        console_device_initialize(
            &console_device,
            &console,
            &registry
        ) !=
            CONSOLE_DEVICE_INITIALIZE_RESULT_REGISTRY_REJECTED ||
        registry.count != 1 ||
        registry.devices[0] !=
            &preexisting ||
        registry.generation !=
            generation_before ||
        preexisting.state !=
            DEVICE_STATE_ACTIVE ||
        preexisting.reference_count != 2 ||
        console_device.device.state !=
            DEVICE_STATE_GONE ||
        console_device.device.reference_count != 0
    ) {
        kernel_panic(
            "Console-device registration rollback failed"
        );
    }

    console_device_test_unregister(
        &registry,
        &preexisting
    );

    if (registry.count != 0) {
        kernel_panic(
            "Console rollback fixture cleanup failed"
        );
    }
}
