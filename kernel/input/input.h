// SPDX-License-Identifier: GPL-2.0-only

#ifndef MYOS_INPUT_INPUT_H
#define MYOS_INPUT_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * MyOS keyboard key codes use the USB HID Keyboard/Keypad usage values.
 *
 * This gives PS/2 and future USB HID keyboards one common internal
 * representation.
 */
enum input_key_code {
    INPUT_KEY_NONE = 0x00,

    INPUT_KEY_A = 0x04,
    INPUT_KEY_B = 0x05,
    INPUT_KEY_C = 0x06,
    INPUT_KEY_D = 0x07,
    INPUT_KEY_E = 0x08,
    INPUT_KEY_F = 0x09,
    INPUT_KEY_G = 0x0A,
    INPUT_KEY_H = 0x0B,
    INPUT_KEY_I = 0x0C,
    INPUT_KEY_J = 0x0D,
    INPUT_KEY_K = 0x0E,
    INPUT_KEY_L = 0x0F,
    INPUT_KEY_M = 0x10,
    INPUT_KEY_N = 0x11,
    INPUT_KEY_O = 0x12,
    INPUT_KEY_P = 0x13,
    INPUT_KEY_Q = 0x14,
    INPUT_KEY_R = 0x15,
    INPUT_KEY_S = 0x16,
    INPUT_KEY_T = 0x17,
    INPUT_KEY_U = 0x18,
    INPUT_KEY_V = 0x19,
    INPUT_KEY_W = 0x1A,
    INPUT_KEY_X = 0x1B,
    INPUT_KEY_Y = 0x1C,
    INPUT_KEY_Z = 0x1D,

    INPUT_KEY_1 = 0x1E,
    INPUT_KEY_2 = 0x1F,
    INPUT_KEY_3 = 0x20,
    INPUT_KEY_4 = 0x21,
    INPUT_KEY_5 = 0x22,
    INPUT_KEY_6 = 0x23,
    INPUT_KEY_7 = 0x24,
    INPUT_KEY_8 = 0x25,
    INPUT_KEY_9 = 0x26,
    INPUT_KEY_0 = 0x27,

    INPUT_KEY_ENTER = 0x28,
    INPUT_KEY_ESCAPE = 0x29,
    INPUT_KEY_BACKSPACE = 0x2A,
    INPUT_KEY_TAB = 0x2B,
    INPUT_KEY_SPACE = 0x2C,

    INPUT_KEY_MINUS = 0x2D,
    INPUT_KEY_EQUAL = 0x2E,
    INPUT_KEY_LEFT_BRACKET = 0x2F,
    INPUT_KEY_RIGHT_BRACKET = 0x30,
    INPUT_KEY_BACKSLASH = 0x31,
    INPUT_KEY_SEMICOLON = 0x33,
    INPUT_KEY_APOSTROPHE = 0x34,
    INPUT_KEY_GRAVE = 0x35,
    INPUT_KEY_COMMA = 0x36,
    INPUT_KEY_PERIOD = 0x37,
    INPUT_KEY_SLASH = 0x38,
    INPUT_KEY_CAPS_LOCK = 0x39,

    INPUT_KEY_F1 = 0x3A,
    INPUT_KEY_F2 = 0x3B,
    INPUT_KEY_F3 = 0x3C,
    INPUT_KEY_F4 = 0x3D,
    INPUT_KEY_F5 = 0x3E,
    INPUT_KEY_F6 = 0x3F,
    INPUT_KEY_F7 = 0x40,
    INPUT_KEY_F8 = 0x41,
    INPUT_KEY_F9 = 0x42,
    INPUT_KEY_F10 = 0x43,
    INPUT_KEY_F11 = 0x44,
    INPUT_KEY_F12 = 0x45,

    INPUT_KEY_PRINT_SCREEN = 0x46,
    INPUT_KEY_SCROLL_LOCK = 0x47,
    INPUT_KEY_PAUSE = 0x48,

    INPUT_KEY_INSERT = 0x49,
    INPUT_KEY_HOME = 0x4A,
    INPUT_KEY_PAGE_UP = 0x4B,

    INPUT_KEY_DELETE = 0x4C,
    INPUT_KEY_RIGHT = 0x4F,
    INPUT_KEY_LEFT = 0x50,
    INPUT_KEY_DOWN = 0x51,
    INPUT_KEY_UP = 0x52,

    INPUT_KEY_END = 0x4D,
    INPUT_KEY_PAGE_DOWN = 0x4E,

    INPUT_KEY_NUM_LOCK = 0x53,
    INPUT_KEY_KEYPAD_DIVIDE = 0x54,
    INPUT_KEY_KEYPAD_MULTIPLY = 0x55,
    INPUT_KEY_KEYPAD_MINUS = 0x56,
    INPUT_KEY_KEYPAD_PLUS = 0x57,
    INPUT_KEY_KEYPAD_ENTER = 0x58,
    INPUT_KEY_KEYPAD_1 = 0x59,
    INPUT_KEY_KEYPAD_2 = 0x5A,
    INPUT_KEY_KEYPAD_3 = 0x5B,
    INPUT_KEY_KEYPAD_4 = 0x5C,
    INPUT_KEY_KEYPAD_5 = 0x5D,
    INPUT_KEY_KEYPAD_6 = 0x5E,
    INPUT_KEY_KEYPAD_7 = 0x5F,
    INPUT_KEY_KEYPAD_8 = 0x60,
    INPUT_KEY_KEYPAD_9 = 0x61,
    INPUT_KEY_KEYPAD_0 = 0x62,
    INPUT_KEY_KEYPAD_PERIOD = 0x63,

    /*
     * ISO 102/105-key extra key between Left Shift and Z.
     * Important later for non-US keyboard layouts.
     */
    INPUT_KEY_NON_US_BACKSLASH = 0x64,

    INPUT_KEY_APPLICATION = 0x65,

    INPUT_KEY_LEFT_CTRL = 0xE0,
    INPUT_KEY_LEFT_SHIFT = 0xE1,
    INPUT_KEY_LEFT_ALT = 0xE2,
    INPUT_KEY_LEFT_GUI = 0xE3,
    INPUT_KEY_RIGHT_CTRL = 0xE4,
    INPUT_KEY_RIGHT_SHIFT = 0xE5,
    INPUT_KEY_RIGHT_ALT = 0xE6,
    INPUT_KEY_RIGHT_GUI = 0xE7
};

enum input_event_type {
    INPUT_EVENT_KEY = 1
};

enum input_key_state {
    INPUT_KEY_RELEASED = 0,
    INPUT_KEY_PRESSED = 1
};

enum input_modifier_flags {
    INPUT_MODIFIER_LEFT_CTRL   = 1U << 0,
    INPUT_MODIFIER_LEFT_SHIFT  = 1U << 1,
    INPUT_MODIFIER_LEFT_ALT    = 1U << 2,
    INPUT_MODIFIER_LEFT_GUI    = 1U << 3,
    INPUT_MODIFIER_RIGHT_CTRL  = 1U << 4,
    INPUT_MODIFIER_RIGHT_SHIFT = 1U << 5,
    INPUT_MODIFIER_RIGHT_ALT   = 1U << 6,
    INPUT_MODIFIER_RIGHT_GUI   = 1U << 7
};

enum input_lock_flags {
    INPUT_LOCK_CAPS   = 1U << 0,
    INPUT_LOCK_NUM    = 1U << 1,
    INPUT_LOCK_SCROLL = 1U << 2
};

enum input_system_action {
    INPUT_SYSTEM_ACTION_RESET = 1
};

typedef void (*input_system_action_handler_fn)(
    enum input_system_action action
);

struct input_key_event {
    enum input_key_code code;
    enum input_key_state state;

    /*
     * State snapshot after applying this key event.
     *
     * A modifier make event therefore includes its own modifier bit,
     * while its break event no longer includes it.
     */
    uint16_t modifiers;
    uint8_t locks;
};

struct input_event {
    enum input_event_type type;

    union {
        struct input_key_event key;
    };
};

/**
 * Initializes the kernel input-event queue.
 */
void input_init(void);

/**
 * Submits one normalized input event to the kernel input subsystem.
 *
 * Besides queueing the event, keyboard state and kernel-level input
 * chords are updated before the event is made available to consumers.
 *
 * A system action may still be recognized even if the event queue is
 * full; queue backpressure must not suppress kernel control chords.
 *
 * @return true when the event was queued; false when invalid or when
 *         the queue was full.
 */
bool input_event_submit(
    const struct input_event *event
);

/**
 * Removes the oldest pending input event.
 *
 * @return true when an event was returned; false when empty.
 */
bool input_event_pop(
    struct input_event *event
);

/**
 * Returns the number of currently queued events.
 */
size_t input_event_pending(void);

/**
 * Returns the number of events dropped because the queue was full.
 */
uint64_t input_event_dropped(void);

/**
 * Retrieves and clears one pending kernel-level input action.
 *
 * @param action Receives the pending action.
 *
 * @return true when an action was returned; false when none is pending.
 */
bool input_system_action_take(
    enum input_system_action *action
);

/**
 * Installs the kernel-level system-action handler.
 *
 * Passing NULL disables immediate dispatch while preserving action
 * recognition through input_system_action_take().
 *
 * @return Previously installed handler.
 */
input_system_action_handler_fn input_system_action_handler_set(
    input_system_action_handler_fn handler
);

#endif
