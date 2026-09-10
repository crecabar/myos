// SPDX-License-Identifier: GPL-2.0-only

#include "interrupt_topology.h"

#include <stddef.h>

#define Q35_IOAPIC_PHYSICAL_ADDRESS 0xFEC00000ULL
#define Q35_IOAPIC_GSI_BASE         0
#define Q35_PIT_IRQ                 0
#define Q35_PIT_GSI                 2

bool interrupt_topology_discover(struct interrupt_topology *topology)
{
    if (topology == NULL) return false;

    topology->ioapic_physical_address = Q35_IOAPIC_PHYSICAL_ADDRESS;
    topology->ioapic_gsi_base = Q35_IOAPIC_GSI_BASE;

    topology->timer_route.irq = Q35_PIT_IRQ;
    topology->timer_route.gsi = Q35_PIT_GSI;
    topology->timer_route.active_low = false;
    topology->timer_route.level_triggered = false;

    return true;
}
