// SPDX-License-Identifier: GPL-2.0-only

#include "pic.h"

#define PIC_MASTER_COMMAND 0x20
#define PIC_MASTER_DATA    0x21
#define PIC_SLAVE_COMMAND  0xA0
#define PIC_SLAVE_DATA     0xA1

#define PIC_COMMAND_INIT 0x11
#define PIC_COMMAND_EOI  0x20

#define PIC_ICW4_8086 0x01

static void pic_out8(uint16_t port, uint8_t value);
static uint8_t pic_in8(uint16_t port);
static void pic_io_wait(void);

static void pic_out8(uint16_t port, uint8_t value)
{
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

static uint8_t pic_in8(uint16_t port)
{
    uint8_t value;

    __asm__ volatile (
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

static void pic_io_wait(void)
{
    __asm__ volatile (
        "outb %%al, $0x80"
        :
        : "a"(0)
    );
}

void pic_disable(void)
{
    pic_out8(PIC_MASTER_DATA, 0xFF);
    pic_out8(PIC_SLAVE_DATA, 0xFF);
}

void pic_init(void)
{
    pic_out8(PIC_MASTER_COMMAND, PIC_COMMAND_INIT);
    pic_io_wait();

    pic_out8(PIC_SLAVE_COMMAND, PIC_COMMAND_INIT);
    pic_io_wait();

    pic_out8(PIC_MASTER_DATA, PIC_MASTER_VECTOR_BASE);
    pic_io_wait();

    pic_out8(PIC_SLAVE_DATA, PIC_SLAVE_VECTOR_BASE);
    pic_io_wait();

    pic_out8(PIC_MASTER_DATA, 0x04);
    pic_io_wait();

    pic_out8(PIC_SLAVE_DATA, 0x02);
    pic_io_wait();

    pic_out8(PIC_MASTER_DATA, PIC_ICW4_8086);
    pic_io_wait();

    pic_out8(PIC_SLAVE_DATA, PIC_ICW4_8086);
    pic_io_wait();

    /*
     * Initially mask every IRQ except IRQ0 (timer).
     */
    pic_out8(PIC_MASTER_DATA, 0xFE);
    pic_out8(PIC_SLAVE_DATA, 0xFF);
}

void pic_send_eoi(uint8_t irq)
{
    if (irq >= 8) {
        pic_out8(PIC_SLAVE_COMMAND, PIC_COMMAND_EOI);
    }

    pic_out8(PIC_MASTER_COMMAND, PIC_COMMAND_EOI);
}

uint8_t pic_master_mask(void)
{
    return pic_in8(PIC_MASTER_DATA);
}

uint8_t pic_master_irr(void)
{
    pic_out8(PIC_MASTER_COMMAND, 0x0A);

    return pic_in8(PIC_MASTER_COMMAND);
}
