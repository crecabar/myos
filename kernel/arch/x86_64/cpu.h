// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file cpu.h
 * @brief x86-64 CPU identification.
 */

#ifndef MYOS_ARCH_X86_64_CPU_H
#define MYOS_ARCH_X86_64_CPU_H

#include <stdbool.h>

#define CPU_BRAND_STRING_CAPACITY 49
#define CPU_HYPERVISOR_VENDOR_CAPACITY 13

struct cpu_info {
    char brand[CPU_BRAND_STRING_CAPACITY];
    char hypervisor_vendor[CPU_HYPERVISOR_VENDOR_CAPACITY];
    bool hypervisor_present;
};

/**
 * Reads basic CPU identity information through CPUID.
 *
 * @param info CPU information descriptor to initialize.
 */
void cpu_info_read(struct cpu_info *info);

/**
 * Returns whether the CPU environment appears to be QEMU/TCG.
 *
 * @param info Previously initialized CPU information.
 *
 * @return true when QEMU is detected; false otherwise.
 */
bool cpu_info_is_qemu(const struct cpu_info *info);

#endif
