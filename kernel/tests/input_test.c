// SPDX-License-Identifier: GPL-2.0-only

#include "input_test.h"

#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../input/input.h"

#include <stddef.h>

static uint64_t input_test_action_count;
static enum input_system_action
    input_test_last_action;

// PRIVATE HELPERS DECLARATIONS
static void input_test_submit_key(
    enum input_key_code code,
    enum input_key_state state
);
static void input_test_action_handler(
    enum input_system_action action);

// PUBLIC FUNCTIONS IMPLEMENTATIONS
void input_test_run(void)
{
    input_init();

    input_test_action_count = 0;

    input_system_action_handler_set(
        input_test_action_handler
    );

    if (
        input_event_pending() != 0 ||
        input_event_dropped() != 0
    ) {
        kernel_panic(
            "Input queue was not empty after initialization"
        );
    }

    /*
     * Verify FIFO event delivery.
     */
    input_test_submit_key(
        INPUT_KEY_A,
        INPUT_KEY_PRESSED
    );

    input_test_submit_key(
        INPUT_KEY_A,
        INPUT_KEY_RELEASED
    );

    if (input_event_pending() != 2U) {
        kernel_panic(
            "Input queue pending count is incorrect"
        );
    }

    struct input_event event;

    if (
        !input_event_pop(&event) ||
        event.type != INPUT_EVENT_KEY ||
        event.key.code != INPUT_KEY_A ||
        event.key.state != INPUT_KEY_PRESSED
    ) {
        kernel_panic(
            "Input queue returned incorrect key press"
        );
    }

    if (
        !input_event_pop(&event) ||
        event.type != INPUT_EVENT_KEY ||
        event.key.code != INPUT_KEY_A ||
        event.key.state != INPUT_KEY_RELEASED
    ) {
        kernel_panic(
            "Input queue returned incorrect key release"
        );
    }

    if (
        input_event_pop(&event) ||
        input_event_pending() != 0
    ) {
        kernel_panic(
            "Input queue did not become empty"
        );
    }

    /*
     * Delete alone must not request a system action.
     */
    input_test_submit_key(
        INPUT_KEY_DELETE,
        INPUT_KEY_PRESSED
    );

    enum input_system_action action;

    if (input_system_action_take(&action)) {
        kernel_panic(
            "Delete alone triggered a system action"
        );
    }

    input_test_submit_key(
        INPUT_KEY_DELETE,
        INPUT_KEY_RELEASED
    );

    /*
     * Ctrl+Alt+Delete must request reset independently of
     * the physical keyboard transport.
     */
    input_test_submit_key(
        INPUT_KEY_LEFT_CTRL,
        INPUT_KEY_PRESSED
    );

    input_test_submit_key(
        INPUT_KEY_LEFT_ALT,
        INPUT_KEY_PRESSED
    );

    input_test_submit_key(
        INPUT_KEY_DELETE,
        INPUT_KEY_PRESSED
    );

    if (
        !input_system_action_take(&action) ||
        action != INPUT_SYSTEM_ACTION_RESET
    ) {
        kernel_panic(
            "Ctrl+Alt+Delete did not request reset"
        );
    }

    if (
        input_test_action_count != 1 ||
        input_test_last_action !=
            INPUT_SYSTEM_ACTION_RESET
    ) {
        kernel_panic(
            "Input system-action handler was not invoked"
        );
    }

    /*
     * Taking the action must clear it.
     */
    if (input_system_action_take(&action)) {
        kernel_panic(
            "Input system action was not consumed"
        );
    }

    input_test_submit_key(
        INPUT_KEY_DELETE,
        INPUT_KEY_RELEASED
    );

    input_test_submit_key(
        INPUT_KEY_LEFT_ALT,
        INPUT_KEY_RELEASED
    );

    input_test_submit_key(
        INPUT_KEY_LEFT_CTRL,
        INPUT_KEY_RELEASED
    );

    /*
     * Right-side modifiers must behave identically.
     */
    input_test_submit_key(
        INPUT_KEY_RIGHT_CTRL,
        INPUT_KEY_PRESSED
    );

    input_test_submit_key(
        INPUT_KEY_RIGHT_ALT,
        INPUT_KEY_PRESSED
    );

    input_test_submit_key(
        INPUT_KEY_DELETE,
        INPUT_KEY_PRESSED
    );

    if (
        !input_system_action_take(&action) ||
        action != INPUT_SYSTEM_ACTION_RESET
    ) {
        kernel_panic(
            "Right Ctrl+Alt+Delete did not request reset"
        );
    }

    if (
        input_test_action_count != 2 ||
        input_test_last_action !=
            INPUT_SYSTEM_ACTION_RESET
    ) {
        kernel_panic(
            "Input system-action handler invocation count is incorrect"
        );
    }

    input_system_action_handler_set(NULL);

    /*
     * Restore a clean input subsystem for the rest of the kernel
     * test execution.
     */
    input_init();

    diagnostics_write(
        "[input] Event queue and Ctrl+Alt+Delete tests passed\n"
    );
}

// PRIVATE HELPERS IMPLEMENTATIONS
static void input_test_submit_key(
    enum input_key_code code,
    enum input_key_state state)
{
    struct input_event event = {
        .type = INPUT_EVENT_KEY,
        .key = {
            .code = code,
            .state = state,
        },
    };

    if (!input_event_submit(&event)) {
        kernel_panic(
            "Input test event could not be submitted"
        );
    }
}

static void input_test_action_handler(
    enum input_system_action action)
{
    ++input_test_action_count;
    input_test_last_action = action;
}
