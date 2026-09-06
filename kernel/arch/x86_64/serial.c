// SPDX-License-Identifier: GPL-2.0-only

#include <stdbool.h>
#include <stdint.h>

#include "io.h"
#include "serial.h"

#define COM1_PORT 0x3F8

static bool serial_can_transmit(void)
{
    return (inb(COM1_PORT + 5) & 0x20) != 0;
}

void serial_init(void)
{
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x80);
    outb(COM1_PORT + 0, 0x03);
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x03);
    outb(COM1_PORT + 2, 0xC7);
    outb(COM1_PORT + 4, 0x0B);
}

void serial_write_char(char character)
{
    if (character == '\n') {
        while (!serial_can_transmit()) {
        }

        outb(COM1_PORT, '\r');
    }

    while (!serial_can_transmit()) {
    }

    outb(COM1_PORT, (uint8_t) character);
}

void serial_write_string(const char *string)
{
    while (*string != '\0') {
        serial_write_char(*string);
        ++string;
    }
}
