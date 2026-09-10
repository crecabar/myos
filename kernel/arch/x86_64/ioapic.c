// SPDX-License-Identifier: GPL-2.0-only

#include "ioapic.h"

#include "paging.h"

#include <stddef.h>

#define IOAPIC_REGISTER_SELECT 0x00
#define IOAPIC_REGISTER_WINDOW 0x10

#define IOAPIC_REGISTER_ID      0x00
#define IOAPIC_REGISTER_VERSION 0x01

#define IOAPIC_REDIRECTION_BASE 0x10

#define IOAPIC_REDIRECTION_POLARITY_LOW (1U << 13)
#define IOAPIC_REDIRECTION_TRIGGER_LEVEL (1U << 15)
#define IOAPIC_REDIRECTION_MASKED        (1U << 16)

static volatile uint32_t *ioapic_base;
static uint32_t ioapic_gsi_base;
static uint32_t ioapic_redirection_count;
static bool ioapic_initialized;

/* Helpers and private functions */

static uint32_t ioapic_read(uint8_t reg);
static void ioapic_write(uint8_t reg, uint32_t value);

/* END HELPERS */

bool ioapic_init(
    uint64_t physical_address,
    uint32_t gsi_base)
{
    if (ioapic_initialized) return false;

    volatile void *mapping;

    if (!paging_map_mmio_page(
        physical_address,
        &mapping
    )) {
        return false;
    }

    ioapic_base = mapping;
    ioapic_gsi_base = gsi_base;

    uint32_t version =
        ioapic_read(IOAPIC_REGISTER_VERSION);

    uint32_t maximum_redirection_entry =
        (version >> 16) & 0xFFU;

    ioapic_redirection_count =
        maximum_redirection_entry + 1;

    ioapic_initialized = true;

    return true;
}

bool ioapic_route(
    uint32_t gsi,
    uint8_t vector,
    uint8_t destination_lapic_id,
    bool active_low,
    bool level_triggered)
{
    if (!ioapic_initialized) return false;
    if (gsi < ioapic_gsi_base) return false;

    uint32_t input =
        gsi - ioapic_gsi_base;

    if (input >= ioapic_redirection_count) {
        return false;
    }

    uint8_t register_low =
        (uint8_t) (
            IOAPIC_REDIRECTION_BASE +
            (input * 2)
        );

    uint8_t register_high =
        (uint8_t) (register_low + 1);

    uint32_t low =
        vector | IOAPIC_REDIRECTION_MASKED;

    if (active_low) {
        low |= IOAPIC_REDIRECTION_POLARITY_LOW;
    }

    if (level_triggered) {
        low |= IOAPIC_REDIRECTION_TRIGGER_LEVEL;
    }

    uint32_t high =
        (uint32_t) destination_lapic_id << 24;

    /*
     * Program the destination while the input is masked.
     */
    ioapic_write(register_low, low);
    ioapic_write(register_high, high);

    /*
     * Finally unmask the route.
     */
    low &= ~IOAPIC_REDIRECTION_MASKED;
    ioapic_write(register_low, low);

    return true;
}

static uint32_t ioapic_read(uint8_t reg)
{
    ioapic_base[IOAPIC_REGISTER_SELECT / sizeof(uint32_t)] =
        reg;

    return ioapic_base[
        IOAPIC_REGISTER_WINDOW / sizeof(uint32_t)
    ];
}

static void ioapic_write(uint8_t reg, uint32_t value)
{
    ioapic_base[IOAPIC_REGISTER_SELECT / sizeof(uint32_t)] =
        reg;

    ioapic_base[
        IOAPIC_REGISTER_WINDOW / sizeof(uint32_t)
    ] = value;
}
