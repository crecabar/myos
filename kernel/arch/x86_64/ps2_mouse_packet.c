// SPDX-License-Identifier: GPL-2.0-only

#include "ps2_mouse_packet.h"

#include <stddef.h>
#include <stdint.h>

#define PS2_MOUSE_BUTTON_LEFT   (1U << 0)
#define PS2_MOUSE_BUTTON_RIGHT  (1U << 1)
#define PS2_MOUSE_BUTTON_MIDDLE (1U << 2)

#define PS2_MOUSE_SYNC       (1U << 3)
#define PS2_MOUSE_X_SIGN     (1U << 4)
#define PS2_MOUSE_Y_SIGN     (1U << 5)
#define PS2_MOUSE_X_OVERFLOW (1U << 6)
#define PS2_MOUSE_Y_OVERFLOW (1U << 7)

void ps2_mouse_packet_init(
    struct ps2_mouse_packet_decoder *decoder)
{
    if (decoder == NULL) {
        return;
    }

    decoder->bytes[0] = 0;
    decoder->bytes[1] = 0;
    decoder->bytes[2] = 0;
    decoder->index = 0;
}

bool ps2_mouse_packet_decode(
    struct ps2_mouse_packet_decoder *decoder,
    uint8_t byte,
    struct input_event *event)
{
    if (
        decoder == NULL ||
        event == NULL
    ) {
        return false;
    }

    /*
     * Bit 3 of the first byte is always set in a standard PS/2
     * mouse packet. Use it to establish packet synchronization.
     */
    if (
        decoder->index == 0 &&
        (byte & PS2_MOUSE_SYNC) == 0
    ) {
        return false;
    }

    decoder->bytes[
        decoder->index
    ] = byte;

    ++decoder->index;

    if (decoder->index < 3U) {
        return false;
    }

    decoder->index = 0;

    uint8_t status =
        decoder->bytes[0];

    /*
     * Movement exceeding the representable packet range cannot be
     * reconstructed reliably. Drop the entire packet.
     */
    if (
        (
            status &
            (
                PS2_MOUSE_X_OVERFLOW |
                PS2_MOUSE_Y_OVERFLOW
            )
        ) != 0
    ) {
        return false;
    }

    int16_t delta_x =
        decoder->bytes[1];

    if (
        (status & PS2_MOUSE_X_SIGN) != 0
    ) {
        delta_x -= 256;
    }

    int16_t raw_delta_y =
        decoder->bytes[2];

    if (
        (status & PS2_MOUSE_Y_SIGN) != 0
    ) {
        raw_delta_y -= 256;
    }

    uint8_t buttons = 0;

    if (
        (status & PS2_MOUSE_BUTTON_LEFT) != 0
    ) {
        buttons |=
            INPUT_POINTER_BUTTON_LEFT;
    }

    if (
        (status & PS2_MOUSE_BUTTON_RIGHT) != 0
    ) {
        buttons |=
            INPUT_POINTER_BUTTON_RIGHT;
    }

    if (
        (status & PS2_MOUSE_BUTTON_MIDDLE) != 0
    ) {
        buttons |=
            INPUT_POINTER_BUTTON_MIDDLE;
    }

    /*
     * Native PS/2 Y coordinates increase upward. MyOS pointer
     * coordinates follow the framebuffer convention and increase
     * downward, so invert Y here at the transport boundary.
     */
    *event = (struct input_event) {
        .type = INPUT_EVENT_POINTER,
        .pointer = {
            .delta_x = delta_x,
            .delta_y =
                (int16_t) -raw_delta_y,
            .delta_wheel = 0,
            .buttons = buttons,
        },
    };

    return true;
}
