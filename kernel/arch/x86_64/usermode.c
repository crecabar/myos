// SPDX-License-Identifier: GPL-2.0-only

#include "usermode.h"

#include "gdt.h"

extern _Noreturn void usermode_enter_asm(
    uint64_t instruction_pointer,
    uint64_t stack_pointer,
    uint64_t code_selector,
    uint64_t data_selector
);

_Noreturn void usermode_enter(
    uint64_t instruction_pointer,
    uint64_t stack_pointer)
{
    usermode_enter_asm(
        instruction_pointer,
        stack_pointer,
        GDT_USER_CODE_SELECTOR,
        GDT_USER_DATA_SELECTOR
    );

    __builtin_unreachable();
}
