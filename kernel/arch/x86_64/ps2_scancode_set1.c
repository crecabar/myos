// SPDX-License-Identifier: GPL-2.0-only

#include "ps2_scancode_set1.h"

#include <stddef.h>

static enum input_key_code
ps2_scancode_set1_base_key(
    uint8_t code
);

static enum input_key_code
ps2_scancode_set1_extended_key(
    uint8_t code
);

void ps2_scancode_set1_init(
    struct ps2_scancode_set1_decoder *decoder)
{
    if (decoder == NULL) {
        return;
    }

    decoder->state = PS2_SET1_STATE_BASE;
}

bool ps2_scancode_set1_decode(
    struct ps2_scancode_set1_decoder *decoder,
    uint8_t scancode,
    struct input_event *event)
{
    if (
        decoder == NULL ||
        event == NULL
    ) {
        return false;
    }

retry:
    switch (decoder->state) {
        case PS2_SET1_STATE_BASE:
            if (scancode == 0xE0U) {
                decoder->state =
                    PS2_SET1_STATE_E0;

                return false;
            }

            if (scancode == 0xE1U) {
                decoder->state =
                    PS2_SET1_STATE_PAUSE_1D;

                return false;
            }

            break;

        case PS2_SET1_STATE_E0:
            /*
             * Print Screen make:
             *
             *   E0 2A E0 37
             */
            if (scancode == 0x2AU) {
                decoder->state =
                    PS2_SET1_STATE_PRINT_MAKE_E0;

                return false;
            }

            /*
             * Print Screen break:
             *
             *   E0 B7 E0 AA
             */
            if (scancode == 0xB7U) {
                decoder->state =
                    PS2_SET1_STATE_PRINT_BREAK_E0;

                return false;
            }

            decoder->state =
                PS2_SET1_STATE_BASE;

            {
                bool released =
                    (scancode & 0x80U) != 0;

                uint8_t make_code =
                    scancode & 0x7FU;

                enum input_key_code key =
                    ps2_scancode_set1_extended_key(
                        make_code
                    );

                if (key == INPUT_KEY_NONE) {
                    return false;
                }

                *event = (struct input_event) {
                    .type = INPUT_EVENT_KEY,
                    .key = {
                        .code = key,
                        .state = released
                            ? INPUT_KEY_RELEASED
                            : INPUT_KEY_PRESSED,
                    },
                };

                return true;
            }

        case PS2_SET1_STATE_PRINT_MAKE_E0:
            if (scancode == 0xE0U) {
                decoder->state =
                    PS2_SET1_STATE_PRINT_MAKE_37;

                return false;
            }

            decoder->state =
                PS2_SET1_STATE_BASE;

            goto retry;

        case PS2_SET1_STATE_PRINT_MAKE_37:
            decoder->state =
                PS2_SET1_STATE_BASE;

            if (scancode != 0x37U) {
                goto retry;
            }

            *event = (struct input_event) {
                .type = INPUT_EVENT_KEY,
                .key = {
                    .code =
                        INPUT_KEY_PRINT_SCREEN,
                    .state =
                        INPUT_KEY_PRESSED,
                },
            };

            return true;

        case PS2_SET1_STATE_PRINT_BREAK_E0:
            if (scancode == 0xE0U) {
                decoder->state =
                    PS2_SET1_STATE_PRINT_BREAK_AA;

                return false;
            }

            decoder->state =
                PS2_SET1_STATE_BASE;

            goto retry;

        case PS2_SET1_STATE_PRINT_BREAK_AA:
            decoder->state =
                PS2_SET1_STATE_BASE;

            if (scancode != 0xAAU) {
                goto retry;
            }

            *event = (struct input_event) {
                .type = INPUT_EVENT_KEY,
                .key = {
                    .code =
                        INPUT_KEY_PRINT_SCREEN,
                    .state =
                        INPUT_KEY_RELEASED,
                },
            };

            return true;

        case PS2_SET1_STATE_PAUSE_1D:
            if (scancode == 0x1DU) {
                decoder->state =
                    PS2_SET1_STATE_PAUSE_45;

                return false;
            }

            decoder->state =
                PS2_SET1_STATE_BASE;

            goto retry;

        case PS2_SET1_STATE_PAUSE_45:
            if (scancode == 0x45U) {
                decoder->state =
                    PS2_SET1_STATE_PAUSE_E1;

                return false;
            }

            decoder->state =
                PS2_SET1_STATE_BASE;

            goto retry;

        case PS2_SET1_STATE_PAUSE_E1:
            if (scancode == 0xE1U) {
                decoder->state =
                    PS2_SET1_STATE_PAUSE_9D;

                return false;
            }

            decoder->state =
                PS2_SET1_STATE_BASE;

            goto retry;

        case PS2_SET1_STATE_PAUSE_9D:
            if (scancode == 0x9DU) {
                decoder->state =
                    PS2_SET1_STATE_PAUSE_C5;

                return false;
            }

            decoder->state =
                PS2_SET1_STATE_BASE;

            goto retry;

        case PS2_SET1_STATE_PAUSE_C5:
            decoder->state =
                PS2_SET1_STATE_BASE;

            if (scancode != 0xC5U) {
                goto retry;
            }

            /*
             * Pause/Break has no normal Set 1 break sequence.
             * Emit one pressed event for the complete sequence.
             */
            *event = (struct input_event) {
                .type = INPUT_EVENT_KEY,
                .key = {
                    .code = INPUT_KEY_PAUSE,
                    .state = INPUT_KEY_PRESSED,
                },
            };

            return true;

        default:
            decoder->state =
                PS2_SET1_STATE_BASE;

            return false;
    }

    bool released =
        (scancode & 0x80U) != 0;

    uint8_t make_code =
        scancode & 0x7FU;

    enum input_key_code key =
        ps2_scancode_set1_base_key(
            make_code
        );

    if (key == INPUT_KEY_NONE) {
        return false;
    }

    *event = (struct input_event) {
        .type = INPUT_EVENT_KEY,
        .key = {
            .code = key,
            .state = released
                ? INPUT_KEY_RELEASED
                : INPUT_KEY_PRESSED,
        },
    };

    return true;
}

static enum input_key_code
ps2_scancode_set1_base_key(
    uint8_t code)
{
    switch (code) {
        case 0x01: return INPUT_KEY_ESCAPE;

        case 0x02: return INPUT_KEY_1;
        case 0x03: return INPUT_KEY_2;
        case 0x04: return INPUT_KEY_3;
        case 0x05: return INPUT_KEY_4;
        case 0x06: return INPUT_KEY_5;
        case 0x07: return INPUT_KEY_6;
        case 0x08: return INPUT_KEY_7;
        case 0x09: return INPUT_KEY_8;
        case 0x0A: return INPUT_KEY_9;
        case 0x0B: return INPUT_KEY_0;

        case 0x0C: return INPUT_KEY_MINUS;
        case 0x0D: return INPUT_KEY_EQUAL;
        case 0x0E: return INPUT_KEY_BACKSPACE;
        case 0x0F: return INPUT_KEY_TAB;

        case 0x10: return INPUT_KEY_Q;
        case 0x11: return INPUT_KEY_W;
        case 0x12: return INPUT_KEY_E;
        case 0x13: return INPUT_KEY_R;
        case 0x14: return INPUT_KEY_T;
        case 0x15: return INPUT_KEY_Y;
        case 0x16: return INPUT_KEY_U;
        case 0x17: return INPUT_KEY_I;
        case 0x18: return INPUT_KEY_O;
        case 0x19: return INPUT_KEY_P;

        case 0x1A: return INPUT_KEY_LEFT_BRACKET;
        case 0x1B: return INPUT_KEY_RIGHT_BRACKET;
        case 0x1C: return INPUT_KEY_ENTER;
        case 0x1D: return INPUT_KEY_LEFT_CTRL;

        case 0x1E: return INPUT_KEY_A;
        case 0x1F: return INPUT_KEY_S;
        case 0x20: return INPUT_KEY_D;
        case 0x21: return INPUT_KEY_F;
        case 0x22: return INPUT_KEY_G;
        case 0x23: return INPUT_KEY_H;
        case 0x24: return INPUT_KEY_J;
        case 0x25: return INPUT_KEY_K;
        case 0x26: return INPUT_KEY_L;

        case 0x27: return INPUT_KEY_SEMICOLON;
        case 0x28: return INPUT_KEY_APOSTROPHE;
        case 0x29: return INPUT_KEY_GRAVE;
        case 0x2A: return INPUT_KEY_LEFT_SHIFT;
        case 0x2B: return INPUT_KEY_BACKSLASH;

        case 0x2C: return INPUT_KEY_Z;
        case 0x2D: return INPUT_KEY_X;
        case 0x2E: return INPUT_KEY_C;
        case 0x2F: return INPUT_KEY_V;
        case 0x30: return INPUT_KEY_B;
        case 0x31: return INPUT_KEY_N;
        case 0x32: return INPUT_KEY_M;

        case 0x33: return INPUT_KEY_COMMA;
        case 0x34: return INPUT_KEY_PERIOD;
        case 0x35: return INPUT_KEY_SLASH;
        case 0x36: return INPUT_KEY_RIGHT_SHIFT;
        case 0x37: return INPUT_KEY_KEYPAD_MULTIPLY;
        case 0x38: return INPUT_KEY_LEFT_ALT;
        case 0x39: return INPUT_KEY_SPACE;
        case 0x3A: return INPUT_KEY_CAPS_LOCK;

        case 0x3B: return INPUT_KEY_F1;
        case 0x3C: return INPUT_KEY_F2;
        case 0x3D: return INPUT_KEY_F3;
        case 0x3E: return INPUT_KEY_F4;
        case 0x3F: return INPUT_KEY_F5;
        case 0x40: return INPUT_KEY_F6;
        case 0x41: return INPUT_KEY_F7;
        case 0x42: return INPUT_KEY_F8;
        case 0x43: return INPUT_KEY_F9;
        case 0x44: return INPUT_KEY_F10;

        case 0x45: return INPUT_KEY_NUM_LOCK;
        case 0x46: return INPUT_KEY_SCROLL_LOCK;

        case 0x47: return INPUT_KEY_KEYPAD_7;
        case 0x48: return INPUT_KEY_KEYPAD_8;
        case 0x49: return INPUT_KEY_KEYPAD_9;
        case 0x4A: return INPUT_KEY_KEYPAD_MINUS;
        case 0x4B: return INPUT_KEY_KEYPAD_4;
        case 0x4C: return INPUT_KEY_KEYPAD_5;
        case 0x4D: return INPUT_KEY_KEYPAD_6;
        case 0x4E: return INPUT_KEY_KEYPAD_PLUS;
        case 0x4F: return INPUT_KEY_KEYPAD_1;
        case 0x50: return INPUT_KEY_KEYPAD_2;
        case 0x51: return INPUT_KEY_KEYPAD_3;
        case 0x52: return INPUT_KEY_KEYPAD_0;
        case 0x53: return INPUT_KEY_KEYPAD_PERIOD;

        /*
         * Extra ISO key on 102/105-key keyboards.
         */
        case 0x56:
            return INPUT_KEY_NON_US_BACKSLASH;

        case 0x57: return INPUT_KEY_F11;
        case 0x58: return INPUT_KEY_F12;

        default:
            return INPUT_KEY_NONE;
    }
}

static enum input_key_code
ps2_scancode_set1_extended_key(
    uint8_t code)
{
    switch (code) {
        case 0x1C:
            return INPUT_KEY_KEYPAD_ENTER;

        case 0x1D:
            return INPUT_KEY_RIGHT_CTRL;

        case 0x35:
            return INPUT_KEY_KEYPAD_DIVIDE;

        case 0x38:
            return INPUT_KEY_RIGHT_ALT;

        case 0x47:
            return INPUT_KEY_HOME;

        case 0x48:
            return INPUT_KEY_UP;

        case 0x49:
            return INPUT_KEY_PAGE_UP;

        case 0x4B:
            return INPUT_KEY_LEFT;

        case 0x4D:
            return INPUT_KEY_RIGHT;

        case 0x4F:
            return INPUT_KEY_END;

        case 0x50:
            return INPUT_KEY_DOWN;

        case 0x51:
            return INPUT_KEY_PAGE_DOWN;

        case 0x52:
            return INPUT_KEY_INSERT;

        case 0x53:
            return INPUT_KEY_DELETE;

        case 0x5B:
            return INPUT_KEY_LEFT_GUI;

        case 0x5C:
            return INPUT_KEY_RIGHT_GUI;

        case 0x5D:
            return INPUT_KEY_APPLICATION;

        /*
         * Print Screen uses E0 2A E0 37 / E0 B7 E0 AA.
         * Returning NONE here deliberately avoids producing fake
         * Shift or keypad events until that sequence has its own
         * explicit state-machine support.
         */
        default:
            return INPUT_KEY_NONE;
    }
}
