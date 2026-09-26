// SPDX-License-Identifier: GPL-2.0-only

#include "arch.h"

#include "i8042.h"

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

static _Noreturn void reset_via_triple_fault(void);

static _Noreturn void reset_via_triple_fault(void)
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
     if (i8042_wait_input_empty()) {
        i8042_command_write(
            I8042_CPU_RESET_COMMAND
        );

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
