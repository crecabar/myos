// SPDX-License-Identifier: GPL-2.0-only

#ifndef MYOS_ARCH_X86_64_PS2_SCANCODE_SET1_H
#define MYOS_ARCH_X86_64_PS2_SCANCODE_SET1_H

#include "../../input/input.h"

#include <stdbool.h>
#include <stdint.h>

enum ps2_scancode_set1_state {
    PS2_SET1_STATE_BASE = 0,

    PS2_SET1_STATE_E0,

    PS2_SET1_STATE_PRINT_MAKE_E0,
    PS2_SET1_STATE_PRINT_MAKE_37,

    PS2_SET1_STATE_PRINT_BREAK_E0,
    PS2_SET1_STATE_PRINT_BREAK_AA,

    PS2_SET1_STATE_PAUSE_1D,
    PS2_SET1_STATE_PAUSE_45,
    PS2_SET1_STATE_PAUSE_E1,
    PS2_SET1_STATE_PAUSE_9D,
    PS2_SET1_STATE_PAUSE_C5
};

struct ps2_scancode_set1_decoder {
    enum ps2_scancode_set1_state state;
};

/**
 * Resets a PS/2 Scan Code Set 1 decoder.
 */
void ps2_scancode_set1_init(
    struct ps2_scancode_set1_decoder *decoder
);

/**
 * Consumes one byte from a Scan Code Set 1 stream.
 *
 * @param decoder Stateful decoder.
 * @param scancode Next raw PS/2 byte.
 * @param event Receives a normalized key event when one is completed.
 *
 * @return true when an event was produced; false when the byte was
 *         only part of a sequence or represents an unsupported key.
 */
bool ps2_scancode_set1_decode(
    struct ps2_scancode_set1_decoder *decoder,
    uint8_t scancode,
    struct input_event *event
);

#endif
