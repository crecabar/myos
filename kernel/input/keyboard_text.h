// SPDX-License-Identifier: GPL-2.0-only

#ifndef MYOS_INPUT_KEYBOARD_TEXT_H
#define MYOS_INPUT_KEYBOARD_TEXT_H

#include "input.h"

#include <stdbool.h>

/**
 * Converts a normalized keyboard event into a basic text character.
 *
 * This initial translator intentionally handles only keys whose meaning is
 * sufficiently layout-independent for the current bring-up:
 *
 * - A-Z with Shift/Caps Lock
 * - 0-9 without Shift
 * - Space
 * - Enter
 * - Tab
 * - Backspace
 *
 * Punctuation and shifted number-row symbols are intentionally left to a
 * future keyboard-layout implementation.
 *
 * @return true when the event produced a character; false otherwise.
 */
bool keyboard_text_from_key_event(
    const struct input_key_event *event,
    char *character
);

#endif
