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
#include "../console/console_input_worker.h"
#include "../core/cpu/cpu_local.h"
#include "../core/execution/kernel_dispatch.h"
#include "../console/console.h"
#include "../console/console_device.h"
#include "../core/device/character_device.h"
#include "../core/device/device.h"
#include "../core/device/registry.h"
#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../drivers/framebuffer.h"
#include "../input/input.h"

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
        bytes_written !=
            sizeof(payload) ||
        console.cursor_x !=
            27 ||
        console.cursor_y !=
            0
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
