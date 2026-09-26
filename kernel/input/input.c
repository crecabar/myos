// SPDX-License-Identifier: GPL-2.0-only

#include "input.h"

#include <stddef.h>
#include <stdint.h>

#define INPUT_EVENT_QUEUE_CAPACITY 128U

static struct input_event event_queue[
    INPUT_EVENT_QUEUE_CAPACITY
];

// Current state --------------------------------------------------------------
static size_t event_head;
static size_t event_tail;

static uint64_t dropped_events;

static input_system_action_handler_fn
    system_action_handler;
//-----------------------------------------------------------------------------

static uint16_t keyboard_modifiers;
static uint8_t keyboard_locks;

static bool reset_action_pending;

// PRIVATE HELPERS DECLARATIONS
static bool input_event_valid(
    const struct input_event *event
);

static void input_key_state_update(
    struct input_key_event *event
);

static uint16_t input_modifier_for_key(
    enum input_key_code code
);

static uint8_t input_lock_for_key(
    enum input_key_code code
);

// PUBLIC FUNCTIONS IMPLEMENTATION
void input_init(void)
{
    __atomic_store_n(
        &event_head,
        0,
        __ATOMIC_RELAXED
    );

    __atomic_store_n(
        &event_tail,
        0,
        __ATOMIC_RELAXED
    );

    __atomic_store_n(
        &dropped_events,
        0,
        __ATOMIC_RELAXED
    );

    __atomic_store_n(
        &system_action_handler,
        NULL,
        __ATOMIC_RELAXED
    );

    keyboard_modifiers = 0;
    keyboard_locks = 0;

    __atomic_store_n(
        &reset_action_pending,
        false,
        __ATOMIC_RELAXED
    );
}

bool input_event_submit(
    const struct input_event *event)
{
    if (!input_event_valid(event)) {
        return false;
    }

    /*
     * Update kernel input state before attempting to queue the
     * event. A full consumer queue must not disable system chords.
     */
    struct input_event normalized_event =
        *event;

    if (
        normalized_event.type ==
        INPUT_EVENT_KEY
    ) {
        input_key_state_update(
            &normalized_event.key
        );
    }

    size_t head =
        __atomic_load_n(
            &event_head,
            __ATOMIC_RELAXED
        );

    size_t next =
        (head + 1U) %
        INPUT_EVENT_QUEUE_CAPACITY;

    size_t tail =
        __atomic_load_n(
            &event_tail,
            __ATOMIC_ACQUIRE
        );

    if (next == tail) {
        __atomic_fetch_add(
            &dropped_events,
            1,
            __ATOMIC_RELAXED
        );

        return false;
    }

    event_queue[head] =
        normalized_event;

    __atomic_store_n(
        &event_head,
        next,
        __ATOMIC_RELEASE
    );

    return true;
}

bool input_event_pop(
    struct input_event *event)
{
    if (event == NULL) {
        return false;
    }

    size_t tail =
        __atomic_load_n(
            &event_tail,
            __ATOMIC_RELAXED
        );

    size_t head =
        __atomic_load_n(
            &event_head,
            __ATOMIC_ACQUIRE
        );

    if (tail == head) {
        return false;
    }

    *event = event_queue[tail];

    size_t next =
        (tail + 1U) %
        INPUT_EVENT_QUEUE_CAPACITY;

    __atomic_store_n(
        &event_tail,
        next,
        __ATOMIC_RELEASE
    );

    return true;
}

size_t input_event_pending(void)
{
    size_t head =
        __atomic_load_n(
            &event_head,
            __ATOMIC_ACQUIRE
        );

    size_t tail =
        __atomic_load_n(
            &event_tail,
            __ATOMIC_ACQUIRE
        );

    if (head >= tail) {
        return head - tail;
    }

    return
        INPUT_EVENT_QUEUE_CAPACITY -
        tail +
        head;
}

uint64_t input_event_dropped(void)
{
    return __atomic_load_n(
        &dropped_events,
        __ATOMIC_RELAXED
    );
}

bool input_system_action_take(
    enum input_system_action *action)
{
    if (action == NULL) {
        return false;
    }

    bool pending =
        __atomic_exchange_n(
            &reset_action_pending,
            false,
            __ATOMIC_ACQ_REL
        );

    if (!pending) {
        return false;
    }

    *action =
        INPUT_SYSTEM_ACTION_RESET;

    return true;
}

input_system_action_handler_fn input_system_action_handler_set(
    input_system_action_handler_fn handler)
{
    return __atomic_exchange_n(
        &system_action_handler,
        handler,
        __ATOMIC_ACQ_REL
    );
}

// PRIVATE HELPERS IMPLEMENTATION
static bool input_event_valid(
    const struct input_event *event)
{
    if (event == NULL) {
        return false;
    }

    switch (event->type) {
        case INPUT_EVENT_KEY:
            if (
                event->key.code ==
                INPUT_KEY_NONE
            ) {
                return false;
            }

            return
                event->key.state ==
                    INPUT_KEY_PRESSED ||
                event->key.state ==
                    INPUT_KEY_RELEASED;

        case INPUT_EVENT_POINTER:
        {
            const uint8_t valid_buttons =
                INPUT_POINTER_BUTTON_LEFT |
                INPUT_POINTER_BUTTON_RIGHT |
                INPUT_POINTER_BUTTON_MIDDLE;

            return
                (
                    event->pointer.buttons &
                    (uint8_t) ~valid_buttons
                ) == 0;
        }

        default:
            return false;
    }
}

static void input_key_state_update(
    struct input_key_event *event)
{
    bool pressed =
        event->state == INPUT_KEY_PRESSED;

    uint16_t modifier =
        input_modifier_for_key(
            event->code
        );

    if (modifier != 0) {
        if (pressed) {
            keyboard_modifiers |=
                modifier;
        } else {
            keyboard_modifiers &=
                (uint16_t) ~modifier;
        }
    }

    uint8_t lock =
        input_lock_for_key(
            event->code
        );

    /*
     * Lock keys toggle only on make. The break event preserves
     * the state established by the preceding make event.
     */
    if (
        lock != 0 &&
        pressed
    ) {
        keyboard_locks ^=
            lock;
    }

    event->modifiers =
        keyboard_modifiers;

    event->locks =
        keyboard_locks;

    /*
     * Ctrl+Alt+Delete is transport independent. It operates on
     * normalized key state whether the event originated in PS/2,
     * USB HID or a future input driver.
     */
    if (
        event->code != INPUT_KEY_DELETE ||
        !pressed
    ) {
        return;
    }

    const uint16_t ctrl_mask =
        INPUT_MODIFIER_LEFT_CTRL |
        INPUT_MODIFIER_RIGHT_CTRL;

    const uint16_t alt_mask =
        INPUT_MODIFIER_LEFT_ALT |
        INPUT_MODIFIER_RIGHT_ALT;

    if (
        (event->modifiers & ctrl_mask) == 0 ||
        (event->modifiers & alt_mask) == 0
    ) {
        return;
    }

    __atomic_store_n(
        &reset_action_pending,
        true,
        __ATOMIC_RELEASE
    );

    input_system_action_handler_fn handler =
        __atomic_load_n(
            &system_action_handler,
            __ATOMIC_ACQUIRE
        );

    if (handler != NULL) {
        handler(
            INPUT_SYSTEM_ACTION_RESET
        );
    }
}

static uint16_t input_modifier_for_key(
    enum input_key_code code)
{
    switch (code) {
        case INPUT_KEY_LEFT_CTRL:
            return INPUT_MODIFIER_LEFT_CTRL;

        case INPUT_KEY_LEFT_SHIFT:
            return INPUT_MODIFIER_LEFT_SHIFT;

        case INPUT_KEY_LEFT_ALT:
            return INPUT_MODIFIER_LEFT_ALT;

        case INPUT_KEY_LEFT_GUI:
            return INPUT_MODIFIER_LEFT_GUI;

        case INPUT_KEY_RIGHT_CTRL:
            return INPUT_MODIFIER_RIGHT_CTRL;

        case INPUT_KEY_RIGHT_SHIFT:
            return INPUT_MODIFIER_RIGHT_SHIFT;

        case INPUT_KEY_RIGHT_ALT:
            return INPUT_MODIFIER_RIGHT_ALT;

        case INPUT_KEY_RIGHT_GUI:
            return INPUT_MODIFIER_RIGHT_GUI;

        default:
            return 0;
    }
}

static uint8_t input_lock_for_key(
    enum input_key_code code)
{
    switch (code) {
        case INPUT_KEY_CAPS_LOCK:
            return INPUT_LOCK_CAPS;

        case INPUT_KEY_NUM_LOCK:
            return INPUT_LOCK_NUM;

        case INPUT_KEY_SCROLL_LOCK:
            return INPUT_LOCK_SCROLL;

        default:
            return 0;
    }
}
