// SPDX-License-Identifier: GPL-2.0-only

#include "gdt.h"
#include "../../diagnostics/diagnostics.h"

#include <stddef.h>
#include <stdint.h>

#define GDT_ENTRY_COUNT 7
#define GDT_KERNEL_STACK_SIZE 16384
#define GDT_DOUBLE_FAULT_STACK_SIZE 16384

#define GDT_KERNEL_CODE_DESCRIPTOR 0x00AF9A000000FFFFULL
#define GDT_KERNEL_DATA_DESCRIPTOR 0x00CF92000000FFFFULL
#define GDT_USER_DATA_DESCRIPTOR   0x00CFF2000000FFFFULL
#define GDT_USER_CODE_DESCRIPTOR   0x00AFFA000000FFFFULL

struct gdt_descriptor {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct task_state_segment {
    uint32_t reserved0;

    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;

    uint64_t reserved1;

    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;

    uint64_t reserved2;

    uint16_t reserved3;
    uint16_t io_map_base;
} __attribute__((packed));

_Static_assert(
    sizeof(struct gdt_descriptor) == 10,
    "struct gdt_descriptor must be 10 bytes"
);

_Static_assert(
    sizeof(struct task_state_segment) == 104,
    "struct task_state_segment must be 104 bytes"
);

static uint64_t gdt[GDT_ENTRY_COUNT];
static struct task_state_segment tss;

static uint8_t kernel_stack[GDT_KERNEL_STACK_SIZE]
    __attribute__((aligned(16)));

static uint8_t double_fault_stack[GDT_DOUBLE_FAULT_STACK_SIZE]
    __attribute__((aligned(16)));

extern void gdt_load(
    const struct gdt_descriptor *descriptor,
    uint16_t code_selector,
    uint16_t data_selector
);

static void gdt_set_tss_descriptor(uint64_t base, uint32_t limit);
static void gdt_load_task_register(uint16_t selector);

static void gdt_set_tss_descriptor(uint64_t base, uint32_t limit)
{
    uint64_t low = 0;

    low |= (uint64_t) (limit & 0xFFFF);
    low |= (base & 0xFFFFFFULL) << 16;
    low |= 0x89ULL << 40;
    low |= ((uint64_t) (limit >> 16) & 0xFULL) << 48;
    low |= ((base >> 24) & 0xFFULL) << 56;

    uint64_t high = base >> 32;

    gdt[5] = low;
    gdt[6] = high;
}

static void gdt_load_task_register(uint16_t selector)
{
    __asm__ volatile (
        "ltr %w0"
        :
        : "r"(selector)
    );
}

void gdt_init(void)
{
    gdt[0] = 0;
    gdt[1] = GDT_KERNEL_CODE_DESCRIPTOR;
    gdt[2] = GDT_KERNEL_DATA_DESCRIPTOR;
    gdt[3] = GDT_USER_DATA_DESCRIPTOR;
    gdt[4] = GDT_USER_CODE_DESCRIPTOR;

    /*
     * tss has static storage duration and is already zero-initialized.
     */
    tss.rsp0 = (uint64_t) &kernel_stack[GDT_KERNEL_STACK_SIZE];

    tss.ist1 = (uint64_t) &double_fault_stack[GDT_DOUBLE_FAULT_STACK_SIZE];

    tss.io_map_base = (uint16_t) sizeof(struct task_state_segment);

    gdt_set_tss_descriptor(
        (uint64_t) &tss,
        (uint32_t) sizeof(struct task_state_segment) - 1
    );

    struct gdt_descriptor descriptor = {
        .limit = (uint16_t) (sizeof(gdt) - 1),
        .base = (uint64_t) gdt,
    };

    gdt_load(
        &descriptor,
        GDT_KERNEL_CODE_SELECTOR,
        GDT_KERNEL_DATA_SELECTOR
    );

    gdt_load_task_register(GDT_TSS_SELECTOR);
}

uint64_t gdt_kernel_stack_top(void)
{
    return tss.rsp0;
}
