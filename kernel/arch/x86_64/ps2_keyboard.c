// SPDX-License-Identifier: GPL-2.0-only

#include "ps2_keyboard.h"

#include "lapic.h"
#include "ps2_scancode_set1.h"

#include "../../input/input.h"

#include <stdbool.h>
#include <stdint.h>

#define PS2_DATA_PORT           0x60U
#define PS2_STATUS_COMMAND_PORT 0x64U

#define PS2_STATUS_OUTPUT_FULL (1U << 0)
#define PS2_STATUS_INPUT_FULL  (1U << 1)
#define PS2_STATUS_AUXILIARY   (1U << 5)
#define PS2_STATUS_TIMEOUT     (1U << 6)
#define PS2_STATUS_PARITY      (1U << 7)

#define PS2_COMMAND_READ_CONFIGURATION  0x20U
#define PS2_COMMAND_WRITE_CONFIGURATION 0x60U
#define PS2_COMMAND_DISABLE_FIRST_PORT  0xADU
#define PS2_COMMAND_ENABLE_FIRST_PORT   0xAEU

#define PS2_CONFIG_FIRST_PORT_IRQ      (1U << 0)
#define PS2_CONFIG_FIRST_PORT_DISABLED (1U << 4)

#define PS2_KEYBOARD_ENABLE_SCANNING 0xF4U
#define PS2_DEVICE_ACK              0xFAU
#define PS2_DEVICE_RESEND           0xFEU

#define PS2_WAIT_ITERATIONS 1000000U
#define PS2_DEVICE_RETRIES  3U
#define PS2_FLUSH_LIMIT     32U

static bool ps2_keyboard_initialized;

static struct ps2_scancode_set1_decoder
    ps2_keyboard_decoder;

// PRIVATE HELPERS AND FUNCTIONS DECLARATIONS
static uint8_t ps2_in8(uint16_t port);
static void ps2_out8(uint16_t port, uint8_t value);

static bool ps2_wait_input_empty(void);
static bool ps2_wait_output_full(void);

static bool ps2_write_command(uint8_t command);
static bool ps2_write_data(uint8_t value);

static void ps2_flush_output(void);

static bool ps2_keyboard_send_command(
    uint8_t command
);

// PUBLIC FUNCTIONS IMPLEMENTATIONS
bool ps2_keyboard_init(void)
{
    if (ps2_keyboard_initialized) {
        return true;
    }

    ps2_flush_output();

    /*
     * Temporarily disable the first port while updating the
     * controller configuration byte.
     */
    if (
        !ps2_write_command(
            PS2_COMMAND_DISABLE_FIRST_PORT
        )
    ) {
        return false;
    }

    if (
        !ps2_write_command(
            PS2_COMMAND_READ_CONFIGURATION
        ) ||
        !ps2_wait_output_full()
    ) {
        return false;
    }

    uint8_t configuration =
        ps2_in8(PS2_DATA_PORT);

    configuration |=
        PS2_CONFIG_FIRST_PORT_IRQ;

    configuration &=
        (uint8_t) ~PS2_CONFIG_FIRST_PORT_DISABLED;

    if (
        !ps2_write_command(
            PS2_COMMAND_WRITE_CONFIGURATION
        ) ||
        !ps2_write_data(configuration)
    ) {
        return false;
    }

    if (
        !ps2_write_command(
            PS2_COMMAND_ENABLE_FIRST_PORT
        )
    ) {
        return false;
    }

    /*
     * Remove any byte left over from firmware interaction before
     * issuing the keyboard command whose acknowledgement we expect.
     */
    ps2_flush_output();

    if (
        !ps2_keyboard_send_command(
            PS2_KEYBOARD_ENABLE_SCANNING
        )
    ) {
        return false;
    }

    ps2_scancode_set1_init(
        &ps2_keyboard_decoder
    );

    ps2_keyboard_initialized = true;

    return true;
}

void ps2_keyboard_handle_interrupt(
    struct interrupt_context *context)
{
    (void) context;

    uint8_t status =
        ps2_in8(PS2_STATUS_COMMAND_PORT);

    if (
        (status & PS2_STATUS_OUTPUT_FULL) == 0
    ) {
        lapic_send_eoi();
        return;
    }

    /*
     * Always drain the byte that caused the interrupt.
     */
    uint8_t scancode =
        ps2_in8(PS2_DATA_PORT);

    bool valid_keyboard_byte =
        (status & PS2_STATUS_AUXILIARY) == 0 &&
        (
            status &
            (
                PS2_STATUS_TIMEOUT |
                PS2_STATUS_PARITY
            )
        ) == 0;

    /*
     * Acknowledge the Local APIC as soon as the controller byte has
     * been consumed.
     */
    lapic_send_eoi();

    if (valid_keyboard_byte) {
        struct input_event event;

        if (
            ps2_scancode_set1_decode(
                &ps2_keyboard_decoder,
                scancode,
                &event
            )
        ) {
            /*
             * Queue overflow deliberately drops the newest event.
             * The interrupt path must never block waiting for a
             * consumer.
             */
            (void) input_event_submit(
                &event
            );
        }
    }
}

// PRIVATE HELPERS AND FUNCTIONS IMPLEMENTATIONS
static uint8_t ps2_in8(uint16_t port)
{
    uint8_t value;

    __asm__ volatile (
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

static void ps2_out8(
    uint16_t port,
    uint8_t value)
{
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

static bool ps2_wait_input_empty(void)
{
    for (
        uint32_t iteration = 0;
        iteration < PS2_WAIT_ITERATIONS;
        ++iteration
    ) {
        uint8_t status =
            ps2_in8(PS2_STATUS_COMMAND_PORT);

        if (
            (status & PS2_STATUS_INPUT_FULL) == 0
        ) {
            return true;
        }

        __asm__ volatile ("pause");
    }

    return false;
}

static bool ps2_wait_output_full(void)
{
    for (
        uint32_t iteration = 0;
        iteration < PS2_WAIT_ITERATIONS;
        ++iteration
    ) {
        uint8_t status =
            ps2_in8(PS2_STATUS_COMMAND_PORT);

        if (
            (status & PS2_STATUS_OUTPUT_FULL) != 0
        ) {
            return true;
        }

        __asm__ volatile ("pause");
    }

    return false;
}

static bool ps2_write_command(uint8_t command)
{
    if (!ps2_wait_input_empty()) {
        return false;
    }

    ps2_out8(
        PS2_STATUS_COMMAND_PORT,
        command
    );

    return true;
}

static bool ps2_write_data(uint8_t value)
{
    if (!ps2_wait_input_empty()) {
        return false;
    }

    ps2_out8(
        PS2_DATA_PORT,
        value
    );

    return true;
}

static void ps2_flush_output(void)
{
    for (
        uint32_t count = 0;
        count < PS2_FLUSH_LIMIT;
        ++count
    ) {
        uint8_t status =
            ps2_in8(PS2_STATUS_COMMAND_PORT);

        if (
            (status & PS2_STATUS_OUTPUT_FULL) == 0
        ) {
            return;
        }

        (void) ps2_in8(PS2_DATA_PORT);
    }
}

static bool ps2_keyboard_send_command(
    uint8_t command)
{
    for (
        uint32_t attempt = 0;
        attempt < PS2_DEVICE_RETRIES;
        ++attempt
    ) {
        if (!ps2_write_data(command)) {
            return false;
        }

        if (!ps2_wait_output_full()) {
            return false;
        }

        uint8_t response =
            ps2_in8(PS2_DATA_PORT);

        if (response == PS2_DEVICE_ACK) {
            return true;
        }

        if (response != PS2_DEVICE_RESEND) {
            return false;
        }
    }

    return false;
}
