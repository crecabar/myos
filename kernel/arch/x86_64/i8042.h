// SPDX-License-Identifier: GPL-2.0-only

#ifndef MYOS_ARCH_X86_64_I8042_H
#define MYOS_ARCH_X86_64_I8042_H

#include <stdbool.h>
#include <stdint.h>

#define I8042_STATUS_OUTPUT_FULL (1U << 0)
#define I8042_STATUS_INPUT_FULL  (1U << 1)
#define I8042_STATUS_AUXILIARY   (1U << 5)
#define I8042_STATUS_TIMEOUT     (1U << 6)
#define I8042_STATUS_PARITY      (1U << 7)

bool i8042_wait_input_empty(void);
bool i8042_wait_output_full(void);

uint8_t i8042_status_read(void);
uint8_t i8042_data_read(void);

bool i8042_command_write(
    uint8_t command
);

bool i8042_data_write(
    uint8_t value
);

void i8042_flush_output(void);

#endif
