// SPDX-License-Identifier: GPL-2.0-only

#include "i8042.h"

#include <stdint.h>

#define I8042_DATA_PORT           0x60U
#define I8042_STATUS_COMMAND_PORT 0x64U

#define I8042_WAIT_ITERATIONS 1000000U
#define I8042_FLUSH_LIMIT     32U

// PRIVATE HELPERS AND FUNCTIONS DECLARATIONS
static uint8_t i8042_in8(
    uint16_t port
);

static void i8042_out8(
    uint16_t port,
    uint8_t value
);

// PUBLIC FUNCIONS IMPLEMENTATIONS
uint8_t i8042_status_read(void)
{
    return i8042_in8(
        I8042_STATUS_COMMAND_PORT
    );
}

uint8_t i8042_data_read(void)
{
    return i8042_in8(
        I8042_DATA_PORT
    );
}

bool i8042_wait_input_empty(void)
{
    for (
        uint32_t iteration = 0;
        iteration < I8042_WAIT_ITERATIONS;
        ++iteration
    ) {
        if (
            (
                i8042_status_read() &
                I8042_STATUS_INPUT_FULL
            ) == 0
        ) {
            return true;
        }

        __asm__ volatile ("pause");
    }

    return false;
}

bool i8042_wait_output_full(void)
{
    for (
        uint32_t iteration = 0;
        iteration < I8042_WAIT_ITERATIONS;
        ++iteration
    ) {
        if (
            (
                i8042_status_read() &
                I8042_STATUS_OUTPUT_FULL
            ) != 0
        ) {
            return true;
        }

        __asm__ volatile ("pause");
    }

    return false;
}

bool i8042_command_write(
    uint8_t command)
{
    if (!i8042_wait_input_empty()) {
        return false;
    }

    i8042_out8(
        I8042_STATUS_COMMAND_PORT,
        command
    );

    return true;
}

bool i8042_data_write(
    uint8_t value)
{
    if (!i8042_wait_input_empty()) {
        return false;
    }

    i8042_out8(
        I8042_DATA_PORT,
        value
    );

    return true;
}

void i8042_flush_output(void)
{
    for (
        uint32_t count = 0;
        count < I8042_FLUSH_LIMIT;
        ++count
    ) {
        if (
            (
                i8042_status_read() &
                I8042_STATUS_OUTPUT_FULL
            ) == 0
        ) {
            return;
        }

        (void) i8042_data_read();
    }
}

// PRIVATE HELPERS AND FUNCTIONS IMPLEMENTATIONS
static uint8_t i8042_in8(
    uint16_t port)
{
    uint8_t value;

    __asm__ volatile (
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

static void i8042_out8(
    uint16_t port,
    uint8_t value)
{
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}
