// SPDX-License-Identifier: GPL-2.0-only

#include "interrupt_topology.h"

#include "../../firmware/acpi.h"
#include "../../memory/memory.h"

#include <stddef.h>
#include <stdint.h>

#define ISA_BUS_ID 0U
#define PIT_IRQ    0U

static bool interrupt_topology_isa_flags_decode(
    uint16_t flags,
    bool *active_low,
    bool *level_triggered
);

static bool interrupt_topology_isa_flags_decode(
    uint16_t flags,
    bool *active_low,
    bool *level_triggered)
{
    if (
        active_low == NULL ||
        level_triggered == NULL
    ) {
        return false;
    }

    /*
     * Only the low four bits are defined for MADT
     * Interrupt Source Override polarity and trigger mode.
     */
    if ((flags & 0xFFF0U) != 0) {
        return false;
    }

    uint16_t polarity = flags & 0x3U;
    uint16_t trigger = (flags >> 2) & 0x3U;

    /*
     * For ISA, "conforms to bus specifications" means
     * active-high polarity and edge-triggered delivery.
     *
     * Encoding 2 is reserved for both fields.
     */
    switch (polarity) {
        case 0U:
        case 1U:
            *active_low = false;
            break;

        case 3U:
            *active_low = true;
            break;

        default:
            return false;
    }

    switch (trigger) {
        case 0U:
        case 1U:
            *level_triggered = false;
            break;

        case 3U:
            *level_triggered = true;
            break;

        default:
            return false;
    }

    return true;
}

bool interrupt_topology_isa_route_from_madt(
    const struct acpi_madt *madt,
    uint8_t irq,
    struct interrupt_route *route)
{
    if (
        madt == NULL ||
        route == NULL ||
        madt->data == NULL
    ) {
        return false;
    }

    struct interrupt_route candidate = {
        .irq = irq,
        .gsi = (uint32_t) irq,
        .active_low = false,
        .level_triggered = false,
    };

    bool override_found = false;

    for (
        size_t index = 0;
        index < madt->record_count;
        ++index
    ) {
        struct acpi_madt_record record;

        if (
            !acpi_madt_record_get(
                madt,
                index,
                &record
            )
        ) {
            return false;
        }

        if (
            record.type !=
            ACPI_MADT_RECORD_INTERRUPT_OVERRIDE
        ) {
            continue;
        }

        struct acpi_madt_interrupt_override
            irq_override;

        if (
            !acpi_madt_interrupt_override_decode(
                &record,
                &irq_override
            )
        ) {
            return false;
        }

        if (
            irq_override.bus != ISA_BUS_ID ||
            irq_override.source_irq != irq
        ) {
            continue;
        }

        if (override_found) {
            return false;
        }

        bool active_low;
        bool level_triggered;

        if (
            !interrupt_topology_isa_flags_decode(
                irq_override.flags,
                &active_low,
                &level_triggered
            )
        ) {
            return false;
        }

        candidate.gsi =
            irq_override.gsi;

        candidate.active_low =
            active_low;

        candidate.level_triggered =
            level_triggered;

        override_found = true;
    }

    *route = candidate;

    return true;
}

bool interrupt_topology_from_madt(
    const struct acpi_madt *madt,
    struct interrupt_topology *topology)
{
    if (
        madt == NULL ||
        topology == NULL ||
        madt->data == NULL
    ) {
        return false;
    }

    /*
     * Build a local candidate first. Never publish a partially
     * discovered topology to the caller.
     */
    struct interrupt_topology candidate = {0};

    bool ioapic_found = false;

    for (
        size_t index = 0;
        index < madt->record_count;
        ++index
    ) {
        struct acpi_madt_record record;

        if (
            !acpi_madt_record_get(
                madt,
                index,
                &record
            )
        ) {
            return false;
        }

        if (record.type == ACPI_MADT_RECORD_IOAPIC) {
            struct acpi_madt_ioapic ioapic;

            if (
                !acpi_madt_ioapic_decode(
                    &record,
                    &ioapic
                )
            ) {
                return false;
            }

            /*
             * The current IOAPIC driver manages one controller.
             * Do not select one arbitrarily when firmware declares
             * multiple controllers.
             */
            if (ioapic_found) {
                return false;
            }

            if (
                ioapic.physical_address == 0 ||
                (
                    ioapic.physical_address &
                    (MEMORY_FRAME_SIZE - 1U)
                ) != 0
            ) {
                return false;
            }

            candidate.ioapic_physical_address =
                (uint64_t) ioapic.physical_address;

            candidate.ioapic_gsi_base =
                ioapic.gsi_base;

            ioapic_found = true;

            continue;
        }
    }

    if (!ioapic_found) {
        return false;
    }

    if (
        !interrupt_topology_isa_route_from_madt(
            madt,
            PIT_IRQ,
            &candidate.timer_route
        )
    ) {
        return false;
    }

    /*
     * We have not yet queried the IOAPIC version register to
     * establish its redirection-entry count. For now, reject
     * only the case where the timer GSI precedes its declared
     * base. ioapic_route() performs the hardware upper-bound
     * check during controller initialization.
     */
    if (
        candidate.timer_route.gsi <
        candidate.ioapic_gsi_base
    ) {
        return false;
    }

    *topology = candidate;

    return true;
}
