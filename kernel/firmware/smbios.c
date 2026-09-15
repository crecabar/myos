// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file smbios.c
 * @brief SMBIOS platform identification.
 */

#include "smbios.h"

#include "../core/panic.h"
#include "../memory/memory.h"

#include <stddef.h>
#include <stdint.h>

#define SMBIOS_STRUCTURE_HEADER_SIZE 4U
#define SMBIOS_SYSTEM_INFORMATION_TYPE 1U
#define SMBIOS_END_OF_TABLE_TYPE 127U

#define SMBIOS_TYPE1_MINIMUM_LENGTH 8U

#define SMBIOS_TYPE1_MANUFACTURER_OFFSET 4U
#define SMBIOS_TYPE1_PRODUCT_NAME_OFFSET 5U
#define SMBIOS_TYPE1_VERSION_OFFSET 6U
#define SMBIOS_TYPE1_SERIAL_NUMBER_OFFSET 7U

#define SMBIOS32_MINIMUM_ENTRY_LENGTH 0x1FU
#define SMBIOS32_TABLE_LENGTH_OFFSET 0x16U
#define SMBIOS32_TABLE_ADDRESS_OFFSET 0x18U
#define SMBIOS32_STRUCTURE_COUNT_OFFSET 0x1CU

#define SMBIOS64_MINIMUM_ENTRY_LENGTH 0x18U
#define SMBIOS64_TABLE_MAXIMUM_SIZE_OFFSET 0x0CU
#define SMBIOS64_TABLE_ADDRESS_OFFSET 0x10U

static uint16_t smbios_read_u16(
    const uint8_t *bytes
);

static uint32_t smbios_read_u32(
    const uint8_t *bytes
);

static uint64_t smbios_read_u64(
    const uint8_t *bytes
);

static bool smbios_bytes_equal(
    const uint8_t *bytes,
    const char *string,
    size_t length
);

static bool smbios_checksum_valid(
    const uint8_t *bytes,
    size_t length
);

static void smbios_string_clear(
    char *string,
    size_t capacity
);

static void smbios_system_info_clear(
    struct smbios_system_info *info
);

static bool smbios_string_copy_indexed(
    const uint8_t *strings,
    const uint8_t *table_end,
    uint8_t string_index,
    char *destination,
    size_t capacity
);

static const uint8_t *smbios_structure_next(
    const uint8_t *structure,
    const uint8_t *table_end
);

static bool smbios_type1_decode(
    const uint8_t *structure,
    const uint8_t *table_end,
    struct smbios_system_info *info
);

static bool smbios_table_walk(
    const uint8_t *table,
    size_t table_size,
    size_t structure_limit,
    struct smbios_system_info *info
);

static bool smbios_read_64(
    const void *entry,
    struct smbios_system_info *info
);

static bool smbios_read_32(
    const void *entry,
    struct smbios_system_info *info
);

static uint16_t smbios_read_u16(
    const uint8_t *bytes)
{
    return
        (uint16_t) bytes[0] |
        ((uint16_t) bytes[1] << 8);
}

static uint32_t smbios_read_u32(
    const uint8_t *bytes)
{
    return
        (uint32_t) bytes[0] |
        ((uint32_t) bytes[1] << 8) |
        ((uint32_t) bytes[2] << 16) |
        ((uint32_t) bytes[3] << 24);
}

static uint64_t smbios_read_u64(
    const uint8_t *bytes)
{
    return
        (uint64_t) bytes[0] |
        ((uint64_t) bytes[1] << 8) |
        ((uint64_t) bytes[2] << 16) |
        ((uint64_t) bytes[3] << 24) |
        ((uint64_t) bytes[4] << 32) |
        ((uint64_t) bytes[5] << 40) |
        ((uint64_t) bytes[6] << 48) |
        ((uint64_t) bytes[7] << 56);
}

static bool smbios_bytes_equal(
    const uint8_t *bytes,
    const char *string,
    size_t length)
{
    for (size_t index = 0; index < length; ++index) {
        if (bytes[index] != (uint8_t) string[index]) {
            return false;
        }
    }

    return true;
}

static bool smbios_checksum_valid(
    const uint8_t *bytes,
    size_t length)
{
    uint8_t checksum = 0;

    for (size_t index = 0; index < length; ++index) {
        checksum =
            (uint8_t) (
                checksum +
                bytes[index]
            );
    }

    return checksum == 0;
}

static void smbios_string_clear(
    char *string,
    size_t capacity)
{
    for (size_t index = 0; index < capacity; ++index) {
        string[index] = '\0';
    }
}

static void smbios_system_info_clear(
    struct smbios_system_info *info)
{
    smbios_string_clear(
        info->manufacturer,
        SMBIOS_SYSTEM_STRING_CAPACITY
    );

    smbios_string_clear(
        info->product_name,
        SMBIOS_SYSTEM_STRING_CAPACITY
    );

    smbios_string_clear(
        info->version,
        SMBIOS_SYSTEM_STRING_CAPACITY
    );

    smbios_string_clear(
        info->serial_number,
        SMBIOS_SYSTEM_STRING_CAPACITY
    );

    info->available = false;
}

static bool smbios_string_copy_indexed(
    const uint8_t *strings,
    const uint8_t *table_end,
    uint8_t string_index,
    char *destination,
    size_t capacity)
{
    if (string_index == 0) {
        return true;
    }

    uint8_t current_index = 1;
    const uint8_t *cursor = strings;

    while (cursor < table_end) {
        if (*cursor == 0) {
            if (
                cursor + 1 >= table_end ||
                cursor[1] == 0
            ) {
                return false;
            }

            ++cursor;
            ++current_index;
            continue;
        }

        if (current_index == string_index) {
            size_t destination_index = 0;

            while (
                cursor < table_end &&
                *cursor != 0
            ) {
                if (
                    destination_index + 1 <
                    capacity
                ) {
                    destination[destination_index] =
                        (char) *cursor;

                    ++destination_index;
                }

                ++cursor;
            }

            destination[destination_index] = '\0';

            return cursor < table_end;
        }

        while (
            cursor < table_end &&
            *cursor != 0
        ) {
            ++cursor;
        }
    }

    return false;
}

static const uint8_t *smbios_structure_next(
    const uint8_t *structure,
    const uint8_t *table_end)
{
    if (
        structure + SMBIOS_STRUCTURE_HEADER_SIZE >
        table_end
    ) {
        return NULL;
    }

    uint8_t formatted_length =
        structure[1];

    if (
        formatted_length <
        SMBIOS_STRUCTURE_HEADER_SIZE
    ) {
        return NULL;
    }

    if (
        (size_t) (table_end - structure) <
        formatted_length
    ) {
        return NULL;
    }

    const uint8_t *cursor =
        structure + formatted_length;

    while (cursor < table_end) {
        if (*cursor != 0) {
            ++cursor;
            continue;
        }

        if (
            cursor + 1 >= table_end
        ) {
            return NULL;
        }

        if (cursor[1] == 0) {
            return cursor + 2;
        }

        ++cursor;
    }

    return NULL;
}

static bool smbios_type1_decode(
    const uint8_t *structure,
    const uint8_t *table_end,
    struct smbios_system_info *info)
{
    uint8_t formatted_length =
        structure[1];

    if (
        formatted_length <
        SMBIOS_TYPE1_MINIMUM_LENGTH
    ) {
        return false;
    }

    if (
        (size_t) (table_end - structure) <
        formatted_length
    ) {
        return false;
    }

    const uint8_t *strings =
        structure + formatted_length;

    if (
        !smbios_string_copy_indexed(
            strings,
            table_end,
            structure[SMBIOS_TYPE1_MANUFACTURER_OFFSET],
            info->manufacturer,
            SMBIOS_SYSTEM_STRING_CAPACITY
        )
    ) {
        return false;
    }

    if (
        !smbios_string_copy_indexed(
            strings,
            table_end,
            structure[SMBIOS_TYPE1_PRODUCT_NAME_OFFSET],
            info->product_name,
            SMBIOS_SYSTEM_STRING_CAPACITY
        )
    ) {
        return false;
    }

    if (
        !smbios_string_copy_indexed(
            strings,
            table_end,
            structure[SMBIOS_TYPE1_VERSION_OFFSET],
            info->version,
            SMBIOS_SYSTEM_STRING_CAPACITY
        )
    ) {
        return false;
    }

    if (
        !smbios_string_copy_indexed(
            strings,
            table_end,
            structure[SMBIOS_TYPE1_SERIAL_NUMBER_OFFSET],
            info->serial_number,
            SMBIOS_SYSTEM_STRING_CAPACITY
        )
    ) {
        return false;
    }

    info->available = true;

    return true;
}

static bool smbios_table_walk(
    const uint8_t *table,
    size_t table_size,
    size_t structure_limit,
    struct smbios_system_info *info)
{
    const uint8_t *cursor = table;
    const uint8_t *table_end =
        table + table_size;

    size_t structure_count = 0;

    while (
        cursor < table_end &&
        structure_count < structure_limit
    ) {
        if (
            (size_t) (table_end - cursor) <
            SMBIOS_STRUCTURE_HEADER_SIZE
        ) {
            return false;
        }

        uint8_t type =
            cursor[0];

        if (
            type ==
            SMBIOS_SYSTEM_INFORMATION_TYPE
        ) {
            return smbios_type1_decode(
                cursor,
                table_end,
                info
            );
        }

        if (
            type ==
            SMBIOS_END_OF_TABLE_TYPE
        ) {
            return false;
        }

        const uint8_t *next =
            smbios_structure_next(
                cursor,
                table_end
            );

        if (next == NULL) {
            return false;
        }

        cursor = next;
        ++structure_count;
    }

    return false;
}

static bool smbios_read_64(
    const void *entry,
    struct smbios_system_info *info)
{
    const uint8_t *bytes =
        (const uint8_t *) entry;

    if (
        !smbios_bytes_equal(
            bytes,
            "_SM3_",
            5
        )
    ) {
        return false;
    }

    uint8_t entry_length =
        bytes[6];

    if (
        entry_length <
        SMBIOS64_MINIMUM_ENTRY_LENGTH
    ) {
        return false;
    }

    if (
        !smbios_checksum_valid(
            bytes,
            entry_length
        )
    ) {
        return false;
    }

    uint32_t table_size =
        smbios_read_u32(
            &bytes[
                SMBIOS64_TABLE_MAXIMUM_SIZE_OFFSET
            ]
        );

    uint64_t table_physical =
        smbios_read_u64(
            &bytes[
                SMBIOS64_TABLE_ADDRESS_OFFSET
            ]
        );

    if (
        table_size == 0 ||
        table_physical == 0
    ) {
        return false;
    }

    const uint8_t *table =
        (const uint8_t *)
            memory_physical_to_virtual(
                table_physical
            );

    return smbios_table_walk(
        table,
        table_size,
        SIZE_MAX,
        info
    );
}

static bool smbios_read_32(
    const void *entry,
    struct smbios_system_info *info)
{
    const uint8_t *bytes =
        (const uint8_t *) entry;

    if (
        !smbios_bytes_equal(
            bytes,
            "_SM_",
            4
        )
    ) {
        return false;
    }

    uint8_t entry_length =
        bytes[5];

    if (
        entry_length <
        SMBIOS32_MINIMUM_ENTRY_LENGTH
    ) {
        return false;
    }

    if (
        !smbios_checksum_valid(
            bytes,
            entry_length
        )
    ) {
        return false;
    }

    if (
        !smbios_bytes_equal(
            &bytes[16],
            "_DMI_",
            5
        )
    ) {
        return false;
    }

    /*
     * The intermediate DMI entry-point checksum covers bytes 0x10..0x1e.
     */
    if (
        !smbios_checksum_valid(
            &bytes[16],
            15
        )
    ) {
        return false;
    }

    uint16_t table_size =
        smbios_read_u16(
            &bytes[
                SMBIOS32_TABLE_LENGTH_OFFSET
            ]
        );

    uint32_t table_physical =
        smbios_read_u32(
            &bytes[
                SMBIOS32_TABLE_ADDRESS_OFFSET
            ]
        );

    uint16_t structure_count =
        smbios_read_u16(
            &bytes[
                SMBIOS32_STRUCTURE_COUNT_OFFSET
            ]
        );

    if (
        table_size == 0 ||
        table_physical == 0 ||
        structure_count == 0
    ) {
        return false;
    }

    const uint8_t *table =
        (const uint8_t *)
            memory_physical_to_virtual(
                table_physical
            );

    return smbios_table_walk(
        table,
        table_size,
        structure_count,
        info
    );
}

bool smbios_system_info_read(
    const void *entry_32,
    const void *entry_64,
    struct smbios_system_info *info)
{
    if (info == NULL) {
        kernel_panic(
            "smbios_system_info_read received NULL info"
        );
    }

    smbios_system_info_clear(info);

    if (
        entry_64 != NULL &&
        smbios_read_64(
            entry_64,
            info
        )
    ) {
        return true;
    }

    smbios_system_info_clear(info);

    if (
        entry_32 != NULL &&
        smbios_read_32(
            entry_32,
            info
        )
    ) {
        return true;
    }

    smbios_system_info_clear(info);

    return false;
}
