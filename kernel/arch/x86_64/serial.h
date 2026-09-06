// SPDX-License-Identifier: GPL-2.0-only

#ifndef MYOS_ARCH_X86_64_SERIAL_H
#define MYOS_ARCH_X86_64_SERIAL_H

void serial_init(void);
void serial_write_char(char character);
void serial_write_string(const char *string);

#endif
