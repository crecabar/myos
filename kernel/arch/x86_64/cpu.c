// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file cpu.c
 * @brief x86-64 CPU identification through CPUID.
 */

#include "cpu.h"

#include "../../core/panic.h"

#include <stddef.h>
#include <stdint.h>

#define CPUID_BASIC_FEATURES       0x00000001U
#define CPUID_HYPERVISOR_VENDOR    0x40000000U
#define CPUID_EXTENDED_MAX         0x80000000U
#define CPUID_EXTENDED_BRAND_FIRST 0x80000002U
#define CPUID_EXTENDED_BRAND_LAST  0x80000004U

#define CPUID_FEATURE_HYPERVISOR (1U << 31)

static void cpu_cpuid(
    uint32_t leaf,
    uint32_t subleaf,
    uint32_t *eax,
    uint32_t *ebx,
    uint32_t *ecx,
    uint32_t *edx
);

static void cpu_write_u32_string(
    char *destination,
    uint32_t value
);

static void cpu_string_clear(
    char *string,
    size_t capacity
);

static void cpu_string_set(
    char *destination,
    size_t capacity,
    const char *source
);

static bool cpu_string_starts_with(
    const char *string,
    const char *prefix
);

static void cpu_brand_trim(char *brand);

static void cpu_cpuid(
    uint32_t leaf,
    uint32_t subleaf,
    uint32_t *eax,
    uint32_t *ebx,
    uint32_t *ecx,
    uint32_t *edx)
{
    uint32_t a = leaf;
    uint32_t b;
    uint32_t c = subleaf;
    uint32_t d;

    __asm__ volatile (
        "cpuid"
        : "+a"(a),
          "=b"(b),
          "+c"(c),
          "=d"(d)
    );

    *eax = a;
    *ebx = b;
    *ecx = c;
    *edx = d;
}

static void cpu_write_u32_string(
    char *destination,
    uint32_t value)
{
    destination[0] = (char) (value & 0xFFU);
    destination[1] = (char) ((value >> 8) & 0xFFU);
    destination[2] = (char) ((value >> 16) & 0xFFU);
    destination[3] = (char) ((value >> 24) & 0xFFU);
}

static void cpu_string_clear(
    char *string,
    size_t capacity)
{
    for (size_t index = 0; index < capacity; ++index) {
        string[index] = '\0';
    }
}

static void cpu_string_set(
    char *destination,
    size_t capacity,
    const char *source)
{
    size_t index = 0;

    while (
        index + 1 < capacity &&
        source[index] != '\0'
    ) {
        destination[index] = source[index];
        ++index;
    }

    destination[index] = '\0';
}

static bool cpu_string_starts_with(
    const char *string,
    const char *prefix)
{
    size_t index = 0;

    while (prefix[index] != '\0') {
        if (string[index] != prefix[index]) {
            return false;
        }

        ++index;
    }

    return true;
}

static void cpu_brand_trim(char *brand)
{
    size_t first = 0;

    while (brand[first] == ' ') {
        ++first;
    }

    if (first > 0) {
        size_t destination = 0;

        while (brand[first] != '\0') {
            brand[destination] = brand[first];
            ++destination;
            ++first;
        }

        brand[destination] = '\0';
    }

    size_t length = 0;

    while (brand[length] != '\0') {
        ++length;
    }

    while (
        length > 0 &&
        brand[length - 1] == ' '
    ) {
        --length;
        brand[length] = '\0';
    }
}

void cpu_info_read(struct cpu_info *info)
{
    if (info == NULL) {
        kernel_panic(
            "cpu_info_read received NULL info"
        );
    }

    cpu_string_clear(
        info->brand,
        CPU_BRAND_STRING_CAPACITY
    );

    cpu_string_clear(
        info->hypervisor_vendor,
        CPU_HYPERVISOR_VENDOR_CAPACITY
    );

    info->hypervisor_present = false;

    cpu_string_set(
        info->brand,
        CPU_BRAND_STRING_CAPACITY,
        "x86-64 CPU"
    );

    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;

    cpu_cpuid(
        CPUID_BASIC_FEATURES,
        0,
        &eax,
        &ebx,
        &ecx,
        &edx
    );

    info->hypervisor_present =
        (ecx & CPUID_FEATURE_HYPERVISOR) != 0;

    if (info->hypervisor_present) {
        cpu_cpuid(
            CPUID_HYPERVISOR_VENDOR,
            0,
            &eax,
            &ebx,
            &ecx,
            &edx
        );

        cpu_write_u32_string(
            &info->hypervisor_vendor[0],
            ebx
        );

        cpu_write_u32_string(
            &info->hypervisor_vendor[4],
            ecx
        );

        cpu_write_u32_string(
            &info->hypervisor_vendor[8],
            edx
        );

        info->hypervisor_vendor[12] = '\0';
    }

    cpu_cpuid(
        CPUID_EXTENDED_MAX,
        0,
        &eax,
        &ebx,
        &ecx,
        &edx
    );

    if (eax < CPUID_EXTENDED_BRAND_LAST) {
        return;
    }

    size_t offset = 0;

    for (
        uint32_t leaf = CPUID_EXTENDED_BRAND_FIRST;
        leaf <= CPUID_EXTENDED_BRAND_LAST;
        ++leaf
    ) {
        cpu_cpuid(
            leaf,
            0,
            &eax,
            &ebx,
            &ecx,
            &edx
        );

        cpu_write_u32_string(
            &info->brand[offset],
            eax
        );

        cpu_write_u32_string(
            &info->brand[offset + 4],
            ebx
        );

        cpu_write_u32_string(
            &info->brand[offset + 8],
            ecx
        );

        cpu_write_u32_string(
            &info->brand[offset + 12],
            edx
        );

        offset += 16;
    }

    info->brand[48] = '\0';

    cpu_brand_trim(info->brand);
}

bool cpu_info_is_qemu(const struct cpu_info *info)
{
    if (info == NULL) {
        return false;
    }

    if (
        cpu_string_starts_with(
            info->brand,
            "QEMU"
        )
    ) {
        return true;
    }

    return cpu_string_starts_with(
        info->hypervisor_vendor,
        "TCGTCGTCGTCG"
    );
}
