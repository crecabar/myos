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

static bool left_ctrl_down;
static bool right_ctrl_down;

static bool left_alt_down;
static bool right_alt_down;

static bool reset_action_pending;

// PRIVATE HELPERS DECLARATIONS
static bool input_event_valid(
    const struct input_event *event
);

static void input_key_state_update(
    const struct input_key_event *event
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

    left_ctrl_down = false;
    right_ctrl_down = false;

    left_alt_down = false;
    right_alt_down = false;

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
    input_key_state_update(
        &event->key
    );

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

    event_queue[head] = *event;

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

    if (event->type != INPUT_EVENT_KEY) {
        return false;
    }

    if (event->key.code == INPUT_KEY_NONE) {
        return false;
    }

    if (
        event->key.state != INPUT_KEY_PRESSED &&
        event->key.state != INPUT_KEY_RELEASED
    ) {
        return false;
    }

    return true;
}

static void input_key_state_update(
    const struct input_key_event *event)
{
    bool pressed =
        event->state == INPUT_KEY_PRESSED;

    switch (event->code) {
        case INPUT_KEY_LEFT_CTRL:
            left_ctrl_down = pressed;
            return;

        case INPUT_KEY_RIGHT_CTRL:
            right_ctrl_down = pressed;
            return;

        case INPUT_KEY_LEFT_ALT:
            left_alt_down = pressed;
            return;

        case INPUT_KEY_RIGHT_ALT:
            right_alt_down = pressed;
            return;

        case INPUT_KEY_DELETE:
            break;

        default:
            return;
    }

    /*
     * Ctrl+Alt+Delete is recognized only on the Delete make event.
     * Autorepeat may request the action again, which is harmless while
     * the action is represented as a single pending bit.
     */
    if (
        !pressed ||
        (!left_ctrl_down && !right_ctrl_down) ||
        (!left_alt_down && !right_alt_down)
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
