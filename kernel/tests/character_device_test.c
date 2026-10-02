// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file character_device_test.c
 * @brief Character-device capability and I/O contract regression tests.
 */

#include "character_device_test.h"

#include "../core/device/character_device.h"
#include "../core/device/device.h"
#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct character_device_test_state {
    enum character_device_io_result read_result;
    enum character_device_io_result write_result;

    size_t read_transfer;
    size_t write_transfer;

    size_t read_calls;
    size_t write_calls;

    size_t last_write_size;
    uint8_t last_write_first_byte;

    uint8_t read_fill;
};

// Private functions and helpers declarations
static enum character_device_io_result character_device_test_read(
    struct character_device *character,
    void *buffer,
    size_t size,
    size_t *bytes_read
);

static enum character_device_io_result character_device_test_write(
    struct character_device *character,
    const void *buffer,
    size_t size,
    size_t *bytes_written
);

static void character_device_test_remove_generic(
    struct device *device
);

static void character_device_test_initialization(void);

static void character_device_test_invalid_initialization(void);

static void character_device_test_io_dispatch(void);

static void character_device_test_missing_capability(void);

static void character_device_test_backend_failure(void);

static void character_device_test_overreport_rejection(void);

static void character_device_test_class_discovery(void);

// Static local variables
static const struct character_device_operations
    character_device_test_operations = {
        .read =
            character_device_test_read,
        .write =
            character_device_test_write,
    };

static const struct character_device_operations
    character_device_test_write_only_operations = {
        .read =
            NULL,
        .write =
            character_device_test_write,
    };

// Public functions implementations
void character_device_test_run(void)
{
    character_device_test_initialization();
    character_device_test_class_discovery();
    character_device_test_invalid_initialization();
    character_device_test_io_dispatch();
    character_device_test_missing_capability();
    character_device_test_backend_failure();
    character_device_test_overreport_rejection();

    diagnostics_write(
        "[device] Character-device capability and I/O contract tests passed\n"
    );
}

// Private functions and helpers implementations
static enum character_device_io_result character_device_test_read(
    struct character_device *character,
    void *buffer,
    size_t size,
    size_t *bytes_read)
{
    if (
        character == NULL ||
        buffer == NULL ||
        bytes_read == NULL ||
        character->private_data == NULL
    ) {
        kernel_panic(
            "Character-device test read received invalid state"
        );
    }

    struct character_device_test_state *state =
        character->private_data;

    ++state->read_calls;

    if (
        state->read_result !=
            CHARACTER_DEVICE_IO_RESULT_SUCCESS
    ) {
        return
            state->read_result;
    }

    uint8_t *bytes =
        buffer;

    size_t fill =
        state->read_transfer < size
            ? state->read_transfer
            : size;

    for (
        size_t index = 0;
        index < fill;
        ++index
    ) {
        bytes[index] =
            state->read_fill;
    }

    /*
     * Deliberately report the configured transfer rather than the bounded fill
     * amount. This lets the generic wrapper regression-test an over-reporting
     * backend without writing beyond the supplied buffer.
     */
    *bytes_read =
        state->read_transfer;

    return
        CHARACTER_DEVICE_IO_RESULT_SUCCESS;
}

static enum character_device_io_result character_device_test_write(
    struct character_device *character,
    const void *buffer,
    size_t size,
    size_t *bytes_written)
{
    if (
        character == NULL ||
        buffer == NULL ||
        bytes_written == NULL ||
        character->private_data == NULL
    ) {
        kernel_panic(
            "Character-device test write received invalid state"
        );
    }

    struct character_device_test_state *state =
        character->private_data;

    ++state->write_calls;

    state->last_write_size =
        size;

    if (size != 0) {
        const uint8_t *bytes =
            buffer;

        state->last_write_first_byte =
            bytes[0];
    }

    if (
        state->write_result !=
            CHARACTER_DEVICE_IO_RESULT_SUCCESS
    ) {
        return
            state->write_result;
    }

    *bytes_written =
        state->write_transfer;

    return
        CHARACTER_DEVICE_IO_RESULT_SUCCESS;
}

static void character_device_test_remove_generic(
    struct device *device)
{
    if (
        device == NULL ||
        !device_begin_removal(
            device
        ) ||
        !device_finish_removal(
            device
        ) ||
        !device_release(
            device
        )
    ) {
        kernel_panic(
            "Character-device generic fixture cleanup failed"
        );
    }
}

static void character_device_test_initialization(void)
{
    struct device generic;

    uint64_t generic_private =
        0x1122334455667788ULL;

    if (!device_initialize(
        &generic,
        6400,
        "char-test",
        sizeof("char-test") - 1U,
        DEVICE_KIND_PSEUDO,
        NULL,
        &generic_private
    )) {
        kernel_panic(
            "Character-device generic initialization failed"
        );
    }

    struct character_device_test_state state = {
        .read_result =
            CHARACTER_DEVICE_IO_RESULT_SUCCESS,
        .write_result =
            CHARACTER_DEVICE_IO_RESULT_SUCCESS,
        .read_transfer =
            0,
        .write_transfer =
            0,
        .read_calls =
            0,
        .write_calls =
            0,
        .last_write_size =
            0,
        .last_write_first_byte =
            0,
        .read_fill =
            0,
    };

    struct character_device character;

    if (!character_device_initialize(
        &character,
        &generic,
        &character_device_test_operations,
        &state
    )) {
        kernel_panic(
            "Character-device initialization rejected valid capability"
        );
    }

    if (
        character.device !=
            &generic ||
        character.operations !=
            &character_device_test_operations ||
        character.private_data !=
            &state ||
        generic.device_class !=
            DEVICE_CLASS_CHARACTER ||
        generic.class_interface !=
            &character ||
        character_device_from_device(
            &generic
        ) !=
            &character ||
        generic.reference_count !=
            1 ||
        generic.private_data !=
            &generic_private ||
        !character_device_can_read(
            &character
        ) ||
        !character_device_can_write(
            &character
        )
    ) {
        kernel_panic(
            "Character-device initialization produced invalid state"
        );
    }

    character_device_test_remove_generic(
        &generic
    );
}

static void character_device_test_class_discovery(void)
{
    struct device generic;

    if (!device_initialize(
        &generic,
        6405,
        "char-class",
        sizeof("char-class") - 1U,
        DEVICE_KIND_PSEUDO,
        NULL,
        NULL
    )) {
        kernel_panic(
            "Character-device class fixture initialization failed"
        );
    }

    struct character_device_test_state state = {
        .read_result =
            CHARACTER_DEVICE_IO_RESULT_SUCCESS,
        .write_result =
            CHARACTER_DEVICE_IO_RESULT_SUCCESS,
    };

    struct character_device character;

    if (
        !character_device_initialize(
            &character,
            &generic,
            &character_device_test_operations,
            &state
        ) ||
        character_device_from_device(
            &generic
        ) !=
            &character
    ) {
        kernel_panic(
            "Character-device class discovery failed"
        );
    }

    struct character_device duplicate = {
        .device =
            (struct device *) &state,
        .operations =
            &character_device_test_operations,
        .private_data =
            &generic,
    };

    if (
        character_device_initialize(
            &duplicate,
            &generic,
            &character_device_test_operations,
            &state
        ) ||
        duplicate.device != NULL ||
        duplicate.operations != NULL ||
        duplicate.private_data != NULL ||
        character_device_from_device(
            &generic
        ) !=
            &character
    ) {
        kernel_panic(
            "Character-device duplicate class binding was not contained"
        );
    }

    if (!device_begin_removal(
        &generic
    )) {
        kernel_panic(
            "Character-device class fixture removal failed"
        );
    }

    if (
        character_device_from_device(
            &generic
        ) != NULL ||
        character_device_can_read(
            &character
        ) ||
        character_device_can_write(
            &character
        )
    ) {
        kernel_panic(
            "Removing character device remained discoverable"
        );
    }

    if (
        !device_finish_removal(
            &generic
        ) ||
        !device_release(
            &generic
        )
    ) {
        kernel_panic(
            "Character-device class fixture cleanup failed"
        );
    }
}

static void character_device_test_invalid_initialization(void)
{
    struct device generic;

    if (!device_initialize(
        &generic,
        6410,
        "char-invalid",
        sizeof("char-invalid") - 1U,
        DEVICE_KIND_PSEUDO,
        NULL,
        NULL
    )) {
        kernel_panic(
            "Character-device invalid fixture initialization failed"
        );
    }

    struct character_device character;

    struct character_device_operations empty_operations = {
        .read =
            NULL,
        .write =
            NULL,
    };

    if (
        character_device_initialize(
            NULL,
            &generic,
            &character_device_test_operations,
            NULL
        ) ||
        character_device_initialize(
            &character,
            NULL,
            &character_device_test_operations,
            NULL
        ) ||
        character_device_initialize(
            &character,
            &generic,
            NULL,
            NULL
        ) ||
        character_device_initialize(
            &character,
            &generic,
            &empty_operations,
            NULL
        )
    ) {
        kernel_panic(
            "Character-device initialization accepted invalid arguments"
        );
    }

    if (!device_begin_removal(
        &generic
    )) {
        kernel_panic(
            "Character-device inactive fixture transition failed"
        );
    }

    if (character_device_initialize(
        &character,
        &generic,
        &character_device_test_operations,
        NULL
    )) {
        kernel_panic(
            "Character-device initialization accepted non-ACTIVE device"
        );
    }

    if (
        !device_finish_removal(
            &generic
        ) ||
        !device_release(
            &generic
        )
    ) {
        kernel_panic(
            "Character-device invalid fixture cleanup failed"
        );
    }
}

static void character_device_test_io_dispatch(void)
{
    struct device generic;

    if (!device_initialize(
        &generic,
        6420,
        "char-io",
        sizeof("char-io") - 1U,
        DEVICE_KIND_PSEUDO,
        NULL,
        NULL
    )) {
        kernel_panic(
            "Character-device I/O generic initialization failed"
        );
    }

    struct character_device_test_state state = {
        .read_result =
            CHARACTER_DEVICE_IO_RESULT_SUCCESS,
        .write_result =
            CHARACTER_DEVICE_IO_RESULT_SUCCESS,
        .read_transfer =
            3,
        .write_transfer =
            4,
        .read_calls =
            0,
        .write_calls =
            0,
        .last_write_size =
            0,
        .last_write_first_byte =
            0,
        .read_fill =
            0xa5U,
    };

    struct character_device character;

    if (!character_device_initialize(
        &character,
        &generic,
        &character_device_test_operations,
        &state
    )) {
        kernel_panic(
            "Character-device I/O capability initialization failed"
        );
    }

    uint8_t read_buffer[8] = {0};

    size_t bytes_read =
        SIZE_MAX;

    if (
        character_device_read(
            &character,
            read_buffer,
            sizeof(read_buffer),
            &bytes_read
        ) !=
            CHARACTER_DEVICE_IO_RESULT_SUCCESS ||
        bytes_read != 3 ||
        state.read_calls != 1 ||
        read_buffer[0] != 0xa5U ||
        read_buffer[1] != 0xa5U ||
        read_buffer[2] != 0xa5U ||
        read_buffer[3] != 0
    ) {
        kernel_panic(
            "Character-device read dispatch failed"
        );
    }

    static const uint8_t write_buffer[] = {
        0x31U,
        0x32U,
        0x33U,
        0x34U,
    };

    size_t bytes_written =
        SIZE_MAX;

    if (
        character_device_write(
            &character,
            write_buffer,
            sizeof(write_buffer),
            &bytes_written
        ) !=
            CHARACTER_DEVICE_IO_RESULT_SUCCESS ||
        bytes_written !=
            sizeof(write_buffer) ||
        state.write_calls != 1 ||
        state.last_write_size !=
            sizeof(write_buffer) ||
        state.last_write_first_byte !=
            write_buffer[0]
    ) {
        kernel_panic(
            "Character-device write dispatch failed"
        );
    }

    bytes_read =
        SIZE_MAX;

    bytes_written =
        SIZE_MAX;

    if (
        character_device_read(
            &character,
            NULL,
            0,
            &bytes_read
        ) !=
            CHARACTER_DEVICE_IO_RESULT_SUCCESS ||
        character_device_write(
            &character,
            NULL,
            0,
            &bytes_written
        ) !=
            CHARACTER_DEVICE_IO_RESULT_SUCCESS ||
        bytes_read != 0 ||
        bytes_written != 0 ||
        state.read_calls != 1 ||
        state.write_calls != 1
    ) {
        kernel_panic(
            "Character-device zero-length I/O contract failed"
        );
    }

    character_device_test_remove_generic(
        &generic
    );
}

static void character_device_test_missing_capability(void)
{
    struct device generic;

    if (!device_initialize(
        &generic,
        6430,
        "char-write-only",
        sizeof("char-write-only") - 1U,
        DEVICE_KIND_PSEUDO,
        NULL,
        NULL
    )) {
        kernel_panic(
            "Character-device capability fixture initialization failed"
        );
    }

    struct character_device_test_state state = {
        .read_result =
            CHARACTER_DEVICE_IO_RESULT_SUCCESS,
        .write_result =
            CHARACTER_DEVICE_IO_RESULT_SUCCESS,
        .read_transfer =
            0,
        .write_transfer =
            1,
        .read_calls =
            0,
        .write_calls =
            0,
        .last_write_size =
            0,
        .last_write_first_byte =
            0,
        .read_fill =
            0,
    };

    struct character_device character;

    if (
        !character_device_initialize(
            &character,
            &generic,
            &character_device_test_write_only_operations,
            &state
        ) ||
        character_device_can_read(
            &character
        ) ||
        !character_device_can_write(
            &character
        )
    ) {
        kernel_panic(
            "Character-device capability discovery failed"
        );
    }

    uint8_t byte =
        0;

    size_t bytes_read =
        91;

    if (
        character_device_read(
            &character,
            &byte,
            sizeof(byte),
            &bytes_read
        ) !=
            CHARACTER_DEVICE_IO_RESULT_NOT_SUPPORTED ||
        bytes_read != 91 ||
        state.read_calls != 0
    ) {
        kernel_panic(
            "Character-device missing read capability was not rejected"
        );
    }

    bytes_read =
        92;

    if (
        character_device_read(
            &character,
            NULL,
            0,
            &bytes_read
        ) !=
            CHARACTER_DEVICE_IO_RESULT_NOT_SUPPORTED ||
        bytes_read != 92
    ) {
        kernel_panic(
            "Character-device zero-length unsupported read was accepted"
        );
    }

    character_device_test_remove_generic(
        &generic
    );
}

static void character_device_test_backend_failure(void)
{
    struct device generic;

    if (!device_initialize(
        &generic,
        6440,
        "char-failure",
        sizeof("char-failure") - 1U,
        DEVICE_KIND_PSEUDO,
        NULL,
        NULL
    )) {
        kernel_panic(
            "Character-device failure fixture initialization failed"
        );
    }

    struct character_device_test_state state = {
        .read_result =
            CHARACTER_DEVICE_IO_RESULT_RESOURCE_EXHAUSTED,
        .write_result =
            CHARACTER_DEVICE_IO_RESULT_NOT_SUPPORTED,
        .read_transfer =
            0,
        .write_transfer =
            0,
        .read_calls =
            0,
        .write_calls =
            0,
        .last_write_size =
            0,
        .last_write_first_byte =
            0,
        .read_fill =
            0,
    };

    struct character_device character;

    if (!character_device_initialize(
        &character,
        &generic,
        &character_device_test_operations,
        &state
    )) {
        kernel_panic(
            "Character-device failure capability initialization failed"
        );
    }

    uint8_t buffer =
        0x5aU;

    size_t bytes_read =
        71;

    size_t bytes_written =
        72;

    if (
        character_device_read(
            &character,
            &buffer,
            sizeof(buffer),
            &bytes_read
        ) !=
            CHARACTER_DEVICE_IO_RESULT_RESOURCE_EXHAUSTED ||
        character_device_write(
            &character,
            &buffer,
            sizeof(buffer),
            &bytes_written
        ) !=
            CHARACTER_DEVICE_IO_RESULT_NOT_SUPPORTED ||
        bytes_read != 71 ||
        bytes_written != 72 ||
        state.read_calls != 1 ||
        state.write_calls != 1
    ) {
        kernel_panic(
            "Character-device backend failure contract failed"
        );
    }

    character_device_test_remove_generic(
        &generic
    );
}

static void character_device_test_overreport_rejection(void)
{
    struct device generic;

    if (!device_initialize(
        &generic,
        6450,
        "char-overreport",
        sizeof("char-overreport") - 1U,
        DEVICE_KIND_PSEUDO,
        NULL,
        NULL
    )) {
        kernel_panic(
            "Character-device over-report fixture initialization failed"
        );
    }

    struct character_device_test_state state = {
        .read_result =
            CHARACTER_DEVICE_IO_RESULT_SUCCESS,
        .write_result =
            CHARACTER_DEVICE_IO_RESULT_SUCCESS,
        .read_transfer =
            5,
        .write_transfer =
            5,
        .read_calls =
            0,
        .write_calls =
            0,
        .last_write_size =
            0,
        .last_write_first_byte =
            0,
        .read_fill =
            0xccU,
    };

    struct character_device character;

    if (!character_device_initialize(
        &character,
        &generic,
        &character_device_test_operations,
        &state
    )) {
        kernel_panic(
            "Character-device over-report capability initialization failed"
        );
    }

    uint8_t buffer[4] = {
        0,
        0,
        0,
        0,
    };

    size_t bytes_read =
        81;

    size_t bytes_written =
        82;

    if (
        character_device_read(
            &character,
            buffer,
            sizeof(buffer),
            &bytes_read
        ) !=
            CHARACTER_DEVICE_IO_RESULT_INVALID_ARGUMENT ||
        character_device_write(
            &character,
            buffer,
            sizeof(buffer),
            &bytes_written
        ) !=
            CHARACTER_DEVICE_IO_RESULT_INVALID_ARGUMENT ||
        bytes_read != 81 ||
        bytes_written != 82 ||
        state.read_calls != 1 ||
        state.write_calls != 1
    ) {
        kernel_panic(
            "Character-device backend over-report was not contained"
        );
    }

    character_device_test_remove_generic(
        &generic
    );
}
