// SPDX-License-Identifier: GPL-2.0-only

#include "lapic.h"

#include "paging.h"

#include <stddef.h>

#define IA32_APIC_BASE_MSR 0x1B

#define IA32_APIC_BASE_BSP          (1ULL << 8)
#define IA32_APIC_BASE_X2APIC       (1ULL << 10)
#define IA32_APIC_BASE_ENABLE       (1ULL << 11)
#define IA32_APIC_BASE_ADDRESS_MASK 0x000ffffffffff000ULL

#define LAPIC_REGISTER_ID       0x020
#define LAPIC_REGISTER_EOI      0x0B0
#define LAPIC_REGISTER_SPURIOUS 0x0F0

#define LAPIC_SPURIOUS_VECTOR 0xFF
#define LAPIC_SOFTWARE_ENABLE (1U << 8)

static volatile uint32_t *lapic_base;
static bool lapic_initialized;

/* Helpers and private functions */

static uint64_t lapic_read_msr(uint32_t msr);
static uint32_t lapic_read(uint32_t offset);
static void lapic_write(uint32_t offset, uint32_t value);

/* END HELPERS */

bool lapic_init(void)
{
    if (lapic_initialized) return false;

    uint64_t apic_base_msr = lapic_read_msr(IA32_APIC_BASE_MSR);

    if ((apic_base_msr & IA32_APIC_BASE_ENABLE) == 0) {
        return false;
    }

    /*
     * MyOS currently implements the xAPIC MMIO interface.
     * x2APIC uses MSRs instead and will be supported separately.
     */
    if ((apic_base_msr & IA32_APIC_BASE_X2APIC) != 0) {
        return false;
    }

    uint64_t physical_address = apic_base_msr & IA32_APIC_BASE_ADDRESS_MASK;

    volatile void *mapping;

    if (!paging_map_mmio_page(
        physical_address,
        &mapping
    )) {
        return false;
    }

    lapic_base = mapping;

    uint32_t spurious =
        lapic_read(LAPIC_REGISTER_SPURIOUS);

    spurious &= ~0xFFU;
    spurious |= LAPIC_SPURIOUS_VECTOR;
    spurious |= LAPIC_SOFTWARE_ENABLE;

    lapic_write(
        LAPIC_REGISTER_SPURIOUS,
        spurious
    );

    lapic_initialized = true;

    return true;
}

uint8_t lapic_id(void)
{
    if (!lapic_initialized) {
        return 0;
    }

    return (uint8_t) (
        lapic_read(LAPIC_REGISTER_ID) >> 24
    );
}

void lapic_send_eoi(void)
{
    if (!lapic_initialized) return;

    lapic_write(LAPIC_REGISTER_EOI, 0);
}

static uint64_t lapic_read_msr(uint32_t msr)
{
    uint32_t low;
    uint32_t high;

    __asm__ volatile (
        "rdmsr"
        : "=a"(low), "=d"(high)
        : "c"(msr)
    );

    return ((uint64_t) high << 32) | low;
}

static uint32_t lapic_read(uint32_t offset)
{
    return lapic_base[offset / sizeof(uint32_t)];
}

static void lapic_write(uint32_t offset, uint32_t value)
{
    lapic_base[offset / sizeof(uint32_t)] = value;

    /*
     * Read back to serialize the MMIO write.
     */
    (void) lapic_base[LAPIC_REGISTER_ID / sizeof(uint32_t)];
}
