// SPDX-License-Identifier: GPL-2.0-only

#include "ps2_mouse.h"

#include "i8042.h"
#include "lapic.h"

#include "../../diagnostics/diagnostics.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PS2_MOUSE_SET_DEFAULTS     0xF6U
#define PS2_MOUSE_ENABLE_REPORTING 0xF4U

#define PS2_DEVICE_ACK    0xFAU
#define PS2_DEVICE_RESEND 0xFEU

#define PS2_DEVICE_RETRIES        3U
#define PS2_RESPONSE_SCAN_LIMIT  32U

/*
 * Temporary bring-up trace.
 *
 * Once IRQ12 delivery has been demonstrated this disappears and the
 * interrupt path will feed the packet decoder/input queue instead.
 */
#define PS2_MOUSE_TRACE_BYTE_LIMIT 6U

static bool ps2_mouse_initialized;
static uint8_t ps2_mouse_trace_bytes;

static bool ps2_mouse_response_read(
    uint8_t *response
);

static bool ps2_mouse_send_command(
    uint8_t command
);

static bool ps2_mouse_response_read(
    uint8_t *response)
{
    if (response == NULL) {
        return false;
    }

    for (
        uint32_t count = 0;
        count < PS2_RESPONSE_SCAN_LIMIT;
        ++count
    ) {
        if (!i8042_wait_output_full()) {
            return false;
        }

        uint8_t status =
            i8042_status_read();

        uint8_t value =
            i8042_data_read();

        if (
            (
                status &
                (
                    I8042_STATUS_TIMEOUT |
                    I8042_STATUS_PARITY
                )
            ) != 0
        ) {
            continue;
        }

        /*
         * The i8042 output buffer is shared by both ports.
         * Ignore first-port bytes while waiting for the response
         * produced by the mouse.
         */
        if (
            (status & I8042_STATUS_AUXILIARY) == 0
        ) {
            continue;
        }

        *response = value;

        return true;
    }

    return false;
}

static bool ps2_mouse_send_command(
    uint8_t command)
{
    for (
        uint32_t attempt = 0;
        attempt < PS2_DEVICE_RETRIES;
        ++attempt
    ) {
        if (
            !i8042_second_port_data_write(
                command
            )
        ) {
            return false;
        }

        uint8_t response;

        if (
            !ps2_mouse_response_read(
                &response
            )
        ) {
            return false;
        }

        if (response == PS2_DEVICE_ACK) {
            return true;
        }

        if (response != PS2_DEVICE_RESEND) {
            return false;
        }
    }

    return false;
}

bool ps2_mouse_init(void)
{
    if (ps2_mouse_initialized) {
        return true;
    }

    /*
     * Discard firmware leftovers before starting a synchronous
     * command/response exchange with the second-port device.
     */
    i8042_flush_output();

    /*
     * F6 returns the device to the standard PS/2 defaults:
     *
     *   - stream mode
     *   - scaling 1:1
     *   - 100 samples/s
     *   - resolution 4 counts/mm
     *
     * F4 then enables asynchronous packet reporting.
     */
    if (
        !ps2_mouse_send_command(
            PS2_MOUSE_SET_DEFAULTS
        ) ||
        !ps2_mouse_send_command(
            PS2_MOUSE_ENABLE_REPORTING
        )
    ) {
        return false;
    }

    ps2_mouse_trace_bytes = 0;
    ps2_mouse_initialized = true;

    return true;
}

void ps2_mouse_handle_interrupt(
    struct interrupt_context *context)
{
    (void) context;

    uint8_t status =
        i8042_status_read();

    if (
        (status & I8042_STATUS_OUTPUT_FULL) == 0
    ) {
        lapic_send_eoi();
        return;
    }

    /*
     * Always drain the byte that generated the IRQ.
     */
    uint8_t value =
        i8042_data_read();

    bool valid_mouse_byte =
        (status & I8042_STATUS_AUXILIARY) != 0 &&
        (
            status &
            (
                I8042_STATUS_TIMEOUT |
                I8042_STATUS_PARITY
            )
        ) == 0;

    lapic_send_eoi();

    /*
     * Temporary IRQ12 bring-up instrumentation. Limit the output
     * aggressively so ordinary mouse movement cannot flood serial
     * diagnostics from interrupt context.
     */
    if (
        valid_mouse_byte &&
        ps2_mouse_trace_bytes <
            PS2_MOUSE_TRACE_BYTE_LIMIT
    ) {
        diagnostics_printf(
            "[ps2] raw mouse byte=%x\n",
            (uint64_t) value
        );

        ++ps2_mouse_trace_bytes;
    }
}
