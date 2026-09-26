// SPDX-License-Identifier: GPL-2.0-only

#include "keyboard_text.h"

#include <stddef.h>
#include <stdint.h>

// PRIVATE HELPERS AND FUNCTIONS DECLARATIONS
static bool keyboard_shift_active(
    const struct input_key_event *event
);

// PUBLIC FUNCTIONS IMPLEMENTATIONS
bool keyboard_text_from_key_event(
    const struct input_key_event *event,
    char *character)
{
    if (
        event == NULL ||
        character == NULL ||
        event->state != INPUT_KEY_PRESSED
    ) {
        return false;
    }

    if (
        event->code >= INPUT_KEY_A &&
        event->code <= INPUT_KEY_Z
    ) {
        char value =
            (char) (
                'a' +
                (
                    event->code -
                    INPUT_KEY_A
                )
            );

        bool shifted =
            keyboard_shift_active(event);

        bool caps =
            (event->locks & INPUT_LOCK_CAPS) != 0;

        if (shifted != caps) {
            value =
                (char) (
                    'A' +
                    (
                        event->code -
                        INPUT_KEY_A
                    )
                );
        }

        *character = value;

        return true;
    }

    /*
     * Unshifted number-row digits are common to the layouts
     * MyOS currently targets. Shifted symbols are deliberately
     * deferred to the real keyboard-layout layer.
     */
    if (!keyboard_shift_active(event)) {
        if (
            event->code >= INPUT_KEY_1 &&
            event->code <= INPUT_KEY_9
        ) {
            *character =
                (char) (
                    '1' +
                    (
                        event->code -
                        INPUT_KEY_1
                    )
                );

            return true;
        }

        if (event->code == INPUT_KEY_0) {
            *character = '0';
            return true;
        }
    }

    switch (event->code) {
        case INPUT_KEY_SPACE:
            *character = ' ';
            return true;

        case INPUT_KEY_ENTER:
            *character = '\n';
            return true;

        case INPUT_KEY_TAB:
            *character = '\t';
            return true;

        case INPUT_KEY_BACKSPACE:
            *character = '\b';
            return true;

        default:
            return false;
    }
}

// PRIVATE HELPERS AND FUNCTIONS IMPLEMENTATIONS
static bool keyboard_shift_active(
    const struct input_key_event *event)
{
    const uint16_t shift_mask =
        INPUT_MODIFIER_LEFT_SHIFT |
        INPUT_MODIFIER_RIGHT_SHIFT;

    return
        (event->modifiers & shift_mask) != 0;
}
