// SPDX-License-Identifier: GPL-2.0-only

#include "ps2_scancode_set1_test.h"

#include "../arch/x86_64/ps2_scancode_set1.h"
#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"

static void expect_key(
    struct ps2_scancode_set1_decoder *decoder,
    uint8_t byte,
    enum input_key_code code,
    enum input_key_state state
);

static void expect_no_event(
    struct ps2_scancode_set1_decoder *decoder,
    uint8_t byte
);

static void expect_key(
    struct ps2_scancode_set1_decoder *decoder,
    uint8_t byte,
    enum input_key_code code,
    enum input_key_state state)
{
    struct input_event event;

    if (
        !ps2_scancode_set1_decode(
            decoder,
            byte,
            &event
        ) ||
        event.type != INPUT_EVENT_KEY ||
        event.key.code != code ||
        event.key.state != state
    ) {
        kernel_panic(
            "PS/2 Set 1 decoder produced incorrect key event"
        );
    }
}

static void expect_no_event(
    struct ps2_scancode_set1_decoder *decoder,
    uint8_t byte)
{
    struct input_event event;

    if (
        ps2_scancode_set1_decode(
            decoder,
            byte,
            &event
        )
    ) {
        kernel_panic(
            "PS/2 Set 1 decoder produced unexpected event"
        );
    }
}

void ps2_scancode_set1_test_run(void)
{
    struct ps2_scancode_set1_decoder decoder;

    ps2_scancode_set1_init(
        &decoder
    );

    /*
     * Ordinary make/break pair.
     */
    expect_key(
        &decoder,
        0x1EU,
        INPUT_KEY_A,
        INPUT_KEY_PRESSED
    );

    expect_key(
        &decoder,
        0x9EU,
        INPUT_KEY_A,
        INPUT_KEY_RELEASED
    );

    /*
     * Number row and punctuation.
     */
    expect_key(
        &decoder,
        0x02U,
        INPUT_KEY_1,
        INPUT_KEY_PRESSED
    );

    expect_key(
        &decoder,
        0x0CU,
        INPUT_KEY_MINUS,
        INPUT_KEY_PRESSED
    );

    /*
     * Distinguish ordinary keypad bytes from E0 navigation keys.
     */
    expect_key(
        &decoder,
        0x47U,
        INPUT_KEY_KEYPAD_7,
        INPUT_KEY_PRESSED
    );

    expect_no_event(
        &decoder,
        0xE0U
    );

    expect_key(
        &decoder,
        0x47U,
        INPUT_KEY_HOME,
        INPUT_KEY_PRESSED
    );

    /*
     * Extended Delete make/break.
     */
    expect_no_event(
        &decoder,
        0xE0U
    );

    expect_key(
        &decoder,
        0x53U,
        INPUT_KEY_DELETE,
        INPUT_KEY_PRESSED
    );

    expect_no_event(
        &decoder,
        0xE0U
    );

    expect_key(
        &decoder,
        0xD3U,
        INPUT_KEY_DELETE,
        INPUT_KEY_RELEASED
    );

    /*
     * ISO extra key is retained as a physical key, independent of
     * whatever keyboard layout will later translate it.
     */
    expect_key(
        &decoder,
        0x56U,
        INPUT_KEY_NON_US_BACKSLASH,
        INPUT_KEY_PRESSED
    );

    /*
     * Pause/Break is deliberately consumed without generating fake
     * Ctrl or NumLock events.
     */
    expect_no_event(&decoder, 0xE1U);
    expect_no_event(&decoder, 0x1DU);
    expect_no_event(&decoder, 0x45U);
    expect_no_event(&decoder, 0xE1U);
    expect_no_event(&decoder, 0x9DU);

    expect_key(
        &decoder,
        0xC5U,
        INPUT_KEY_PAUSE,
        INPUT_KEY_PRESSED
    );

    /*
     * The decoder must recover after the E1 sequence.
     */
    expect_key(
        &decoder,
        0x3BU,
        INPUT_KEY_F1,
        INPUT_KEY_PRESSED
    );

    /*
     * Print Screen make:
     *
     *   E0 2A E0 37
     */
    expect_no_event(&decoder, 0xE0U);
    expect_no_event(&decoder, 0x2AU);
    expect_no_event(&decoder, 0xE0U);

    expect_key(
        &decoder,
        0x37U,
        INPUT_KEY_PRINT_SCREEN,
        INPUT_KEY_PRESSED
    );

    /*
     * Print Screen break:
     *
     *   E0 B7 E0 AA
     */
    expect_no_event(&decoder, 0xE0U);
    expect_no_event(&decoder, 0xB7U);
    expect_no_event(&decoder, 0xE0U);

    expect_key(
        &decoder,
        0xAAU,
        INPUT_KEY_PRINT_SCREEN,
        INPUT_KEY_RELEASED
    );

    /*
     * A malformed special sequence must not leave the decoder stuck.
     */
    expect_no_event(&decoder, 0xE0U);
    expect_no_event(&decoder, 0x2AU);

    expect_key(
        &decoder,
        0x1EU,
        INPUT_KEY_A,
        INPUT_KEY_PRESSED
    );

    diagnostics_write(
        "[ps2] Scan Code Set 1 decoder tests passed\n"
    );
}
