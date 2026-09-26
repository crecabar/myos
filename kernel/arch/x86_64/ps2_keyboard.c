// SPDX-License-Identifier: GPL-2.0-only

#include "ps2_keyboard.h"

#include "i8042.h"
#include "lapic.h"
#include "ps2_scancode_set1.h"

#include "../../input/input.h"

#include <stdbool.h>
#include <stdint.h>

#define PS2_COMMAND_READ_CONFIGURATION  0x20U
#define PS2_COMMAND_WRITE_CONFIGURATION 0x60U
#define PS2_COMMAND_DISABLE_FIRST_PORT  0xADU
#define PS2_COMMAND_ENABLE_FIRST_PORT   0xAEU

#define PS2_KEYBOARD_SET_SCANCODE_SET 0xF0U
#define PS2_KEYBOARD_SCANCODE_SET_2   0x02U
#define PS2_KEYBOARD_ENABLE_SCANNING  0xF4U
#define PS2_KEYBOARD_DISABLE_SCANNING 0xF5U

#define PS2_DEVICE_ACK    0xFAU
#define PS2_DEVICE_RESEND 0xFEU

#define PS2_CONFIG_FIRST_PORT_IRQ      (1U << 0)
#define PS2_CONFIG_FIRST_PORT_DISABLED (1U << 4)
#define PS2_CONFIG_TRANSLATION         (1U << 6)

#define PS2_KEYBOARD_ENABLE_SCANNING 0xF4U
#define PS2_DEVICE_ACK              0xFAU
#define PS2_DEVICE_RESEND           0xFEU

#define PS2_DEVICE_RETRIES  3U

static bool ps2_keyboard_initialized;

static struct ps2_scancode_set1_decoder
    ps2_keyboard_decoder;

// PRIVATE HELPERS AND FUNCTIONS DECLARATIONS

static bool ps2_keyboard_send_command(
    uint8_t command
);

// PUBLIC FUNCTIONS IMPLEMENTATIONS
bool ps2_keyboard_init(void)
{
    if (ps2_keyboard_initialized) {
        return true;
    }

    i8042_flush_output();

    /*
     * Temporarily disable the first port while updating the
     * controller configuration byte.
     */
    if (
        !i8042_command_write(
            PS2_COMMAND_DISABLE_FIRST_PORT
        )
    ) {
        return false;
    }

    if (
        !i8042_command_write(
            PS2_COMMAND_READ_CONFIGURATION
        ) ||
        !i8042_wait_output_full()
    ) {
        return false;
    }

    uint8_t configuration = i8042_status_read(); // Not sure if data or status read

    /*
     * MyOS decodes translated Scan Code Set 1. Configure the
     * controller explicitly instead of inheriting firmware state.
     */
    configuration |=
        PS2_CONFIG_FIRST_PORT_IRQ |
        PS2_CONFIG_TRANSLATION;

    configuration &=
        (uint8_t) ~PS2_CONFIG_FIRST_PORT_DISABLED;

    if (
        !i8042_command_write(
            PS2_COMMAND_WRITE_CONFIGURATION
        ) ||
        !i8042_data_write(configuration)
    ) {
        return false;
    }

    if (
        !i8042_command_write(
            PS2_COMMAND_ENABLE_FIRST_PORT
        )
    ) {
        return false;
    }

    /*
     * Remove any byte left over from firmware interaction before
     * issuing the keyboard command whose acknowledgement we expect.
     */
    i8042_flush_output();

    /*
     * Stop asynchronous scan-code delivery while changing the
     * keyboard's scan-code set.
     *
     * The keyboard emits Set 2 and the i8042 translates it to
     * Set 1 before MyOS receives bytes from port 0x60.
     */
    if (
        !ps2_keyboard_send_command(
            PS2_KEYBOARD_DISABLE_SCANNING
        ) ||
        !ps2_keyboard_send_command(
            PS2_KEYBOARD_SET_SCANCODE_SET
        ) ||
        !ps2_keyboard_send_command(
            PS2_KEYBOARD_SCANCODE_SET_2
        ) ||
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
        i8042_status_read();

    if (
        (status & I8042_STATUS_OUTPUT_FULL) == 0
    ) {
        lapic_send_eoi();
        return;
    }

    /*
     * Always drain the byte that caused the interrupt.
     */
    uint8_t scancode =
        i8042_data_read();

    bool valid_keyboard_byte =
        (status & I8042_STATUS_AUXILIARY) == 0 &&
        (
            status &
            (
                I8042_STATUS_TIMEOUT |
                I8042_STATUS_PARITY
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
static bool ps2_keyboard_send_command(
    uint8_t command)
{
    for (
        uint32_t attempt = 0;
        attempt < PS2_DEVICE_RETRIES;
        ++attempt
    ) {
        if (!i8042_data_write(command)) {
            return false;
        }

        if (!i8042_wait_output_full()) {
            return false;
        }

        uint8_t response =
            i8042_data_read();

        if (response == PS2_DEVICE_ACK) {
            return true;
        }

        if (response != PS2_DEVICE_RESEND) {
            return false;
        }
    }

    return false;
}
