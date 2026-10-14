// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file console_device.c
 * @brief System console character-device implementation.
 */

#include "console_device.h"

#include "../input/input.h"

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

// Static local variables
static const struct character_device_operations
    console_device_character_operations = {
        .read = console_device_read,
        .write = console_device_write,
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
     * Notify only when input transitions from
     * unavailable to readable.
     *
     * Processing ordinary keystrokes does not make
     * a canonical line readable until Enter arrives.
     */
    if (
        completed_before == 0U &&
        device->input.completed_count != 0U &&
        device->readable_notification != NULL
    ) {
        device->readable_notification(
            device->readable_notification_context,
            device
        );
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
