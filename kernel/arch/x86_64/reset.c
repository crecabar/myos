// SPDX-License-Identifier: GPL-2.0-only

#include "arch.h"

#include <stdbool.h>
#include <stdint.h>

#define I8042_STATUS_COMMAND_PORT 0x64U

#define I8042_STATUS_INPUT_FULL (1U << 1)
#define I8042_CPU_RESET_COMMAND 0xFEU

#define RESET_WAIT_ITERATIONS   1000000U
#define RESET_SETTLE_ITERATIONS 100000U

struct reset_idt_descriptor {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static uint8_t reset_in8(
    uint16_t port
);

static void reset_out8(
    uint16_t port,
    uint8_t value
);

static bool reset_wait_i8042_ready(void);

static _Noreturn void
reset_via_triple_fault(void);

static uint8_t reset_in8(
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

static void reset_out8(
    uint16_t port,
    uint8_t value)
{
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

static bool reset_wait_i8042_ready(void)
{
    for (
        uint32_t iteration = 0;
        iteration < RESET_WAIT_ITERATIONS;
        ++iteration
    ) {
        uint8_t status =
            reset_in8(
                I8042_STATUS_COMMAND_PORT
            );

        if (
            (status &
             I8042_STATUS_INPUT_FULL) == 0
        ) {
            return true;
        }

        __asm__ volatile ("pause");
    }

    return false;
}

static _Noreturn void
reset_via_triple_fault(void)
{
    struct reset_idt_descriptor descriptor = {
        .limit = 0,
        .base = 0,
    };

    /*
     * With an unusable IDT, generating an exception prevents the CPU
     * from delivering the original fault and its double fault, causing
     * the architectural triple-fault reset path.
     */
    __asm__ volatile (
        "lidt %0"
        :
        : "m"(descriptor)
        : "memory"
    );

    __asm__ volatile ("int3");

    __builtin_unreachable();
}

_Noreturn void arch_reset(void)
{
    /*
     * No ordinary interrupt processing may continue after reset has
     * been requested.
     */
    __asm__ volatile (
        "cli"
        :
        :
        : "memory"
    );

    /*
     * The 8042 command 0xFE requests a pulse on the CPU reset line.
     * QEMU's PS/2 controller implements this path and many PC systems
     * provide it for compatibility.
     */
    if (reset_wait_i8042_ready()) {
        reset_out8(
            I8042_STATUS_COMMAND_PORT,
            I8042_CPU_RESET_COMMAND
        );

        /*
         * Give the platform time to act before invoking the fallback.
         */
        for (
            uint32_t iteration = 0;
            iteration < RESET_SETTLE_ITERATIONS;
            ++iteration
        ) {
            __asm__ volatile ("pause");
        }
    }

    reset_via_triple_fault();
}
