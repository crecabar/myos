// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file console_device.c
 * @brief System console character-device implementation.
 */

#include <myos/kernel/console/console_device.h>

#include <myos/kernel/input/input.h>

#include <stddef.h>
#include <stdint.h>

#define CONSOLE_INPUT_WORK_EVENT_BUDGET 16U

// Private functions and helpers declarations
static struct console_device *console_device_from_character(
    struct character_device *character
);

static bool console_device_retire_unpublished(
    struct console_device *console_device
);

static enum character_device_io_result console_device_write(
    struct character_device *character,
    const void *buffer,
    size_t size,
    size_t *bytes_written
);

static enum character_device_io_result console_device_read(
    struct character_device *character,
    void *buffer,
    size_t size,
    size_t *bytes_read
);

static enum deferred_work_result console_device_input_work(
    void *context);

static bool console_device_read_ready(
    struct character_device *character,
    bool *ready
);

static void console_device_echo_accepted(
    void *context,
    char character
);

static void console_device_echo_control(
    void *context
);

static void console_device_echo_rebase(
    struct console_device *device
);

// Static local variables
static const struct character_device_operations
    console_device_character_operations = {
        .read = console_device_read,
        .write = console_device_write,
        .read_ready = console_device_read_ready,
    };

// Public functions implementations
enum console_device_initialize_result console_device_initialize(
    struct console_device *console_device,
    struct console *console,
    struct device_registry *registry)
{
    if (
        console_device == NULL ||
        console == NULL ||
        console->framebuffer == NULL ||
        registry == NULL
    ) {
        return
            CONSOLE_DEVICE_INITIALIZE_RESULT_INVALID_ARGUMENT;
    }

    if (!device_initialize(
        &console_device->device,
        CONSOLE_DEVICE_IDENTIFIER,
        "console",
        sizeof("console") - 1U,
        DEVICE_KIND_VIRTUAL,
        NULL,
        NULL
    )) {
        return
            CONSOLE_DEVICE_INITIALIZE_RESULT_INVALID_ARGUMENT;
    }

    console_device->console = console;
    console_line_buffer_init(&console_device->input);
    console_input_consumer_init(&console_device->input_consumer);
    console_device->echo_start_x = console->cursor_x;
    console_device->echo_start_y = console->cursor_y;
    console_device->echo_scroll_count = console->scroll_count;
    console_device->echo_visible_characters = 0U;
    console_device->readable_notification = NULL;
    console_device->readable_notification_context = NULL;

    if (!character_device_initialize(
        &console_device->character,
        &console_device->device,
        &console_device_character_operations,
        console_device
    )) {
        console_device->console =
            NULL;

        if (!console_device_retire_unpublished(
            console_device
        )) {
            return
                CONSOLE_DEVICE_INITIALIZE_RESULT_ROLLBACK_FAILED;
        }

        return
            CONSOLE_DEVICE_INITIALIZE_RESULT_INVALID_ARGUMENT;
    }

    if (!deferred_work_init(
        &console_device->input_work,
        console_device_input_work,
        console_device
    )) {
        console_device->console = NULL;

        if (!console_device_retire_unpublished(console_device)) {
            return CONSOLE_DEVICE_INITIALIZE_RESULT_ROLLBACK_FAILED;
        }

        return CONSOLE_DEVICE_INITIALIZE_RESULT_INVALID_ARGUMENT;
    }

    enum device_registry_register_result register_result =
        device_registry_register(
            registry,
            &console_device->device
        );

    if (
        register_result !=
            DEVICE_REGISTRY_REGISTER_RESULT_REGISTERED
    ) {
        if (!console_device_retire_unpublished(
            console_device
        )) {
            return
                CONSOLE_DEVICE_INITIALIZE_RESULT_ROLLBACK_FAILED;
        }

        console_device->console =
            NULL;

        return
            CONSOLE_DEVICE_INITIALIZE_RESULT_REGISTRY_REJECTED;
    }

    console_input_consumer_accepted_observer_set(
        &console_device->input_consumer,
        console_device_echo_accepted,
        console_device
    );

    console_input_consumer_control_observer_set(
        &console_device->input_consumer,
        console_device_echo_control,
        console_device
    );

    return
        CONSOLE_DEVICE_INITIALIZE_RESULT_INITIALIZED;
}

size_t console_device_drain_input(
    struct console_device *device,
    size_t budget)
{
    if (
        device == NULL ||
        console_device_from_character(
            &device->character
        ) != device
    ) {
        return 0;
    }

    size_t completed_before =
        device->input.completed_count;

    size_t processed =
        console_input_consumer_drain(
            &device->input_consumer,
            &device->input,
            budget
        );

    /*
     * Notify only on the transition from no completed
     * canonical lines to at least one completed line.
     *
     * Both notification mechanisms observe the same
     * transition without consuming input:
     *
     * - character-device waiters receive a one-shot
     *   broadcast;
     * - the existing console observer retains its
     *   original notification contract.
     */
    if (
        completed_before == 0U &&
        device->input.completed_count != 0U
    ) {
        (void) character_device_read_wait_notify(
            &device->character
        );

        if (device->readable_notification != NULL) {
            device->readable_notification(
                device->readable_notification_context,
                device
            );
        }
    }

    return processed;
}

void console_device_request_input(
    struct console_device *device)
{
    if (
        device != NULL &&
        console_device_from_character(
            &device->character
        ) == device
    ) {
        deferred_work_request(&device->input_work);
    }
}

size_t console_device_dispatch_input(
    struct console_device *device,
    size_t budget)
{
    if (
        device == NULL ||
        console_device_from_character(
            &device->character
        ) != device
    ) {
        return 0;
    }

    return deferred_work_dispatch(
        &device->input_work,
        budget
    );
}

// Private functions and helpers implementations
static struct console_device *console_device_from_character(
    struct character_device *character)
{
    if (
        character == NULL ||
        character->private_data == NULL
    ) {
        return NULL;
    }

    struct console_device *console_device =
        character->private_data;

    if (
        &console_device->character !=
            character ||
        character->device !=
            &console_device->device ||
        console_device->console == NULL ||
        console_device->console->framebuffer == NULL ||
        console_device->device.reference_count == 0 ||
        console_device->device.state !=
            DEVICE_STATE_ACTIVE
    ) {
        return NULL;
    }

    return
        console_device;
}

static bool console_device_retire_unpublished(
    struct console_device *console_device)
{
    if (
        console_device == NULL ||
        console_device->device.reference_count != 1 ||
        console_device->device.state !=
            DEVICE_STATE_ACTIVE
    ) {
        return false;
    }

    return
        device_begin_removal(
            &console_device->device
        ) &&
        device_finish_removal(
            &console_device->device
        ) &&
        device_release(
            &console_device->device
        );
}

static enum character_device_io_result console_device_read(
    struct character_device *character,
    void *buffer,
    size_t size,
    size_t *bytes_read)
{
    struct console_device *device =
        console_device_from_character(character);

    if (
        device == NULL ||
        bytes_read == NULL ||
        (size != 0 && buffer == NULL)
    ) {
        return CHARACTER_DEVICE_IO_RESULT_INVALID_ARGUMENT;
    }

    /*
     * Normally handled by the generic character-device layer.
     */
    if (size == 0) {
        *bytes_read = 0;
        return CHARACTER_DEVICE_IO_RESULT_SUCCESS;
    }

    size_t transferred = 0;

    if (!console_line_buffer_read(
        &device->input,
        buffer,
        size,
        &transferred
    )) {
        /*
         * No completed line is available.
         * This is not EOF and must not become a successful
         * zero-byte read.
         */
        return CHARACTER_DEVICE_IO_RESULT_WOULD_BLOCK;
    }

    *bytes_read = transferred;

    if (device->input_consumer.has_pending_event) {
        deferred_work_request(&device->input_work);
    }

    return CHARACTER_DEVICE_IO_RESULT_SUCCESS;
}

static bool console_device_read_ready(
    struct character_device *character,
    bool *ready)
{
    struct console_device *device =
        console_device_from_character(character);

    if (
        device == NULL ||
        ready == NULL
    ) {
        return false;
    }

    /*
     * A canonical line becomes readable only
     * after it has been completed.
     *
     * This query must never consume input.
     */
    *ready =
        device->input.completed_count != 0U;

    return true;
}

static void console_device_echo_control(
    void *context)
{
    struct console_device *device = context;

    if (
        device == NULL ||
        device->console == NULL
    ) {
        return;
    }

    /*
     * Ctrl+D completes the current canonical
     * editing segment without displaying a
     * character or advancing the cursor.
     *
     * Previously delivered characters must not
     * remain editable by a later Backspace.
     */
    device->echo_visible_characters = 0U;

    device->echo_start_x =
        device->console->cursor_x;

    device->echo_start_y =
        device->console->cursor_y;

    device->echo_scroll_count =
        device->console->scroll_count;
}

static void console_device_echo_rebase(
    struct console_device *device)
{
    struct console *console = device->console;

    uint64_t scrolls =
        console->scroll_count -
        device->echo_scroll_count;

    if (scrolls == 0U) {
        return;
    }

    uint64_t row_height =
        console_row_height(console);

    if (
        row_height == 0U ||
        device->echo_start_y <
            console->origin_y
    ) {
        device->echo_visible_characters = 0U;
        device->echo_scroll_count =
            console->scroll_count;
        return;
    }

    uint64_t preceding_rows =
        (device->echo_start_y -
            console->origin_y) /
        row_height;

    if (scrolls > preceding_rows) {
        /*
         * The original editing boundary has
         * scrolled off the visible framebuffer.
         *
         * Never permit erasure outside the screen.
         */
        device->echo_start_x =
            console->origin_x;

        device->echo_start_y =
            console->origin_y;
    } else {
        device->echo_start_y -=
            scrolls * row_height;
    }

    device->echo_scroll_count =
        console->scroll_count;
}

static void console_device_echo_accepted(
    void *context,
    char character)
{
    struct console_device *device = context;

    if (
        device == NULL ||
        device->console == NULL
    ) {
        return;
    }

    struct console *console = device->console;

    if (character == '\n') {
        console_put_char(console, '\n');

        /*
         * A completed canonical line no longer
         * has any editable visible characters.
         */
        device->echo_visible_characters = 0U;
        device->echo_scroll_count =
            console->scroll_count;
        return;
    }

    if (character == '\b') {
        if (device->echo_visible_characters == 0U) {
            return;
        }

        console_device_echo_rebase(device);

        if (
            console_erase_previous(
                console,
                device->echo_start_x,
                device->echo_start_y
            )
        ) {
            --device->echo_visible_characters;
        } else {
            /*
             * The previous visible region is no
             * longer available. Canonical editing
             * still continues independently.
             */
            device->echo_visible_characters = 0U;
        }

        return;
    }

    /*
     * Establish the editable boundary immediately
     * before displaying the first character of
     * a visible editing segment.
     */
    if (device->echo_visible_characters == 0U) {
        device->echo_start_x =
            console->cursor_x;

        device->echo_start_y =
            console->cursor_y;

        device->echo_scroll_count =
            console->scroll_count;
    }

    console_put_char(console, character);

    ++device->echo_visible_characters;

    console_device_echo_rebase(device);
}

static enum character_device_io_result console_device_write(
    struct character_device *character,
    const void *buffer,
    size_t size,
    size_t *bytes_written)
{
    struct console_device *console_device =
        console_device_from_character(
            character
        );

    if (
        console_device == NULL ||
        bytes_written == NULL ||
        (
            size != 0 &&
            buffer == NULL
        )
    ) {
        return
            CHARACTER_DEVICE_IO_RESULT_INVALID_ARGUMENT;
    }

    /*
     * A nonempty external write may insert text
     * between the characters of an editing line.
     *
     * Invalidate the current visible undo region
     * so Backspace cannot erase another producer's
     * output.
     *
     * Canonical input remains untouched.
     */
    if (size != 0U) {
        console_device->echo_visible_characters = 0U;
    }

    /*
     * Character-device writes are byte streams, not C strings. In
     * particular, an embedded NUL must not terminate the transfer.
     */
    const uint8_t *bytes =
        buffer;

    for (
        size_t index = 0;
        index < size;
        ++index
    ) {
        console_put_char(
            console_device->console,
            (char) bytes[index]
        );
    }

    *bytes_written =
        size;

    return
        CHARACTER_DEVICE_IO_RESULT_SUCCESS;
}

static enum deferred_work_result console_device_input_work(
    void *context)
{
    struct console_device *device = context;

    size_t processed = console_device_drain_input(
        device,
        CONSOLE_INPUT_WORK_EVENT_BUDGET
    );

    if (
        device->input_consumer.has_pending_event
    ) {
        /*
         * The canonical line queue cannot accept this event.
         * A subsequent read must request processing again.
         */
        return DEFERRED_WORK_WAIT;
    }

    if (
        input_event_pending() != 0
    ) {
        return DEFERRED_WORK_MORE;
    }

    (void) processed;

    return DEFERRED_WORK_COMPLETED;
}

bool console_device_readable_notification_set(
    struct console_device *device,
    console_device_readable_notification_fn notification,
    void *context)
{
    if (
        device == NULL ||
        console_device_from_character(
            &device->character
        ) != device
    ) {
        return false;
    }

    if (
        notification != NULL &&
        device->readable_notification != NULL
    ) {
        return false;
    }

    device->readable_notification = notification;

    device->readable_notification_context =
        notification != NULL ? context : NULL;

    /*
     * Do not lose readability that existed before
     * notification registration.
     */
    if (
        notification != NULL &&
        device->input.completed_count != 0U
    ) {
        notification(context, device);
    }

    return true;
}
