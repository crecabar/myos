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

    INPUT_KEY_ENTER = 0x28,
    INPUT_KEY_ESCAPE = 0x29,
    INPUT_KEY_BACKSPACE = 0x2A,
    INPUT_KEY_TAB = 0x2B,
    INPUT_KEY_SPACE = 0x2C,

    INPUT_KEY_DELETE = 0x4C,
    INPUT_KEY_RIGHT = 0x4F,
    INPUT_KEY_LEFT = 0x50,
    INPUT_KEY_DOWN = 0x51,
    INPUT_KEY_UP = 0x52,

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

enum input_system_action {
    INPUT_SYSTEM_ACTION_RESET = 1
};

typedef void (*input_system_action_handler_fn)(
    enum input_system_action action
);

struct input_key_event {
    enum input_key_code code;
    enum input_key_state state;
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
