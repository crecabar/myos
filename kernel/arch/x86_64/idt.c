// SPDX-License-Identifier: GPL-2.0-only

#include <stdint.h>

#include "idt.h"
#include "interrupts.h"
#include "../../diagnostics/diagnostics.h"

#define IDT_ENTRIES 256

extern void isr_divide_error(void);

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct idt_descriptor {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

_Static_assert(
    sizeof(struct idt_entry) == 16,
    "struct idt_entry must be 16 bytes"
);

_Static_assert(
    sizeof(struct idt_descriptor) == 10,
    "struct idt_descriptor must be 10 bytes"
);

static struct idt_entry idt[IDT_ENTRIES];

static void idt_set_gate(
    uint8_t vector,
    uint64_t handler,
    uint16_t selector,
    uint8_t attributes)
{
    idt[vector].offset_low =
        (uint16_t) (handler & 0xFFFF);

    idt[vector].selector =
        selector;

    idt[vector].ist =
        0;

    idt[vector].type_attributes =
        attributes;

    idt[vector].offset_middle =
        (uint16_t) ((handler >> 16) & 0xFFFF);

    idt[vector].offset_high =
        (uint32_t) ((handler >> 32) & 0xFFFFFFFF);

    idt[vector].zero =
        0;
}

void idt_init(void)
{
    uint16_t code_selector;

    __asm__ volatile (
        "mov %%cs, %0"
        : "=r"(code_selector)
    );

    /*
     * We do not handle hardware interrupts yet.
     * Disable maskable interrupts while our IDT is incomplete.
     */
    __asm__ volatile ("cli");

    idt_set_gate(
        0,
        (uint64_t) isr_divide_error,
        code_selector,
        0x8E
    );

    struct idt_descriptor descriptor = {
        .limit = (uint16_t) (sizeof(idt) - 1),
        .base = (uint64_t) idt,
    };

    __asm__ volatile (
        "lidt %0"
        :
        : "m"(descriptor)
    );
}

_Noreturn void divide_error_handler(
    const struct interrupt_context *context)
{
    diagnostics_write(
        "\nEXCEPTION: divide error (#DE, vector 0)\n"
    );

    diagnostics_printf(
        "RIP=%x\n"
        "RAX=%x\n"
        "RBX=%x\n"
        "RCX=%x\n"
        "RDX=%x\n"
        "CS=%x\n"
        "RFLAGS=%x\n",
        context->rip,
        context->rax,
        context->rbx,
        context->rcx,
        context->rdx,
        context->cs,
        context->rflags
    );

    for (;;) {
        __asm__ volatile ("hlt");
    }
}
