// SPDX-License-Identifier: GPL-2.0-only

#include "ps2_mouse_packet_test.h"

#include "../arch/x86_64/ps2_mouse_packet.h"
#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"

// PRIVATE HELPERS AND FUNCTIONS DECLARATIONS
static void expect_no_event(
    struct ps2_mouse_packet_decoder *decoder,
    uint8_t byte
);

static void expect_pointer(
    struct ps2_mouse_packet_decoder *decoder,
    uint8_t first,
    uint8_t second,
    uint8_t third,
    int16_t delta_x,
    int16_t delta_y,
    uint8_t buttons
);

// PUBLIC FUNCTIONS IMPLEMENTATIONS
void ps2_mouse_packet_test_run(void)
{
    struct ps2_mouse_packet_decoder decoder;

    ps2_mouse_packet_init(
        &decoder
    );

    /*
     * Neutral packet: exactly what QEMU produced during bring-up.
     */
    expect_pointer(
        &decoder,
        0x08U,
        0x00U,
        0x00U,
        0,
        0,
        0
    );

    /*
     * Move right five counts while holding the left button.
     */
    expect_pointer(
        &decoder,
        0x09U,
        0x05U,
        0x00U,
        5,
        0,
        INPUT_POINTER_BUTTON_LEFT
    );

    /*
     * Negative X is sign-extended from the packet status bit.
     */
    expect_pointer(
        &decoder,
        0x18U,
        0xFBU,
        0x00U,
        -5,
        0,
        0
    );

    /*
     * Native positive PS/2 Y means upward. MyOS normalizes that
     * into negative framebuffer Y.
     */
    expect_pointer(
        &decoder,
        0x08U,
        0x00U,
        0x05U,
        0,
        -5,
        0
    );

    /*
     * Native negative Y therefore becomes positive screen Y.
     */
    expect_pointer(
        &decoder,
        0x28U,
        0x00U,
        0xFBU,
        0,
        5,
        0
    );

    /*
     * All three standard buttons can be represented together.
     */
    expect_pointer(
        &decoder,
        0x0FU,
        0x00U,
        0x00U,
        0,
        0,
        INPUT_POINTER_BUTTON_LEFT |
        INPUT_POINTER_BUTTON_RIGHT |
        INPUT_POINTER_BUTTON_MIDDLE
    );

    /*
     * Bytes without the mandatory first-byte sync bit must be
     * ignored while waiting for a new packet.
     */
    expect_no_event(
        &decoder,
        0x00U
    );

    expect_pointer(
        &decoder,
        0x08U,
        0x01U,
        0x00U,
        1,
        0,
        0
    );

    /*
     * Overflow packets must be consumed but not published.
     */
    expect_no_event(
        &decoder,
        0x48U
    );

    expect_no_event(
        &decoder,
        0xFFU
    );

    expect_no_event(
        &decoder,
        0x00U
    );

    /*
     * The decoder must recover immediately after the dropped packet.
     */
    expect_pointer(
        &decoder,
        0x08U,
        0x02U,
        0x00U,
        2,
        0,
        0
    );

    diagnostics_write(
        "[ps2] Mouse packet decoder tests passed\n"
    );
}

// PRIVATE HELPERS AND FUNCTIONS IMPLEMENTATIONS
static void expect_no_event(
    struct ps2_mouse_packet_decoder *decoder,
    uint8_t byte)
{
    struct input_event event;

    if (
        ps2_mouse_packet_decode(
            decoder,
            byte,
            &event
        )
    ) {
        kernel_panic(
            "PS/2 mouse decoder produced unexpected event"
        );
    }
}

static void expect_pointer(
    struct ps2_mouse_packet_decoder *decoder,
    uint8_t first,
    uint8_t second,
    uint8_t third,
    int16_t delta_x,
    int16_t delta_y,
    uint8_t buttons)
{
    struct input_event event;

    if (
        ps2_mouse_packet_decode(
            decoder,
            first,
            &event
        ) ||
        ps2_mouse_packet_decode(
            decoder,
            second,
            &event
        ) ||
        !ps2_mouse_packet_decode(
            decoder,
            third,
            &event
        ) ||
        event.type !=
            INPUT_EVENT_POINTER ||
        event.pointer.delta_x !=
            delta_x ||
        event.pointer.delta_y !=
            delta_y ||
        event.pointer.delta_wheel != 0 ||
        event.pointer.buttons !=
            buttons
    ) {
        kernel_panic(
            "PS/2 mouse decoder produced incorrect pointer event"
        );
    }
}
