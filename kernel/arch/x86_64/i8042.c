// SPDX-License-Identifier: GPL-2.0-only

#include "i8042.h"

#include <stddef.h>
#include <stdint.h>

#define I8042_DATA_PORT           0x60U
#define I8042_STATUS_COMMAND_PORT 0x64U

#define I8042_WAIT_ITERATIONS 1000000U
#define I8042_FLUSH_LIMIT     32U

#define I8042_COMMAND_READ_CONFIGURATION   0x20U
#define I8042_COMMAND_WRITE_CONFIGURATION  0x60U
#define I8042_COMMAND_DISABLE_SECOND_PORT  0xA7U
#define I8042_COMMAND_DISABLE_FIRST_PORT   0xADU
#define I8042_COMMAND_ENABLE_FIRST_PORT    0xAEU
#define I8042_COMMAND_ENABLE_SECOND_PORT    0xA8U
#define I8042_COMMAND_WRITE_SECOND_PORT     0xD4U

#define I8042_CONFIG_FIRST_PORT_IRQ       (1U << 0)
#define I8042_CONFIG_SECOND_PORT_IRQ      (1U << 1)
#define I8042_CONFIG_FIRST_PORT_DISABLED  (1U << 4)
#define I8042_CONFIG_SECOND_PORT_DISABLED (1U << 5)
#define I8042_CONFIG_TRANSLATION          (1U << 6)

// PRIVATE HELPERS AND FUNCTIONS DECLARATIONS
static uint8_t i8042_in8(
    uint16_t port
);

static void i8042_out8(
    uint16_t port,
    uint8_t value
);

static bool i8042_configuration_read(
    uint8_t *configuration
);

static bool i8042_configuration_write(
    uint8_t configuration
);

// PUBLIC FUNCIONS IMPLEMENTATIONS
uint8_t i8042_status_read(void)
{
    return i8042_in8(
        I8042_STATUS_COMMAND_PORT
    );
}

uint8_t i8042_data_read(void)
{
    return i8042_in8(
        I8042_DATA_PORT
    );
}

bool i8042_wait_input_empty(void)
{
    for (
        uint32_t iteration = 0;
        iteration < I8042_WAIT_ITERATIONS;
        ++iteration
    ) {
        if (
            (
                i8042_status_read() &
                I8042_STATUS_INPUT_FULL
            ) == 0
        ) {
            return true;
        }

        __asm__ volatile ("pause");
    }

    return false;
}

bool i8042_wait_output_full(void)
{
    for (
        uint32_t iteration = 0;
        iteration < I8042_WAIT_ITERATIONS;
        ++iteration
    ) {
        if (
            (
                i8042_status_read() &
                I8042_STATUS_OUTPUT_FULL
            ) != 0
        ) {
            return true;
        }

        __asm__ volatile ("pause");
    }

    return false;
}

bool i8042_command_write(
    uint8_t command)
{
    if (!i8042_wait_input_empty()) {
        return false;
    }

    i8042_out8(
        I8042_STATUS_COMMAND_PORT,
        command
    );

    return true;
}

bool i8042_data_write(
    uint8_t value)
{
    if (!i8042_wait_input_empty()) {
        return false;
    }

    i8042_out8(
        I8042_DATA_PORT,
        value
    );

    return true;
}

void i8042_flush_output(void)
{
    for (
        uint32_t count = 0;
        count < I8042_FLUSH_LIMIT;
        ++count
    ) {
        if (
            (
                i8042_status_read() &
                I8042_STATUS_OUTPUT_FULL
            ) == 0
        ) {
            return;
        }

        (void) i8042_data_read();
    }
}

bool i8042_init(void)
{
    /*
     * Establish controller ownership independently of whatever state
     * firmware left behind.
     *
     * Keep device IRQs disabled until their IOAPIC routes exist.
     */
    if (
        !i8042_command_write(
            I8042_COMMAND_DISABLE_FIRST_PORT
        ) ||
        !i8042_command_write(
            I8042_COMMAND_DISABLE_SECOND_PORT
        )
    ) {
        return false;
    }

    i8042_flush_output();

    uint8_t configuration;

    if (
        !i8042_configuration_read(
            &configuration
        )
    ) {
        return false;
    }

    configuration &=
        (uint8_t) ~(
            I8042_CONFIG_FIRST_PORT_IRQ |
            I8042_CONFIG_SECOND_PORT_IRQ
        );

    /*
     * MyOS currently receives translated Set 1 bytes from the
     * first port while the keyboard itself runs native Set 2.
     */
    configuration |=
        I8042_CONFIG_TRANSLATION;

    if (
        !i8042_configuration_write(
            configuration
        )
    ) {
        return false;
    }

    if (
        !i8042_command_write(
            I8042_COMMAND_ENABLE_FIRST_PORT
        )
    ) {
        return false;
    }

    return true;
}

bool i8042_first_port_interrupt_enable(void)
{
    uint8_t configuration;

    if (
        !i8042_configuration_read(
            &configuration
        )
    ) {
        return false;
    }

    configuration |=
        I8042_CONFIG_FIRST_PORT_IRQ;

    configuration &=
        (uint8_t)
        ~I8042_CONFIG_FIRST_PORT_DISABLED;

    return i8042_configuration_write(
        configuration
    );
}

bool i8042_second_port_enable(void)
{
    return i8042_command_write(
        I8042_COMMAND_ENABLE_SECOND_PORT
    );
}

bool i8042_second_port_interrupt_enable(void)
{
    uint8_t configuration;

    if (
        !i8042_configuration_read(
            &configuration
        )
    ) {
        return false;
    }

    configuration |=
        I8042_CONFIG_SECOND_PORT_IRQ;

    configuration &=
        (uint8_t)
        ~I8042_CONFIG_SECOND_PORT_DISABLED;

    return i8042_configuration_write(
        configuration
    );
}

bool i8042_second_port_data_write(
    uint8_t value)
{
    if (
        !i8042_command_write(
            I8042_COMMAND_WRITE_SECOND_PORT
        )
    ) {
        return false;
    }

    return i8042_data_write(
        value
    );
}

// PRIVATE HELPERS AND FUNCTIONS IMPLEMENTATIONS
static uint8_t i8042_in8(
    uint16_t port)
{
    uint8_t value;

    __asm__ volatile (
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

static void i8042_out8(
    uint16_t port,
    uint8_t value)
{
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

static bool i8042_configuration_read(
    uint8_t *configuration)
{
    if (configuration == NULL) {
        return false;
    }

    if (
        !i8042_command_write(
            I8042_COMMAND_READ_CONFIGURATION
        ) ||
        !i8042_wait_output_full()
    ) {
        return false;
    }

    *configuration =
        i8042_data_read();

    return true;
}

static bool i8042_configuration_write(
    uint8_t configuration)
{
    return
        i8042_command_write(
            I8042_COMMAND_WRITE_CONFIGURATION
        ) &&
        i8042_data_write(
            configuration
        );
}
