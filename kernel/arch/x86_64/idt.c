// SPDX-License-Identifier: GPL-2.0-only

#include <stdint.h>

#include "idt.h"
#include "interrupts.h"
#include "timer.h"
#include "../../core/panic.h"
#include "../../diagnostics/diagnostics.h"
#include "../../process/process.h"
#include "../../scheduler/scheduler.h"
#include "../../syscall/syscall.h"

#define IDT_ENTRIES 256

#define IDT_IST_NONE 0
#define IDT_IST_DOUBLE_FAULT 1

enum x86_exception_vector {
    X86_EXCEPTION_DIVIDE_ERROR = 0,
    X86_EXCEPTION_DEBUG = 1,
    X86_INTERRUPT_NMI = 2,
    X86_EXCEPTION_BREAKPOINT = 3,
    X86_EXCEPTION_OVERFLOW = 4,
    X86_EXCEPTION_BOUND_RANGE_EXCEEDED = 5,
    X86_EXCEPTION_INVALID_OPCODE = 6,
    X86_EXCEPTION_DEVICE_NOT_AVAILABLE = 7,
    X86_EXCEPTION_DOUBLE_FAULT = 8,
    X86_EXCEPTION_INVALID_TSS = 10,
    X86_EXCEPTION_SEGMENT_NOT_PRESENT = 11,
    X86_EXCEPTION_STACK_SEGMENT_FAULT = 12,
    X86_EXCEPTION_GENERAL_PROTECTION = 13,
    X86_EXCEPTION_PAGE_FAULT = 14,
    X86_EXCEPTION_X87_FLOATING_POINT = 16,
    X86_EXCEPTION_ALIGNMENT_CHECK = 17,
    X86_EXCEPTION_MACHINE_CHECK = 18,
    X86_EXCEPTION_SIMD_FLOATING_POINT = 19,
    X86_EXCEPTION_VIRTUALIZATION = 20,
    X86_EXCEPTION_CONTROL_PROTECTION = 21
};

extern void isr_divide_error(void);
extern void isr_debug(void);
extern void isr_nmi(void);
extern void isr_breakpoint(void);
extern void isr_overflow(void);
extern void isr_bound_range_exceeded(void);
extern void isr_invalid_opcode(void);
extern void isr_device_not_available(void);
extern void isr_double_fault(void);
extern void isr_invalid_tss(void);
extern void isr_segment_not_present(void);
extern void isr_stack_segment_fault(void);
extern void isr_general_protection(void);
extern void isr_page_fault(void);
extern void isr_x87_floating_point(void);
extern void isr_alignment_check(void);
extern void isr_machine_check(void);
extern void isr_simd_floating_point(void);
extern void isr_virtualization_exception(void);
extern void isr_control_protection(void);

extern void isr_syscall(void);
extern void isr_timer(void);
extern void isr_spurious(void);

static void idt_set_gate(
    uint8_t vector, 
    uint64_t handler, 
    uint16_t selector, 
    uint16_t ist,
    uint8_t attributes
);
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
    uint16_t ist,
    uint8_t attributes)
{
    idt[vector].offset_low = (uint16_t) (handler & 0xFFFF);
    idt[vector].selector = selector;
    idt[vector].ist = (uint8_t) (ist & 0x7);
    idt[vector].type_attributes = attributes;
    idt[vector].offset_middle = (uint16_t) ((handler >> 16) & 0xFFFF);
    idt[vector].offset_high = (uint32_t) ((handler >> 32) & 0xFFFFFFFF);
    idt[vector].zero = 0;
}

void idt_init(void)
{
    uint16_t code_selector;

    __asm__ volatile (
        "mov %%cs, %w0"
        : "=r"(code_selector)
    );

    /*
    * Keep maskable interrupts disabled while installing the IDT.
    *
    * arch_init() enables them after the interrupt controllers and timer routing
    * have been initialized.
    */
    __asm__ volatile ("cli");

    // X86_64 instructions that generate exceptions and interrupts are described in
    // Intel 64 and IA-32 Architectures Software Developer's Manual, Volume 3
    // (SDM), Section 6.15 "Interrupt and Exception Handling".
    // #DE 0
    idt_set_gate(
        X86_EXCEPTION_DIVIDE_ERROR,
        (uint64_t) isr_divide_error,
        code_selector,
        IDT_IST_NONE,
        0x8E
    );

    // #DB 1
    idt_set_gate(
        X86_EXCEPTION_DEBUG,
        (uint64_t) isr_debug,
        code_selector,
        IDT_IST_NONE,
        0x8E
    );

    // NMI 2
    idt_set_gate(
        X86_INTERRUPT_NMI,
        (uint64_t) isr_nmi,
        code_selector,
        IDT_IST_NONE,
        0x8E
    );

    // #BP 3
    idt_set_gate(
        X86_EXCEPTION_BREAKPOINT,
        (uint64_t) isr_breakpoint,
        code_selector,
        IDT_IST_NONE,
        0xEE
    );

    // #OF 4
    idt_set_gate(
        X86_EXCEPTION_OVERFLOW,
        (uint64_t) isr_overflow,
        code_selector,
        IDT_IST_NONE,
        0xEE
    );

    // #BR 5
    idt_set_gate(
        X86_EXCEPTION_BOUND_RANGE_EXCEEDED,
        (uint64_t) isr_bound_range_exceeded,
        code_selector,
        IDT_IST_NONE,
        0x8E
    );

    // #UD 6
    idt_set_gate(
        X86_EXCEPTION_INVALID_OPCODE,
        (uint64_t) isr_invalid_opcode,
        code_selector,
        IDT_IST_NONE,
        0x8E
    );

    // #NM 7
    idt_set_gate(
        X86_EXCEPTION_DEVICE_NOT_AVAILABLE,
        (uint64_t) isr_device_not_available,
        code_selector,
        IDT_IST_NONE,
        0x8E
    );

    // #DF 8
    idt_set_gate(
        X86_EXCEPTION_DOUBLE_FAULT,
        (uint64_t) isr_double_fault,
        code_selector,
        IDT_IST_DOUBLE_FAULT,
        0x8E
    );

    // #TS 10
    idt_set_gate(
        X86_EXCEPTION_INVALID_TSS,
        (uint64_t) isr_invalid_tss,
        code_selector,
        IDT_IST_NONE,
        0x8E
    );

    // #NP 11
    idt_set_gate(
        X86_EXCEPTION_SEGMENT_NOT_PRESENT,
        (uint64_t) isr_segment_not_present,
        code_selector,
        IDT_IST_NONE,
        0x8E
    );

    // #SS 12
    idt_set_gate(
        X86_EXCEPTION_STACK_SEGMENT_FAULT,
        (uint64_t) isr_stack_segment_fault,
        code_selector,
        IDT_IST_NONE,
        0x8E
    );

    // #GP 13
    idt_set_gate(
        X86_EXCEPTION_GENERAL_PROTECTION,
        (uint64_t) isr_general_protection,
        code_selector,
        IDT_IST_NONE,
        0x8E
    );

    // #PF 14
    idt_set_gate(
        X86_EXCEPTION_PAGE_FAULT,
        (uint64_t) isr_page_fault,
        code_selector,
        IDT_IST_NONE,
        0x8E
    );

    // #MF 16
    idt_set_gate(
        X86_EXCEPTION_X87_FLOATING_POINT,
        (uint64_t) isr_x87_floating_point,
        code_selector,
        IDT_IST_NONE,
        0x8E
    );

    // #AC 17
    idt_set_gate(
        X86_EXCEPTION_ALIGNMENT_CHECK,
        (uint64_t) isr_alignment_check,
        code_selector,
        IDT_IST_NONE,
        0x8E
    );

    // #MC 18
    idt_set_gate(
        X86_EXCEPTION_MACHINE_CHECK,
        (uint64_t) isr_machine_check,
        code_selector,
        IDT_IST_NONE,
        0x8E
    );

    // #XM 19
    idt_set_gate(
        X86_EXCEPTION_SIMD_FLOATING_POINT,
        (uint64_t) isr_simd_floating_point,
        code_selector,
        IDT_IST_NONE,
        0x8E
    );

    // #VE 20
    idt_set_gate(
        X86_EXCEPTION_VIRTUALIZATION,
        (uint64_t) isr_virtualization_exception,
        code_selector,
        IDT_IST_NONE,
        0x8E
    );

    // #CP 21
    idt_set_gate(
        X86_EXCEPTION_CONTROL_PROTECTION,
        (uint64_t) isr_control_protection,
        code_selector,
        IDT_IST_NONE,
        0x8E
    );

    // Hardware timer interrupt
    idt_set_gate(
        TIMER_INTERRUPT_VECTOR,
        (uint64_t) isr_timer,
        code_selector,
        IDT_IST_NONE,
        0x8E
    );

    // Local APIC spurious interrupt
    idt_set_gate(
        0xFF,
        (uint64_t) isr_spurious,
        code_selector,
        IDT_IST_NONE,
        0x8E
    );

    // User-callable syscall software interrupt
    idt_set_gate(
        0x80,
        (uint64_t) isr_syscall,
        code_selector,
        IDT_IST_NONE,
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

void exception_handler(struct interrupt_context *context)
{
    if (context->vector == X86_INTERRUPT_NMI) {
        diagnostics_write(
            "\n*** NON-MASKABLE INTERRUPT ***\n"
        );
    } else {
        diagnostics_write(
            "\n*** CPU EXCEPTION ***\n"
        );
    }

    diagnostics_printf(
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

    bool user_mode = exception_from_user_mode(context);

    switch (context->vector) {
        case X86_EXCEPTION_DIVIDE_ERROR: // 0
            if (user_mode) {
                diagnostics_write(
                    "\n[process] Arithmetic fault\n"
                );

                scheduler_terminate_current_from_interrupt(
                    context,
                    PROCESS_TERMINATION_ARITHMETIC_FAULT
                );

                return;
            }

            break;
        
        case X86_EXCEPTION_DEBUG: // 1
            if (user_mode) {
                diagnostics_write(
                    "\n[process] Debug trap\n"
                );
        
                scheduler_terminate_current_from_interrupt(
                    context,
                    PROCESS_TERMINATION_TRAP
                );
        
                return;
            }
        
            break;

        case X86_INTERRUPT_NMI: // 2
            kernel_panic("Non-maskable interrupt");
        
        case X86_EXCEPTION_BREAKPOINT: // 3
            if (user_mode) {
                diagnostics_write(
                    "\n[process] Debug trap\n"
                );
        
                scheduler_terminate_current_from_interrupt(
                    context,
                    PROCESS_TERMINATION_TRAP
                );
        
                return;
            }
        
            break;
        
        case X86_EXCEPTION_OVERFLOW: // 4
            if (user_mode) {
                diagnostics_write(
                    "\n[process] Arithmetic fault\n"
                );
        
                scheduler_terminate_current_from_interrupt(
                    context,
                    PROCESS_TERMINATION_ARITHMETIC_FAULT
                );
        
                return;
            }
        
            break;
        
        case X86_EXCEPTION_BOUND_RANGE_EXCEEDED: // 5
            if (user_mode) {
                diagnostics_write(
                    "\n[process] Protection fault\n"
                );
        
                scheduler_terminate_current_from_interrupt(
                    context,
                    PROCESS_TERMINATION_PROTECTION_FAULT
                );
        
                return;
            }
        
            break;

        case X86_EXCEPTION_INVALID_OPCODE: // 6
            if (user_mode) {
                diagnostics_write(
                    "\n[process] Illegal instruction\n"
                );

                scheduler_terminate_current_from_interrupt(
                    context,
                    PROCESS_TERMINATION_ILLEGAL_INSTRUCTION
                );

                return;
            }

            break;
        
        case X86_EXCEPTION_DEVICE_NOT_AVAILABLE: // 7
            if (user_mode) {
                diagnostics_write(
                    "\n[process] Floating-point/SIMD unavailable\n"
                );
        
                scheduler_terminate_current_from_interrupt(
                    context,
                    PROCESS_TERMINATION_ILLEGAL_INSTRUCTION
                );
        
                return;
            }
        
            break;
        
        case X86_EXCEPTION_DOUBLE_FAULT: // 8
            kernel_panic("Double fault");

        case X86_EXCEPTION_INVALID_TSS: // 10
            kernel_panic("Invalid TSS");

        case X86_EXCEPTION_SEGMENT_NOT_PRESENT: // 11
        case X86_EXCEPTION_STACK_SEGMENT_FAULT: // 12
        case X86_EXCEPTION_GENERAL_PROTECTION: // 13
            if (user_mode) {
                diagnostics_write(
                    "\n[process] Protection fault\n"
                );

                scheduler_terminate_current_from_interrupt(
                    context,
                    PROCESS_TERMINATION_PROTECTION_FAULT
                );

                return;
            }

            break;

        case X86_EXCEPTION_PAGE_FAULT: //14
            page_fault_dump(context);

            if (user_mode) {
                diagnostics_write(
                    "\n[process] Segmentation fault\n"
                );

                scheduler_terminate_current_from_interrupt(
                    context,
                    PROCESS_TERMINATION_SEGMENTATION_FAULT
                );

                return;
            }

            break;
        
        case X86_EXCEPTION_X87_FLOATING_POINT: // 16
            if (user_mode) {
                diagnostics_write(
                    "\n[process] Arithmetic fault\n"
                );
        
                scheduler_terminate_current_from_interrupt(
                    context,
                    PROCESS_TERMINATION_ARITHMETIC_FAULT
                );
        
                return;
            }
        
            break;
        
        case X86_EXCEPTION_ALIGNMENT_CHECK: // 17
            if (user_mode) {
                diagnostics_write(
                    "\n[process] Protection fault\n"
                );
        
                scheduler_terminate_current_from_interrupt(
                    context,
                    PROCESS_TERMINATION_PROTECTION_FAULT
                );
        
                return;
            }
        
            break;
        
        case X86_EXCEPTION_MACHINE_CHECK: // 18
            kernel_panic("Machine check");
        
        case X86_EXCEPTION_SIMD_FLOATING_POINT: // 19
            if (user_mode) {
                diagnostics_write(
                    "\n[process] Arithmetic fault\n"
                );
        
                scheduler_terminate_current_from_interrupt(
                    context,
                    PROCESS_TERMINATION_ARITHMETIC_FAULT
                );
        
                return;
            }
        
            break;
        
        case X86_EXCEPTION_VIRTUALIZATION: // 20
            kernel_panic("Virtualization exception");
        
        case X86_EXCEPTION_CONTROL_PROTECTION: // 21
            if (user_mode) {
                diagnostics_write(
                    "\n[process] Protection fault\n"
                );
        
                scheduler_terminate_current_from_interrupt(
                    context,
                    PROCESS_TERMINATION_PROTECTION_FAULT
                );
        
                return;
            }
        
            break;

        default:
            break;
    }

    kernel_panic("Unhandled or kernel-mode CPU exception");
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
