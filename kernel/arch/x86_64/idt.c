// SPDX-License-Identifier: GPL-2.0-only

#include <stdint.h>

#include "idt.h"
#include "interrupts.h"
#include "timer.h"
#include "../../core/panic.h"
#include "../../syscall/syscall.h"
#include "../../diagnostics/diagnostics.h"
#include "../../process/process.h"
#include "../../scheduler/scheduler.h"

#define IDT_ENTRIES 256

enum x86_exception_vector {
    X86_EXCEPTION_DIVIDE_ERROR = 0,
    X86_EXCEPTION_PAGE_FAULT = 14,
};

extern void isr_divide_error(void);
extern void isr_page_fault(void);
extern void isr_syscall(void);
extern void isr_timer(void);
extern void isr_spurious(void);

static void idt_set_gate(uint8_t vector, uint64_t handler, uint16_t selector, uint8_t attributes);
static uint64_t read_cr2(void);
static void page_fault_dump(const struct interrupt_context *context);
static bool exception_from_user_mode(const struct interrupt_context *context);

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

    idt_set_gate(
        14,
        (uint64_t) isr_page_fault,
        code_selector,
        0x8E
    );

    idt_set_gate(
        TIMER_INTERRUPT_VECTOR,
        (uint64_t) isr_timer,
        code_selector,
        0x8E
    );

    idt_set_gate(
        0xFF,
        (uint64_t) isr_spurious,
        code_selector,
        0x8E
    );

    idt_set_gate(
        0x80,
        (uint64_t) isr_syscall,
        code_selector,
        0xEE
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

static uint64_t read_cr2(void)
{
    uint64_t value;

    __asm__ volatile (
        "mov %%cr2, %0"
        : "=r"(value)
    );

    return value;
}

static bool exception_from_user_mode(const struct interrupt_context *context)
{
    return (context->cs & 0x3) == 3;
}

static void page_fault_dump(
    const struct interrupt_context *context)
{
    uint64_t error = context->error_code;

    diagnostics_printf(
        "Page fault details:\n"
        "  Address=%x\n"
        "  Present=%s\n"
        "  Access=%s\n"
        "  Mode=%s\n"
        "  Reserved=%s\n"
        "  Instruction=%s\n",
        read_cr2(),
        (error & (1ULL << 0)) ? "yes" : "no",
        (error & (1ULL << 1)) ? "write" : "read",
        (error & (1ULL << 2)) ? "user" : "supervisor",
        (error & (1ULL << 3)) ? "violation" : "no",
        (error & (1ULL << 4)) ? "yes" : "no"
    );
}

_Noreturn void exception_handler(const struct interrupt_context *context)
{
    diagnostics_printf(
        "\n*** CPU EXCEPTION ***\n"
        "Vector=%u\n"
        "Error=%x\n"
        "RIP=%x\n"
        "RAX=%x\n"
        "RBX=%x\n"
        "RCX=%x\n"
        "RDX=%x\n"
        "CS=%x\n"
        "RFLAGS=%x\n",
        context->vector,
        context->error_code,
        context->rip,
        context->rax,
        context->rbx,
        context->rcx,
        context->rdx,
        context->cs,
        context->rflags
    );

    if (context->vector == X86_EXCEPTION_PAGE_FAULT) {
        page_fault_dump(context);

        if (exception_from_user_mode(context)) {
            diagnostics_write(
                "\n[process] Segmentation fault\n"
            );

            scheduler_terminate_current(
                PROCESS_TERMINATION_SEGMENTATION_FAULT
            );
        }
    }

    for (;;) {
        __asm__ volatile ("hlt");
    }
}

void syscall_handler(struct interrupt_context *context)
{
    if (context->rax == SYSCALL_YIELD) {
        scheduler_yield_current(context);

        return;
    }

    if (context->rax == SYSCALL_EXIT) {
        scheduler_exit_current(
            context,
            context->rdi
        );

        return;
    }

    context->rax = syscall_dispatch(
        context->rax,
        context->rdi,
        context->rsi,
        context->rdx,
        context->r10,
        context->r8,
        context->r9
    );
}
