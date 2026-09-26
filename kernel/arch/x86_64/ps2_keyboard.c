// SPDX-License-Identifier: GPL-2.0-only

#include "ps2_keyboard.h"

#include "lapic.h"

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

static bool ps2_keyboard_extended_prefix;
static uint8_t ps2_keyboard_e1_bytes_remaining;

static enum input_key_code
ps2_keyboard_set1_base_key(
    uint8_t code)
{
    switch (code) {
        case 0x01:
            return INPUT_KEY_ESCAPE;

        case 0x0E:
            return INPUT_KEY_BACKSPACE;

        case 0x0F:
            return INPUT_KEY_TAB;

        case 0x10:
            return INPUT_KEY_Q;

        case 0x11:
            return INPUT_KEY_W;

        case 0x12:
            return INPUT_KEY_E;

        case 0x13:
            return INPUT_KEY_R;

        case 0x14:
            return INPUT_KEY_T;

        case 0x15:
            return INPUT_KEY_Y;

        case 0x16:
            return INPUT_KEY_U;

        case 0x17:
            return INPUT_KEY_I;

        case 0x18:
            return INPUT_KEY_O;

        case 0x19:
            return INPUT_KEY_P;

        case 0x1C:
            return INPUT_KEY_ENTER;

        case 0x1D:
            return INPUT_KEY_LEFT_CTRL;

        case 0x1E:
            return INPUT_KEY_A;

        case 0x1F:
            return INPUT_KEY_S;

        case 0x20:
            return INPUT_KEY_D;

        case 0x21:
            return INPUT_KEY_F;

        case 0x22:
            return INPUT_KEY_G;

        case 0x23:
            return INPUT_KEY_H;

        case 0x24:
            return INPUT_KEY_J;

        case 0x25:
            return INPUT_KEY_K;

        case 0x26:
            return INPUT_KEY_L;

        case 0x2A:
            return INPUT_KEY_LEFT_SHIFT;

        case 0x2C:
            return INPUT_KEY_Z;

        case 0x2D:
            return INPUT_KEY_X;

        case 0x2E:
            return INPUT_KEY_C;

        case 0x2F:
            return INPUT_KEY_V;

        case 0x30:
            return INPUT_KEY_B;

        case 0x31:
            return INPUT_KEY_N;

        case 0x32:
            return INPUT_KEY_M;

        case 0x36:
            return INPUT_KEY_RIGHT_SHIFT;

        case 0x38:
            return INPUT_KEY_LEFT_ALT;

        case 0x39:
            return INPUT_KEY_SPACE;

        default:
            return INPUT_KEY_NONE;
    }
}

static enum input_key_code
ps2_keyboard_set1_extended_key(
    uint8_t code)
{
    switch (code) {
        case 0x1D:
            return INPUT_KEY_RIGHT_CTRL;

        case 0x38:
            return INPUT_KEY_RIGHT_ALT;

        case 0x48:
            return INPUT_KEY_UP;

        case 0x4B:
            return INPUT_KEY_LEFT;

        case 0x4D:
            return INPUT_KEY_RIGHT;

        case 0x50:
            return INPUT_KEY_DOWN;

        case 0x53:
            return INPUT_KEY_DELETE;

        case 0x5B:
            return INPUT_KEY_LEFT_GUI;

        case 0x5C:
            return INPUT_KEY_RIGHT_GUI;

        default:
            return INPUT_KEY_NONE;
    }
}

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

static enum input_key_code ps2_keyboard_set1_base_key(
    uint8_t code
);

static enum input_key_code ps2_keyboard_set1_extended_key(
    uint8_t code
);

static void ps2_keyboard_decode_byte(
    uint8_t scancode
);

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

static void ps2_keyboard_decode_byte(
    uint8_t scancode)
{
    /*
     * Pause/Break begins with E1 and uses a multi-byte Set 1
     * sequence. Ignore it for this first decoder increment rather
     * than generating incorrect key events.
     */
    if (ps2_keyboard_e1_bytes_remaining != 0) {
        --ps2_keyboard_e1_bytes_remaining;
        return;
    }

    if (scancode == 0xE1U) {
        ps2_keyboard_e1_bytes_remaining = 5U;
        ps2_keyboard_extended_prefix = false;
        return;
    }

    if (scancode == 0xE0U) {
        ps2_keyboard_extended_prefix = true;
        return;
    }

    bool released =
        (scancode & 0x80U) != 0;

    uint8_t make_code =
        scancode & 0x7FU;

    enum input_key_code key =
        ps2_keyboard_extended_prefix
            ? ps2_keyboard_set1_extended_key(
                make_code
            )
            : ps2_keyboard_set1_base_key(
                make_code
            );

    ps2_keyboard_extended_prefix = false;

    if (key == INPUT_KEY_NONE) {
        return;
    }

    struct input_event event = {
        .type = INPUT_EVENT_KEY,
        .key = {
            .code = key,
            .state = released
                ? INPUT_KEY_RELEASED
                : INPUT_KEY_PRESSED,
        },
    };

    /*
     * Queue overflow deliberately drops the newest event.
     * The interrupt path must never block waiting for a consumer.
     */
     (void) input_event_submit(&event);
}

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
        ps2_keyboard_decode_byte(
            scancode
        );
    }
}
