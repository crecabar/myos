// SPDX-License-Identifier: GPL-2.0-only

#ifndef MYOS_ARCH_X86_64_PS2_MOUSE_PACKET_H
#define MYOS_ARCH_X86_64_PS2_MOUSE_PACKET_H

#include "../../input/input.h"

#include <stdbool.h>
#include <stdint.h>

struct ps2_mouse_packet_decoder {
    uint8_t bytes[3];
    uint8_t index;
};

/**
 * Resets a standard three-byte PS/2 mouse packet decoder.
 */
void ps2_mouse_packet_init(
    struct ps2_mouse_packet_decoder *decoder
);

/**
 * Consumes one byte from a standard PS/2 mouse packet stream.
 *
 * @return true when one complete normalized pointer event was
 *         produced; false while accumulating a packet or when an
 *         invalid/overflow packet was discarded.
 */
bool ps2_mouse_packet_decode(
    struct ps2_mouse_packet_decoder *decoder,
    uint8_t byte,
    struct input_event *event
);

/**
 * Discards any partially accumulated packet and returns the decoder
 * to first-byte synchronization.
 */
void ps2_mouse_packet_reset(
    struct ps2_mouse_packet_decoder *decoder
);

#endif
