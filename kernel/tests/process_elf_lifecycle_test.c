// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file process_elf_lifecycle_test.c
 * @brief ELF-backed process lifecycle regression tests.
 */

#include "process_elf_lifecycle_test.h"

#include "../arch/x86_64/interrupts.h"
#include "../arch/x86_64/paging.h"
#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../elf/elf64.h"
#include "../memory/memory.h"
#include "../memory/heap.h"
#include "../process/create.h"
#include "../process/cwd.h"
#include "../process/exec.h"
#include "../process/fd_table.h"
#include "../process/fork.h"
#include "../process/image.h"
#include "../process/instance.h"
#include "../process/layout.h"
#include "../process/lifecycle.h"
#include "../process/memory.h"
#include "../process/pid.h"
#include "../process/process.h"
#include "../process/wait.h"
#include "../scheduler/scheduler.h"
#include "../vfs/vfs.h"

#include <stddef.h>
#include <stdint.h>

#define PROCESS_ELF_LIFECYCLE_TEST_IMAGE_SIZE 0x2000U
#define PROCESS_ELF_LIFECYCLE_TEST_VADDR      0x0000000000400000ULL

#define PROCESS_ELF_LIFECYCLE_TEST_KERNEL_ADDRESS \
    0xFFFFFFFF80000000ULL

#define PROCESS_ELF_LIFECYCLE_TEST_HEADER_TYPE_OFFSET                 16U
#define PROCESS_ELF_LIFECYCLE_TEST_HEADER_MACHINE_OFFSET              18U
#define PROCESS_ELF_LIFECYCLE_TEST_HEADER_VERSION_OFFSET              20U
#define PROCESS_ELF_LIFECYCLE_TEST_HEADER_ENTRY_OFFSET                24U
#define PROCESS_ELF_LIFECYCLE_TEST_HEADER_PROGRAM_HEADER_OFFSET       32U
#define PROCESS_ELF_LIFECYCLE_TEST_HEADER_SIZE_OFFSET                 52U
#define PROCESS_ELF_LIFECYCLE_TEST_HEADER_PROGRAM_HEADER_SIZE_OFFSET  54U
#define PROCESS_ELF_LIFECYCLE_TEST_HEADER_PROGRAM_HEADER_COUNT_OFFSET 56U

#define PROCESS_ELF_LIFECYCLE_TEST_PROGRAM_TYPE_OFFSET            0U
#define PROCESS_ELF_LIFECYCLE_TEST_PROGRAM_FLAGS_OFFSET           4U
#define PROCESS_ELF_LIFECYCLE_TEST_PROGRAM_FILE_OFFSET_OFFSET     8U
#define PROCESS_ELF_LIFECYCLE_TEST_PROGRAM_VIRTUAL_ADDRESS_OFFSET 16U
#define PROCESS_ELF_LIFECYCLE_TEST_PROGRAM_FILE_SIZE_OFFSET       32U
#define PROCESS_ELF_LIFECYCLE_TEST_PROGRAM_MEMORY_SIZE_OFFSET     40U
#define PROCESS_ELF_LIFECYCLE_TEST_PROGRAM_ALIGNMENT_OFFSET       48U

#define PROCESS_ELF_LIFECYCLE_TEST_PROGRAM_HEADER_OFFSET ELF64_HEADER_SIZE
#define PROCESS_ELF_LIFECYCLE_TEST_FILE_OFFSET           0x1000ULL
#define PROCESS_ELF_LIFECYCLE_TEST_FILE_SIZE             16ULL

#define PROCESS_ELF_LIFECYCLE_TEST_PID 43

static void process_elf_lifecycle_test_write_u16(
    uint8_t *destination,
    uint16_t value
);

static void process_elf_lifecycle_test_write_u32(
    uint8_t *destination,
    uint32_t value
);

static void process_elf_lifecycle_test_write_u64(
    uint8_t *destination,
    uint64_t value
);

static void process_elf_lifecycle_test_build_image(
    uint8_t *bytes,
    size_t size
);

static uint64_t process_elf_lifecycle_test_read_u64(
    struct process_memory *memory,
    uint64_t address
);

static void process_elf_lifecycle_test_expect_string(
    struct process_memory *memory,
    uint64_t address,
    const char *expected
);

static void process_elf_lifecycle_test_layout_clone(
    const struct elf64_image *image,
    size_t argc,
    const char *const argv[],
    size_t envc,
    const char *const envp[]
);

static void process_elf_lifecycle_test_image_clone(
    const struct elf64_image *image,
    size_t argc,
    const char *const argv[],
    size_t envc,
    const char *const envp[]
);

static void process_elf_lifecycle_test_fork_clone(
    const struct elf64_image *image,
    size_t argc,
    const char *const argv[],
    size_t envc,
    const char *const envp[]
);

static void process_elf_lifecycle_test_fork_rollback(
    const struct elf64_image *image,
    size_t argc,
    const char *const argv[],
    size_t envc,
    const char *const envp[]
);

static void process_elf_lifecycle_test_write_u16(
    uint8_t *destination,
    uint16_t value)
{
    destination[0] =
        (uint8_t) value;

    destination[1] =
        (uint8_t) (value >> 8);
}

static void process_elf_lifecycle_test_write_u32(
    uint8_t *destination,
    uint32_t value)
{
    destination[0] =
        (uint8_t) value;

    destination[1] =
        (uint8_t) (value >> 8);

    destination[2] =
        (uint8_t) (value >> 16);

    destination[3] =
        (uint8_t) (value >> 24);
}

static void process_elf_lifecycle_test_write_u64(
    uint8_t *destination,
    uint64_t value)
{
    destination[0] =
        (uint8_t) value;

    destination[1] =
        (uint8_t) (value >> 8);

    destination[2] =
        (uint8_t) (value >> 16);

    destination[3] =
        (uint8_t) (value >> 24);

    destination[4] =
        (uint8_t) (value >> 32);

    destination[5] =
        (uint8_t) (value >> 40);

    destination[6] =
        (uint8_t) (value >> 48);

    destination[7] =
        (uint8_t) (value >> 56);
}

static void process_elf_lifecycle_test_build_image(
    uint8_t *bytes,
    size_t size)
{
    for (size_t index = 0; index < size; ++index) {
        bytes[index] = 0;
    }

    bytes[0] = ELF64_MAGIC_0;
    bytes[1] = ELF64_MAGIC_1;
    bytes[2] = ELF64_MAGIC_2;
    bytes[3] = ELF64_MAGIC_3;

    bytes[4] = ELF64_CLASS_64;
    bytes[5] = ELF64_DATA_LITTLE_ENDIAN;
    bytes[6] = ELF64_VERSION_CURRENT;

    process_elf_lifecycle_test_write_u16(
        bytes +
        PROCESS_ELF_LIFECYCLE_TEST_HEADER_TYPE_OFFSET,
        ELF64_TYPE_EXECUTABLE
    );

    process_elf_lifecycle_test_write_u16(
        bytes +
        PROCESS_ELF_LIFECYCLE_TEST_HEADER_MACHINE_OFFSET,
        ELF64_MACHINE_X86_64
    );

    process_elf_lifecycle_test_write_u32(
        bytes +
        PROCESS_ELF_LIFECYCLE_TEST_HEADER_VERSION_OFFSET,
        ELF64_VERSION_CURRENT
    );

    process_elf_lifecycle_test_write_u64(
        bytes +
        PROCESS_ELF_LIFECYCLE_TEST_HEADER_ENTRY_OFFSET,
        PROCESS_ELF_LIFECYCLE_TEST_VADDR
    );

    process_elf_lifecycle_test_write_u64(
        bytes +
        PROCESS_ELF_LIFECYCLE_TEST_HEADER_PROGRAM_HEADER_OFFSET,
        PROCESS_ELF_LIFECYCLE_TEST_PROGRAM_HEADER_OFFSET
    );

    process_elf_lifecycle_test_write_u16(
        bytes +
        PROCESS_ELF_LIFECYCLE_TEST_HEADER_SIZE_OFFSET,
        ELF64_HEADER_SIZE
    );

    process_elf_lifecycle_test_write_u16(
        bytes +
        PROCESS_ELF_LIFECYCLE_TEST_HEADER_PROGRAM_HEADER_SIZE_OFFSET,
        ELF64_PROGRAM_HEADER_SIZE
    );

    process_elf_lifecycle_test_write_u16(
        bytes +
        PROCESS_ELF_LIFECYCLE_TEST_HEADER_PROGRAM_HEADER_COUNT_OFFSET,
        1
    );

    uint8_t *program_header =
        bytes +
        PROCESS_ELF_LIFECYCLE_TEST_PROGRAM_HEADER_OFFSET;

    process_elf_lifecycle_test_write_u32(
        program_header +
        PROCESS_ELF_LIFECYCLE_TEST_PROGRAM_TYPE_OFFSET,
        ELF64_PROGRAM_TYPE_LOAD
    );

    process_elf_lifecycle_test_write_u32(
        program_header +
        PROCESS_ELF_LIFECYCLE_TEST_PROGRAM_FLAGS_OFFSET,
        ELF64_PROGRAM_FLAG_READ |
        ELF64_PROGRAM_FLAG_EXECUTE
    );

    process_elf_lifecycle_test_write_u64(
        program_header +
        PROCESS_ELF_LIFECYCLE_TEST_PROGRAM_FILE_OFFSET_OFFSET,
        PROCESS_ELF_LIFECYCLE_TEST_FILE_OFFSET
    );

    process_elf_lifecycle_test_write_u64(
        program_header +
        PROCESS_ELF_LIFECYCLE_TEST_PROGRAM_VIRTUAL_ADDRESS_OFFSET,
        PROCESS_ELF_LIFECYCLE_TEST_VADDR
    );

    process_elf_lifecycle_test_write_u64(
        program_header +
        PROCESS_ELF_LIFECYCLE_TEST_PROGRAM_FILE_SIZE_OFFSET,
        PROCESS_ELF_LIFECYCLE_TEST_FILE_SIZE
    );

    process_elf_lifecycle_test_write_u64(
        program_header +
        PROCESS_ELF_LIFECYCLE_TEST_PROGRAM_MEMORY_SIZE_OFFSET,
        0x1000
    );

    process_elf_lifecycle_test_write_u64(
        program_header +
        PROCESS_ELF_LIFECYCLE_TEST_PROGRAM_ALIGNMENT_OFFSET,
        0x1000
    );

    for (
        size_t index = 0;
        index < PROCESS_ELF_LIFECYCLE_TEST_FILE_SIZE;
        ++index
    ) {
        bytes[
            PROCESS_ELF_LIFECYCLE_TEST_FILE_OFFSET +
            index
        ] =
            (uint8_t) (0x90U + index);
    }
}

static uint64_t process_elf_lifecycle_test_read_u64(
    struct process_memory *memory,
    uint64_t address)
{
    uint64_t value;

    if (!process_memory_read(
        memory,
        address,
        &value,
        sizeof(value)
    )) {
        kernel_panic(
            "Unable to read ELF lifecycle stack word"
        );
    }

    return value;
}

static void process_elf_lifecycle_test_expect_string(
    struct process_memory *memory,
    uint64_t address,
    const char *expected)
{
    size_t index = 0;

    for (;;) {
        uint8_t actual;

        if (!process_memory_read(
            memory,
            address + index,
            &actual,
            sizeof(actual)
        )) {
            kernel_panic(
                "Unable to read ELF lifecycle stack string"
            );
        }

        if (
            actual !=
            (uint8_t) expected[index]
        ) {
            kernel_panic(
                "ELF lifecycle stack string is incorrect"
            );
        }

        if (expected[index] == '\0') {
            break;
        }

        ++index;
    }
}

static void process_elf_lifecycle_test_layout_clone(
    const struct elf64_image *image,
    size_t argc,
    const char *const argv[],
    size_t envc,
    const char *const envp[])
{
    uint64_t free_before =
        physical_free_frame_count();

    struct kernel_heap_stats heap_before;

    if (!kernel_heap_stats_get(
        &heap_before
    )) {
        kernel_panic(
            "Unable to read heap baseline before ELF layout clone test"
        );
    }

    struct process_image source_image;

    if (!process_image_create_elf64(
        &source_image,
        image,
        argc,
        argv,
        envc,
        envp
    )) {
        kernel_panic(
            "Unable to create source ELF image for layout clone test"
        );
    }

    struct process_memory clone_memory;

    if (!process_memory_create(
        &clone_memory
    )) {
        kernel_panic(
            "Unable to create destination address space for layout clone test"
        );
    }

    struct process_layout clone_layout;

    if (!process_layout_clone(
        &clone_memory,
        &source_image.memory,
        &source_image.layout,
        &clone_layout
    )) {
        kernel_panic(
            "Unable to clone ELF process layout"
        );
    }

    if (
        clone_layout.kind !=
            PROCESS_LAYOUT_KIND_ELF64 ||
        clone_layout.entry_point !=
            source_image.layout.entry_point ||
        clone_layout.initial_rsp !=
            source_image.layout.initial_rsp
    ) {
        kernel_panic(
            "Cloned ELF layout changed execution metadata"
        );
    }

    if (
        clone_layout.stack.guard_address !=
            source_image.layout.stack.guard_address ||
        clone_layout.stack.base_address !=
            source_image.layout.stack.base_address ||
        clone_layout.stack.stack_top !=
            source_image.layout.stack.stack_top ||
        clone_layout.stack.page_count !=
            source_image.layout.stack.page_count
    ) {
        kernel_panic(
            "Cloned ELF layout changed stack geometry"
        );
    }

    if (
        clone_layout.loaded_image.segments == NULL ||
        clone_layout.loaded_image.segments ==
            source_image.layout.loaded_image.segments ||
        clone_layout.loaded_image.segment_count !=
            source_image.layout.loaded_image.segment_count
    ) {
        kernel_panic(
            "Cloned ELF layout does not own independent segment metadata"
        );
    }

    if (
        clone_layout.loaded_image.segment_count != 1
    ) {
        kernel_panic(
            "Unexpected segment count in ELF layout clone test"
        );
    }

    const struct elf64_load_segment *source_segment =
        &source_image.layout.loaded_image.segments[0];

    const struct elf64_load_segment *clone_segment =
        &clone_layout.loaded_image.segments[0];

    if (
        clone_segment->virtual_address !=
            source_segment->virtual_address ||
        clone_segment->mapping_start !=
            source_segment->mapping_start ||
        clone_segment->mapping_end !=
            source_segment->mapping_end ||
        clone_segment->page_count !=
            source_segment->page_count ||
        clone_segment->writable !=
            source_segment->writable ||
        clone_segment->executable !=
            source_segment->executable
    ) {
        kernel_panic(
            "Cloned ELF segment metadata is incorrect"
        );
    }

    if (
        source_image.memory
            .address_space.pml4_physical ==
        clone_memory
            .address_space.pml4_physical
    ) {
        kernel_panic(
            "Cloned ELF layout reused source address space"
        );
    }

    if (
        !source_image.memory
            .address_space.kernel_half_shared ||
        !clone_memory
            .address_space.kernel_half_shared
    ) {
        kernel_panic(
            "ELF layout clone lost shared kernel-half policy"
        );
    }

    uint16_t kernel_pml4_index =
        paging_pml4_index(
            PROCESS_ELF_LIFECYCLE_TEST_KERNEL_ADDRESS
        );

    uint64_t source_kernel_entry =
        source_image.memory
            .address_space
            .pml4_virtual[kernel_pml4_index];

    uint64_t clone_kernel_entry =
        clone_memory
            .address_space
            .pml4_virtual[kernel_pml4_index];

    if (
        (source_kernel_entry &
         PAGE_ENTRY_PRESENT) == 0 ||
        source_kernel_entry !=
            clone_kernel_entry
    ) {
        kernel_panic(
            "ELF layout clone changed shared kernel mapping"
        );
    }

    struct paging_translation source_code;
    struct paging_translation clone_code;

    if (
        !paging_translate_address_space(
            &source_image.memory.address_space,
            source_segment->mapping_start,
            &source_code
        ) ||
        !paging_translate_address_space(
            &clone_memory.address_space,
            clone_segment->mapping_start,
            &clone_code
        )
    ) {
        kernel_panic(
            "Unable to translate cloned ELF code page"
        );
    }

    if (
        source_code.page_size !=
            PAGING_PAGE_SIZE_4K ||
        clone_code.page_size !=
            PAGING_PAGE_SIZE_4K
    ) {
        kernel_panic(
            "Cloned ELF code mapping is not 4 KiB"
        );
    }

    if (
        (
            source_code.physical_address &
            PAGE_ADDRESS_MASK_4K
        ) ==
        (
            clone_code.physical_address &
            PAGE_ADDRESS_MASK_4K
        )
    ) {
        kernel_panic(
            "Cloned ELF code page shares source physical frame"
        );
    }

    if (
        (
            source_code.pt_entry &
            PAGE_ENTRY_WRITABLE
        ) !=
        (
            clone_code.pt_entry &
            PAGE_ENTRY_WRITABLE
        ) ||
        (
            source_code.pt_entry &
            PAGE_ENTRY_NO_EXECUTE
        ) !=
        (
            clone_code.pt_entry &
            PAGE_ENTRY_NO_EXECUTE
        )
    ) {
        kernel_panic(
            "Cloned ELF code page changed permissions"
        );
    }

    if (
        (
            clone_code.pt_entry &
            PAGE_ENTRY_WRITABLE
        ) != 0 ||
        (
            clone_code.pt_entry &
            PAGE_ENTRY_NO_EXECUTE
        ) != 0
    ) {
        kernel_panic(
            "Cloned ELF code page is not read-only executable"
        );
    }

    struct paging_translation source_stack;
    struct paging_translation clone_stack;

    if (
        !paging_translate_address_space(
            &source_image.memory.address_space,
            source_image.layout.stack.base_address,
            &source_stack
        ) ||
        !paging_translate_address_space(
            &clone_memory.address_space,
            clone_layout.stack.base_address,
            &clone_stack
        )
    ) {
        kernel_panic(
            "Unable to translate cloned ELF stack page"
        );
    }

    if (
        (
            source_stack.physical_address &
            PAGE_ADDRESS_MASK_4K
        ) ==
        (
            clone_stack.physical_address &
            PAGE_ADDRESS_MASK_4K
        )
    ) {
        kernel_panic(
            "Cloned ELF stack page shares source physical frame"
        );
    }

    if (
        (
            source_stack.pt_entry &
            PAGE_ENTRY_WRITABLE
        ) == 0 ||
        (
            source_stack.pt_entry &
            PAGE_ENTRY_NO_EXECUTE
        ) == 0 ||
        (
            clone_stack.pt_entry &
            PAGE_ENTRY_WRITABLE
        ) == 0 ||
        (
            clone_stack.pt_entry &
            PAGE_ENTRY_NO_EXECUTE
        ) == 0
    ) {
        kernel_panic(
            "Cloned ELF stack page changed RW/NX policy"
        );
    }

    uint8_t source_code_bytes[
        PROCESS_ELF_LIFECYCLE_TEST_FILE_SIZE
    ];

    uint8_t clone_code_bytes[
        PROCESS_ELF_LIFECYCLE_TEST_FILE_SIZE
    ];

    if (
        !process_memory_read(
            &source_image.memory,
            source_segment->virtual_address,
            source_code_bytes,
            sizeof(source_code_bytes)
        ) ||
        !process_memory_read(
            &clone_memory,
            clone_segment->virtual_address,
            clone_code_bytes,
            sizeof(clone_code_bytes)
        )
    ) {
        kernel_panic(
            "Unable to read cloned ELF code contents"
        );
    }

    for (
        size_t index = 0;
        index < sizeof(source_code_bytes);
        ++index
    ) {
        if (
            source_code_bytes[index] !=
            clone_code_bytes[index]
        ) {
            kernel_panic(
                "Cloned ELF code contents differ from source"
            );
        }
    }

    uint64_t source_stack_word =
        process_elf_lifecycle_test_read_u64(
            &source_image.memory,
            source_image.layout.initial_rsp
        );

    uint64_t clone_stack_word =
        process_elf_lifecycle_test_read_u64(
            &clone_memory,
            clone_layout.initial_rsp
        );

    if (
        source_stack_word != clone_stack_word ||
        source_stack_word != argc
    ) {
        kernel_panic(
            "Cloned ELF stack contents differ from source"
        );
    }

    const uint64_t clone_stack_mutation =
        0x1122334455667788ULL;

    if (!process_memory_write(
        &clone_memory,
        clone_layout.initial_rsp,
        &clone_stack_mutation,
        sizeof(clone_stack_mutation)
    )) {
        kernel_panic(
            "Unable to mutate cloned ELF stack"
        );
    }

    uint64_t source_after_mutation =
        process_elf_lifecycle_test_read_u64(
            &source_image.memory,
            source_image.layout.initial_rsp
        );

    uint64_t clone_after_mutation =
        process_elf_lifecycle_test_read_u64(
            &clone_memory,
            clone_layout.initial_rsp
        );

    if (
        source_after_mutation != argc ||
        clone_after_mutation !=
            clone_stack_mutation
    ) {
        kernel_panic(
            "Cloned ELF stack is not physically independent"
        );
    }

    if (!process_layout_destroy(
        &clone_memory,
        &clone_layout
    )) {
        kernel_panic(
            "Unable to destroy cloned ELF layout"
        );
    }

    if (!process_memory_destroy(
        &clone_memory
    )) {
        kernel_panic(
            "Unable to destroy cloned ELF address space"
        );
    }

    if (!process_image_destroy(
        &source_image
    )) {
        kernel_panic(
            "Unable to destroy source ELF image after clone test"
        );
    }

    if (
        physical_free_frame_count() !=
        free_before
    ) {
        kernel_panic(
            "ELF layout clone test leaked physical frames"
        );
    }

    struct kernel_heap_stats heap_after;

    if (!kernel_heap_stats_get(
        &heap_after
    )) {
        kernel_panic(
            "Unable to read heap state after ELF layout clone test"
        );
    }

    if (
        heap_after.allocated_block_count !=
            heap_before.allocated_block_count ||
        heap_after.allocated_bytes !=
            heap_before.allocated_bytes
    ) {
        kernel_panic(
            "ELF layout clone test leaked kernel heap allocations"
        );
    }

    diagnostics_write(
        "[process] ELF layout clone test passed\n"
    );
}

static void process_elf_lifecycle_test_image_clone(
    const struct elf64_image *image,
    size_t argc,
    const char *const argv[],
    size_t envc,
    const char *const envp[])
{
    uint64_t free_before =
        physical_free_frame_count();

    struct kernel_heap_stats heap_before;

    if (!kernel_heap_stats_get(
        &heap_before
    )) {
        kernel_panic(
            "Unable to read heap baseline before ELF image clone test"
        );
    }

    struct process_image source_image;

    if (!process_image_create_elf64(
        &source_image,
        image,
        argc,
        argv,
        envc,
        envp
    )) {
        kernel_panic(
            "Unable to create source ELF image for image clone test"
        );
    }

    struct process_image clone_image;

    if (!process_image_clone(
        &clone_image,
        &source_image
    )) {
        kernel_panic(
            "Unable to clone ELF process image"
        );
    }

    if (
        source_image.memory
            .address_space.pml4_physical ==
        clone_image.memory
            .address_space.pml4_physical
    ) {
        kernel_panic(
            "Cloned process image reused source address space"
        );
    }

    if (
        clone_image.layout.kind !=
            source_image.layout.kind ||
        clone_image.layout.entry_point !=
            source_image.layout.entry_point ||
        clone_image.layout.initial_rsp !=
            source_image.layout.initial_rsp
    ) {
        kernel_panic(
            "Cloned process image changed layout execution state"
        );
    }

    if (
        clone_image.layout.loaded_image.segments == NULL ||
        clone_image.layout.loaded_image.segments ==
            source_image.layout.loaded_image.segments ||
        clone_image.layout.loaded_image.segment_count !=
            source_image.layout.loaded_image.segment_count
    ) {
        kernel_panic(
            "Cloned process image retained shared ELF metadata"
        );
    }

    uint64_t source_stack_word =
        process_elf_lifecycle_test_read_u64(
            &source_image.memory,
            source_image.layout.initial_rsp
        );

    uint64_t clone_stack_word =
        process_elf_lifecycle_test_read_u64(
            &clone_image.memory,
            clone_image.layout.initial_rsp
        );

    if (
        source_stack_word != argc ||
        clone_stack_word != argc
    ) {
        kernel_panic(
            "Cloned process image changed initial stack contents"
        );
    }

    const uint64_t clone_mutation =
        0x8877665544332211ULL;

    if (!process_memory_write(
        &clone_image.memory,
        clone_image.layout.initial_rsp,
        &clone_mutation,
        sizeof(clone_mutation)
    )) {
        kernel_panic(
            "Unable to mutate cloned process image stack"
        );
    }

    uint64_t source_after_mutation =
        process_elf_lifecycle_test_read_u64(
            &source_image.memory,
            source_image.layout.initial_rsp
        );

    uint64_t clone_after_mutation =
        process_elf_lifecycle_test_read_u64(
            &clone_image.memory,
            clone_image.layout.initial_rsp
        );

    if (
        source_after_mutation != argc ||
        clone_after_mutation !=
            clone_mutation
    ) {
        kernel_panic(
            "Cloned process image is not memory-independent"
        );
    }

    if (!process_image_destroy(
        &clone_image
    )) {
        kernel_panic(
            "Unable to destroy cloned ELF process image"
        );
    }

    if (!process_image_destroy(
        &source_image
    )) {
        kernel_panic(
            "Unable to destroy source ELF process image"
        );
    }

    if (
        physical_free_frame_count() !=
        free_before
    ) {
        kernel_panic(
            "ELF image clone test leaked physical frames"
        );
    }

    struct kernel_heap_stats heap_after;

    if (!kernel_heap_stats_get(
        &heap_after
    )) {
        kernel_panic(
            "Unable to read heap state after ELF image clone test"
        );
    }

    if (
        heap_after.allocated_block_count !=
            heap_before.allocated_block_count ||
        heap_after.allocated_bytes !=
            heap_before.allocated_bytes
    ) {
        kernel_panic(
            "ELF image clone test leaked kernel heap allocations"
        );
    }

    diagnostics_write(
        "[process] ELF image clone test passed\n"
    );
}

static void process_elf_lifecycle_test_fork_clone(
    const struct elf64_image *image,
    size_t argc,
    const char *const argv[],
    size_t envc,
    const char *const envp[])
{
    uint64_t free_before =
        physical_free_frame_count();

    struct kernel_heap_stats heap_before;

    if (!kernel_heap_stats_get(
        &heap_before
    )) {
        kernel_panic(
            "Unable to read heap baseline before fork clone test"
        );
    }

    struct process_instance *parent =
        process_create_elf64(
            image,
            argc,
            argv,
            envc,
            envp
        );

    if (parent == NULL) {
        kernel_panic(
            "Unable to create fork clone parent"
        );
    }

    struct vfs_node cwd_node;

    if (!vfs_node_initialize(
        &cwd_node,
        VFS_NODE_TYPE_DIRECTORY,
        NULL,
        NULL
    )) {
        kernel_panic(
            "Unable to initialize fork CWD fixture"
        );
    }

    if (
        !process_cwd_set(
            parent,
            &cwd_node
        ) ||
        process_cwd_get(
            parent
        ) != &cwd_node ||
        cwd_node.reference_count != 2
    ) {
        kernel_panic(
            "Unable to install fork parent CWD"
        );
    }

    struct vfs_node descriptor_node;
    struct vfs_file descriptor_file;

    if (
        !vfs_node_initialize(
            &descriptor_node,
            VFS_NODE_TYPE_REGULAR_FILE,
            NULL,
            NULL
        ) ||
        !vfs_file_initialize(
            &descriptor_file,
            &descriptor_node,
            VFS_OPEN_ACCESS_READ,
            NULL,
            NULL
        )
    ) {
        kernel_panic(
            "Unable to initialize fork descriptor fixture"
        );
    }

    size_t inherited_descriptor;

    if (
        !process_fd_table_install(
            &parent->file_descriptors,
            &descriptor_file,
            &inherited_descriptor
        ) ||
        inherited_descriptor != 0 ||
        descriptor_file.reference_count != 2
    ) {
        kernel_panic(
            "Unable to install fork parent descriptor"
        );
    }

    /*
     * Fork must clone current process memory, not merely reconstruct the
     * original ELF image.
     */
    const uint64_t parent_stack_value =
        0xA1B2C3D4E5F60718ULL;

    if (!process_memory_write(
        &parent->image.memory,
        parent->process.context.rsp,
        &parent_stack_value,
        sizeof(parent_stack_value)
    )) {
        kernel_panic(
            "Unable to initialize current parent memory before fork"
        );
    }

    struct process_context fork_context =
        parent->process.context;

    fork_context.r15 =
        0x1515151515151515ULL;

    fork_context.r12 =
        0x1212121212121212ULL;

    fork_context.rbx =
        0xBBBBBBBBBBBBBBBBULL;

    fork_context.rdi =
        0xD1D1D1D1D1D1D1D1ULL;

    fork_context.rax =
        0xF0F0F0F0F0F0F0F0ULL;

    struct process_instance *child =
        process_fork_create_child(
            parent,
            &fork_context
        );

    if (child == NULL) {
        kernel_panic(
            "Unable to create copy-based fork child"
        );
    }

    if (
        process_cwd_get(
            parent
        ) != &cwd_node ||
        process_cwd_get(
            child
        ) != &cwd_node ||
        cwd_node.reference_count != 3
    ) {
        kernel_panic(
            "Fork did not inherit current directory ownership"
        );
    }

    if (
        process_fd_table_get(
            &child->file_descriptors,
            inherited_descriptor
        ) != &descriptor_file ||
        process_fd_table_get(
            &parent->file_descriptors,
            inherited_descriptor
        ) != &descriptor_file ||
        descriptor_file.reference_count != 3
    ) {
        kernel_panic(
            "Fork did not inherit shared open-file description"
        );
    }

    descriptor_file.offset =
        73;

    if (
        process_fd_table_get(
            &child->file_descriptors,
            inherited_descriptor
        )->offset != 73
    ) {
        kernel_panic(
            "Fork child does not share open-file state"
        );
    }

    uint64_t child_pid =
        child->process.id;

    if (
        child_pid == parent->process.id ||
        child->parent != parent ||
        parent->first_child != child ||
        child->next_sibling != NULL
    ) {
        kernel_panic(
            "Fork child lifecycle relationship is incorrect"
        );
    }

    if (
        child->process.state !=
            PROCESS_STATE_READY ||
        child->process.instance != child ||
        child->process.image !=
            &child->image ||
        child->process.memory !=
            &child->image.memory ||
        child->process.layout !=
            &child->image.layout
    ) {
        kernel_panic(
            "Fork child ownership state is incorrect"
        );
    }

    if (
        child->process.context.rax != 0 ||
        child->process.context.r15 !=
            fork_context.r15 ||
        child->process.context.r12 !=
            fork_context.r12 ||
        child->process.context.rbx !=
            fork_context.rbx ||
        child->process.context.rdi !=
            fork_context.rdi ||
        child->process.context.rip !=
            fork_context.rip ||
        child->process.context.rsp !=
            fork_context.rsp ||
        child->process.context.rflags !=
            fork_context.rflags
    ) {
        kernel_panic(
            "Fork child execution context is incorrect"
        );
    }

    if (
        parent->image.memory
            .address_space.pml4_physical ==
        child->image.memory
            .address_space.pml4_physical
    ) {
        kernel_panic(
            "Fork child reused parent address space"
        );
    }

    uint64_t child_stack_value =
        process_elf_lifecycle_test_read_u64(
            &child->image.memory,
            fork_context.rsp
        );

    if (
        child_stack_value !=
            parent_stack_value
    ) {
        kernel_panic(
            "Fork child did not copy current parent memory"
        );
    }

    const uint64_t child_mutation =
        0xCAFEBABE11223344ULL;

    if (!process_memory_write(
        &child->image.memory,
        fork_context.rsp,
        &child_mutation,
        sizeof(child_mutation)
    )) {
        kernel_panic(
            "Unable to mutate fork child memory"
        );
    }

    uint64_t parent_after_mutation =
        process_elf_lifecycle_test_read_u64(
            &parent->image.memory,
            fork_context.rsp
        );

    uint64_t child_after_mutation =
        process_elf_lifecycle_test_read_u64(
            &child->image.memory,
            fork_context.rsp
        );

    if (
        parent_after_mutation !=
            parent_stack_value ||
        child_after_mutation !=
            child_mutation
    ) {
        kernel_panic(
            "Fork parent and child memory are not independent"
        );
    }

    /*
     * Terminate and reap the child through the exact lifecycle that forked
     * children will use in production.
     */
    child->process.state =
        PROCESS_STATE_TERMINATED;

    child->process.termination_reason =
        PROCESS_TERMINATION_EXITED;

    child->process.exit_status = 23;

    if (!scheduler_unregister_terminated(
        &child->process
    )) {
        kernel_panic(
            "Unable to unregister fork clone child"
        );
    }

    process_lifecycle_notify_terminated(
        &child->process
    );

    if (
        process_cwd_get(
            child
        ) != NULL ||
        process_cwd_get(
            parent
        ) != &cwd_node ||
        cwd_node.reference_count != 2
    ) {
        kernel_panic(
            "Fork child termination did not release CWD ownership"
        );
    }

    if (
        process_fd_table_get(
            &child->file_descriptors,
            inherited_descriptor
        ) != NULL ||
        process_fd_table_get(
            &parent->file_descriptors,
            inherited_descriptor
        ) != &descriptor_file ||
        descriptor_file.reference_count != 2
    ) {
        kernel_panic(
            "Fork child termination did not release descriptor ownership"
        );
    }

    struct process_wait_status wait_status;

    if (
        process_waitpid_try_reap(
            parent,
            child_pid,
            &wait_status
        ) != PROCESS_WAIT_RESULT_REAPED
    ) {
        kernel_panic(
            "Unable to reap fork clone child"
        );
    }

    if (
        wait_status.pid != child_pid ||
        wait_status.termination_reason !=
            PROCESS_TERMINATION_EXITED ||
        wait_status.exit_status != 23 ||
        parent->first_child != NULL
    ) {
        kernel_panic(
            "Fork clone child reaping produced incorrect state"
        );
    }

    parent->process.state =
        PROCESS_STATE_TERMINATED;

    parent->process.termination_reason =
        PROCESS_TERMINATION_EXITED;

    parent->process.exit_status = 0;

    if (!scheduler_unregister_terminated(
        &parent->process
    )) {
        kernel_panic(
            "Unable to unregister fork clone parent"
        );
    }

    process_lifecycle_notify_terminated(
        &parent->process
    );

    if (
        process_cwd_get(
            parent
        ) != NULL ||
        cwd_node.reference_count != 1
    ) {
        kernel_panic(
            "Fork parent termination did not release CWD ownership"
        );
    }

    if (
        process_fd_table_get(
            &parent->file_descriptors,
            inherited_descriptor
        ) != NULL ||
        descriptor_file.reference_count != 1
    ) {
        kernel_panic(
            "Fork parent termination did not release descriptor ownership"
        );
    }

    if (!process_release_terminated(
        parent
    )) {
        kernel_panic(
            "Unable to release fork clone parent"
        );
    }

    if (!vfs_node_release(
        &cwd_node
    )) {
        kernel_panic(
            "Fork CWD fixture cleanup failed"
        );
    }

    if (
        !vfs_file_release(
            &descriptor_file
        ) ||
        !vfs_node_release(
            &descriptor_node
        )
    ) {
        kernel_panic(
            "Fork descriptor fixture cleanup failed"
        );
    }

    if (
        physical_free_frame_count() !=
        free_before
    ) {
        kernel_panic(
            "Fork clone test leaked physical frames"
        );
    }

    struct kernel_heap_stats heap_after;

    if (!kernel_heap_stats_get(
        &heap_after
    )) {
        kernel_panic(
            "Unable to read heap state after fork clone test"
        );
    }

    if (
        heap_after.allocated_block_count !=
            heap_before.allocated_block_count ||
        heap_after.allocated_bytes !=
            heap_before.allocated_bytes
    ) {
        kernel_panic(
            "Fork clone test leaked kernel heap allocations"
        );
    }

    diagnostics_write(
        "[process] Copy-based fork lifecycle test passed\n"
    );
}

static void process_elf_lifecycle_test_fork_rollback(
    const struct elf64_image *image,
    size_t argc,
    const char *const argv[],
    size_t envc,
    const char *const envp[])
{
    uint64_t free_before =
        physical_free_frame_count();

    struct kernel_heap_stats heap_before;

    if (!kernel_heap_stats_get(
        &heap_before
    )) {
        kernel_panic(
            "Unable to read heap baseline before fork rollback test"
        );
    }

    struct process_instance *parent =
        process_create_elf64(
            image,
            argc,
            argv,
            envc,
            envp
        );

    if (parent == NULL) {
        kernel_panic(
            "Unable to create fork rollback parent"
        );
    }

    struct vfs_node rollback_cwd_node;

    if (
        !vfs_node_initialize(
            &rollback_cwd_node,
            VFS_NODE_TYPE_DIRECTORY,
            NULL,
            NULL
        ) ||
        !process_cwd_set(
            parent,
            &rollback_cwd_node
        ) ||
        rollback_cwd_node.reference_count != 2
    ) {
        kernel_panic(
            "Unable to initialize fork rollback CWD fixture"
        );
    }

    struct vfs_node rollback_descriptor_node;
    struct vfs_file rollback_descriptor_file;

    if (
        !vfs_node_initialize(
            &rollback_descriptor_node,
            VFS_NODE_TYPE_REGULAR_FILE,
            NULL,
            NULL
        ) ||
        !vfs_file_initialize(
            &rollback_descriptor_file,
            &rollback_descriptor_node,
            VFS_OPEN_ACCESS_READ,
            NULL,
            NULL
        )
    ) {
        kernel_panic(
            "Unable to initialize fork rollback descriptor fixture"
        );
    }

    size_t rollback_descriptor;

    if (
        !process_fd_table_install(
            &parent->file_descriptors,
            &rollback_descriptor_file,
            &rollback_descriptor
        ) ||
        rollback_descriptor != 0 ||
        rollback_descriptor_file.reference_count != 2
    ) {
        kernel_panic(
            "Unable to install fork rollback descriptor"
        );
    }

    rollback_descriptor_file.offset =
        117;

    struct process_instance
        *fillers[SCHEDULER_MAX_PROCESSES - 1];

    for (
        size_t index = 0;
        index < SCHEDULER_MAX_PROCESSES - 1;
        ++index
    ) {
        fillers[index] =
            process_create_elf64(
                image,
                argc,
                argv,
                envc,
                envp
            );

        if (fillers[index] == NULL) {
            kernel_panic(
                "Unable to fill scheduler for fork rollback test"
            );
        }
    }

    /*
     * The scheduler now contains exactly SCHEDULER_MAX_PROCESSES
     * registrations. fork can still allocate and clone the child, but its
     * final scheduler_add() must fail.
     */
    uint64_t free_before_failed_fork =
        physical_free_frame_count();

    struct kernel_heap_stats
        heap_before_failed_fork;

    if (!kernel_heap_stats_get(
        &heap_before_failed_fork
    )) {
        kernel_panic(
            "Unable to read heap before failed fork"
        );
    }

    /*
     * Probe the next unpublished PID, then return it. A correctly rolled-back
     * fork must leave this same PID available afterward.
     */
    uint64_t expected_child_pid;

    if (
        !process_pid_allocate(
            &expected_child_pid
        ) ||
        !process_pid_release(
            expected_child_pid
        )
    ) {
        kernel_panic(
            "Unable to probe PID before fork rollback test"
        );
    }

    struct process_context fork_context =
        parent->process.context;

    struct process_instance *child =
        process_fork_create_child(
            parent,
            &fork_context
        );

    if (child != NULL) {
        kernel_panic(
            "Fork succeeded with full scheduler"
        );
    }

    if (
        process_fd_table_get(
            &parent->file_descriptors,
            rollback_descriptor
        ) != &rollback_descriptor_file ||
        rollback_descriptor_file.reference_count != 2 ||
        rollback_descriptor_file.offset != 117
    ) {
        kernel_panic(
            "Failed fork leaked inherited descriptor ownership"
        );
    }

    /*
     * The failed child temporarily inherited the parent's CWD while its process
     * instance was being cloned. Rollback must have released that child-owned
     * reference, leaving only the fixture and parent references.
     */
    if (
        process_cwd_get(
            parent
        ) != &rollback_cwd_node ||
        rollback_cwd_node.reference_count != 2
    ) {
        kernel_panic(
            "Failed fork leaked inherited CWD ownership"
        );
    }

    /*
     * Publication happens only after scheduler registration succeeds.
     */
    if (
        parent->first_child != NULL ||
        parent->wait_active ||
        parent->wait_child_pid != 0
    ) {
        kernel_panic(
            "Failed fork published child lifecycle state"
        );
    }

    if (
        physical_free_frame_count() !=
        free_before_failed_fork
    ) {
        kernel_panic(
            "Failed fork leaked physical frames"
        );
    }

    struct kernel_heap_stats
        heap_after_failed_fork;

    if (!kernel_heap_stats_get(
        &heap_after_failed_fork
    )) {
        kernel_panic(
            "Unable to read heap after failed fork"
        );
    }

    if (
        heap_after_failed_fork.allocated_block_count !=
            heap_before_failed_fork.allocated_block_count ||
        heap_after_failed_fork.allocated_bytes !=
            heap_before_failed_fork.allocated_bytes
    ) {
        kernel_panic(
            "Failed fork leaked kernel heap allocations"
        );
    }

    uint64_t pid_after_failed_fork;

    if (!process_pid_allocate(
        &pid_after_failed_fork
    )) {
        kernel_panic(
            "Unable to probe PID after failed fork"
        );
    }

    if (
        pid_after_failed_fork !=
        expected_child_pid
    ) {
        kernel_panic(
            "Failed fork did not roll back PID allocation"
        );
    }

    if (!process_pid_release(
        pid_after_failed_fork
    )) {
        kernel_panic(
            "Unable to release PID rollback probe"
        );
    }

    /*
     * Clean the synthetic scheduler saturation.
     */
    for (
        size_t index =
            SCHEDULER_MAX_PROCESSES - 1;
        index > 0;
        --index
    ) {
        struct process_instance *instance =
            fillers[index - 1];

        instance->process.state =
            PROCESS_STATE_TERMINATED;

        instance->process.termination_reason =
            PROCESS_TERMINATION_EXITED;

        instance->process.exit_status = 0;

        if (!scheduler_unregister_terminated(
            &instance->process
        )) {
            kernel_panic(
                "Unable to unregister fork rollback filler"
            );
        }

        if (!process_release_terminated(
            instance
        )) {
            kernel_panic(
                "Unable to release fork rollback filler"
            );
        }
    }

    parent->process.state =
        PROCESS_STATE_TERMINATED;

    parent->process.termination_reason =
        PROCESS_TERMINATION_EXITED;

    parent->process.exit_status = 0;

    if (!scheduler_unregister_terminated(
        &parent->process
    )) {
        kernel_panic(
            "Unable to unregister fork rollback parent"
        );
    }

    if (!process_release_terminated(
        parent
    )) {
        kernel_panic(
            "Unable to release fork rollback parent"
        );
    }

    if (
        rollback_cwd_node.reference_count != 1
    ) {
        kernel_panic(
            "Fork rollback parent teardown retained CWD ownership"
        );
    }

    if (!vfs_node_release(
        &rollback_cwd_node
    )) {
        kernel_panic(
            "Fork rollback CWD fixture cleanup failed"
        );
    }

    if (
        rollback_descriptor_file.reference_count != 1 ||
        !vfs_file_release(
            &rollback_descriptor_file
        ) ||
        !vfs_node_release(
            &rollback_descriptor_node
        )
    ) {
        kernel_panic(
            "Fork rollback descriptor fixture cleanup failed"
        );
    }

    if (
        physical_free_frame_count() !=
        free_before
    ) {
        kernel_panic(
            "Fork rollback test leaked physical frames"
        );
    }

    struct kernel_heap_stats heap_after;

    if (!kernel_heap_stats_get(
        &heap_after
    )) {
        kernel_panic(
            "Unable to read final fork rollback heap state"
        );
    }

    if (
        heap_after.allocated_block_count !=
            heap_before.allocated_block_count ||
        heap_after.allocated_bytes !=
            heap_before.allocated_bytes
    ) {
        kernel_panic(
            "Fork rollback test leaked kernel heap allocations"
        );
    }

    diagnostics_write(
        "[process] Copy-based fork rollback test passed\n"
    );
}

void process_elf_lifecycle_test_run(void)
{
    uint64_t free_before =
        physical_free_frame_count();

    uint8_t bytes[
        PROCESS_ELF_LIFECYCLE_TEST_IMAGE_SIZE
    ];

    process_elf_lifecycle_test_build_image(
        bytes,
        sizeof(bytes)
    );

    struct elf64_image image;

    if (!elf64_parse(
        bytes,
        sizeof(bytes),
        &image
    )) {
        kernel_panic(
            "ELF lifecycle fixture failed to parse"
        );
    }

    const char *argv[] = {
        "hello",
        "world",
    };

    const char *envp[] = {
        "TERM=myos",
    };

    process_elf_lifecycle_test_layout_clone(
        &image,
        2,
        argv,
        1,
        envp
    );

    process_elf_lifecycle_test_image_clone(
        &image,
        2,
        argv,
        1,
        envp
    );

    process_elf_lifecycle_test_fork_clone(
        &image,
        2,
        argv,
        1,
        envp
    );

    process_elf_lifecycle_test_fork_rollback(
        &image,
        2,
        argv,
        1,
        envp
    );

    uint64_t image_free_before =
        physical_free_frame_count();

    struct process_image owned_image;

    if (!process_image_create_elf64(
        &owned_image,
        &image,
        2,
        argv,
        1,
        envp
    )) {
        kernel_panic(
            "Unable to create owned ELF process image"
        );
    }

    if (
        owned_image.layout.kind !=
        PROCESS_LAYOUT_KIND_ELF64
    ) {
        kernel_panic(
            "Owned ELF process image has incorrect layout kind"
        );
    }

    if (
        owned_image.layout.entry_point !=
        image.entry_point
    ) {
        kernel_panic(
            "Owned ELF process image has incorrect entry point"
        );
    }

    if (
        owned_image.memory.address_space.pml4_physical == 0 ||
        owned_image.memory.address_space.pml4_virtual == NULL
    ) {
        kernel_panic(
            "Owned ELF process image has no address space"
        );
    }

    if (!process_image_destroy(
        &owned_image
    )) {
        kernel_panic(
            "Unable to destroy owned ELF process image"
        );
    }

    if (physical_free_frame_count() != image_free_before) {
        kernel_panic(
            "Owned ELF process image leaked physical frames"
        );
    }

    struct kernel_heap_stats exec_heap_before;

    if (!kernel_heap_stats_get(
        &exec_heap_before
    )) {
        kernel_panic(
            "Unable to read heap state before exec candidate test"
        );
    }

    uint64_t exec_free_before =
        physical_free_frame_count();

    struct process_exec_candidate candidate;

    candidate.prepared = false;

    if (!process_exec_candidate_prepare_elf64(
        &candidate,
        &image,
        2,
        argv,
        1,
        envp
    )) {
        kernel_panic(
            "Unable to prepare exec candidate"
        );
    }

    if (!candidate.prepared) {
        kernel_panic(
            "Exec candidate was not marked prepared"
        );
    }

    if (
        candidate.image.layout.kind !=
        PROCESS_LAYOUT_KIND_ELF64
    ) {
        kernel_panic(
            "Exec candidate has incorrect layout kind"
        );
    }

    if (
        candidate.context.rip !=
        candidate.image.layout.entry_point
    ) {
        kernel_panic(
            "Exec candidate RIP is incorrect"
        );
    }

    if (
        candidate.context.rsp !=
        candidate.image.layout.initial_rsp
    ) {
        kernel_panic(
            "Exec candidate RSP is incorrect"
        );
    }

    if (candidate.context.rflags != 0x202) {
        kernel_panic(
            "Exec candidate RFLAGS are incorrect"
        );
    }

    if (!process_exec_candidate_discard(
        &candidate
    )) {
        kernel_panic(
            "Unable to discard exec candidate"
        );
    }

    if (candidate.prepared) {
        kernel_panic(
            "Discarded exec candidate remained prepared"
        );
    }

    if (
        physical_free_frame_count() !=
        exec_free_before
    ) {
        kernel_panic(
            "Exec candidate leaked physical frames"
        );
    }

    struct kernel_heap_stats exec_heap_after;

    if (!kernel_heap_stats_get(
        &exec_heap_after
    )) {
        kernel_panic(
            "Unable to read heap state after exec candidate test"
        );
    }

    if (
        exec_heap_after.allocated_block_count !=
            exec_heap_before.allocated_block_count ||
        exec_heap_after.allocated_bytes !=
            exec_heap_before.allocated_bytes
    ) {
        kernel_panic(
            "Exec candidate leaked kernel heap allocations"
        );
    }

    struct elf64_image invalid_exec_image =
        image;

    invalid_exec_image.entry_point = 0;

    uint64_t failed_exec_free_before =
        physical_free_frame_count();

    struct kernel_heap_stats failed_exec_heap_before;

    if (!kernel_heap_stats_get(
        &failed_exec_heap_before
    )) {
        kernel_panic(
            "Unable to read heap state before failed exec candidate"
        );
    }

    struct process_exec_candidate failed_candidate;

    failed_candidate.prepared = false;

    if (process_exec_candidate_prepare_elf64(
        &failed_candidate,
        &invalid_exec_image,
        2,
        argv,
        1,
        envp
    )) {
        kernel_panic(
            "Invalid ELF unexpectedly produced exec candidate"
        );
    }

    if (failed_candidate.prepared) {
        kernel_panic(
            "Failed exec candidate remained prepared"
        );
    }

    if (
        physical_free_frame_count() !=
        failed_exec_free_before
    ) {
        kernel_panic(
            "Failed exec candidate leaked physical frames"
        );
    }

    struct kernel_heap_stats failed_exec_heap_after;

    if (!kernel_heap_stats_get(
        &failed_exec_heap_after
    )) {
        kernel_panic(
            "Unable to read heap state after failed exec candidate"
        );
    }

    if (
        failed_exec_heap_after.allocated_block_count !=
            failed_exec_heap_before.allocated_block_count ||
        failed_exec_heap_after.allocated_bytes !=
            failed_exec_heap_before.allocated_bytes
    ) {
        kernel_panic(
            "Failed exec candidate leaked kernel heap allocations"
        );
    }

    uint64_t exec_commit_free_before =
        physical_free_frame_count();

    struct kernel_heap_stats exec_commit_heap_before;

    if (!kernel_heap_stats_get(
        &exec_commit_heap_before
    )) {
        kernel_panic(
            "Unable to read heap state before exec commit test"
        );
    }

    const char *exec_initial_argv[] = {
        "before",
    };

    struct process_instance exec_instance;

    if (!process_instance_prepare_elf64(
        &exec_instance,
        PROCESS_ELF_LIFECYCLE_TEST_PID + 2,
        &image,
        1,
        exec_initial_argv,
        0,
        NULL
    )) {
        kernel_panic(
            "Unable to prepare exec commit process"
        );
    }

    struct vfs_node exec_cwd_node;

    if (
        !vfs_node_initialize(
            &exec_cwd_node,
            VFS_NODE_TYPE_DIRECTORY,
            NULL,
            NULL
        ) ||
        !process_cwd_set(
            &exec_instance,
            &exec_cwd_node
        ) ||
        exec_cwd_node.reference_count != 2
    ) {
        kernel_panic(
            "Unable to initialize exec CWD fixture"
        );
    }

    struct vfs_node exec_descriptor_node;
    struct vfs_file exec_descriptor_file;

    if (
        !vfs_node_initialize(
            &exec_descriptor_node,
            VFS_NODE_TYPE_REGULAR_FILE,
            NULL,
            NULL
        ) ||
        !vfs_file_initialize(
            &exec_descriptor_file,
            &exec_descriptor_node,
            VFS_OPEN_ACCESS_READ,
            NULL,
            NULL
        )
    ) {
        kernel_panic(
            "Unable to initialize exec descriptor fixture"
        );
    }

    size_t exec_descriptor;

    if (
        !process_fd_table_install(
            &exec_instance.file_descriptors,
            &exec_descriptor_file,
            &exec_descriptor
        ) ||
        exec_descriptor != 0
    ) {
        kernel_panic(
            "Unable to install exec descriptor fixture"
        );
    }

    exec_descriptor_file.offset =
        91;

    if (
        exec_instance.process.image !=
        &exec_instance.image
    ) {
        kernel_panic(
            "Exec commit process has incorrect image ownership link"
        );
    }

    uint64_t exec_pid =
        exec_instance.process.id;

    uint64_t old_exec_cr3 =
        exec_instance.image
            .memory
            .address_space
            .pml4_physical &
        PAGE_ADDRESS_MASK_4K;

    const char *exec_replacement_argv[] = {
        "after",
        "replacement",
    };

    struct process_exec_candidate commit_candidate;

    commit_candidate.prepared = false;

    if (!process_exec_candidate_prepare_elf64(
        &commit_candidate,
        &image,
        2,
        exec_replacement_argv,
        0,
        NULL
    )) {
        kernel_panic(
            "Unable to prepare exec commit candidate"
        );
    }

    uint64_t new_exec_cr3 =
        commit_candidate.image
            .memory
            .address_space
            .pml4_physical &
        PAGE_ADDRESS_MASK_4K;

    uint64_t expected_exec_rip =
        commit_candidate.context.rip;

    uint64_t expected_exec_rsp =
        commit_candidate.context.rsp;

    if (
        old_exec_cr3 == 0 ||
        new_exec_cr3 == 0 ||
        old_exec_cr3 == new_exec_cr3
    ) {
        kernel_panic(
            "Exec commit test did not create independent address spaces"
        );
    }

    exec_instance.process.state =
        PROCESS_STATE_RUNNING;

    if (!paging_address_space_activate(
        &exec_instance.image.memory.address_space
    )) {
        kernel_panic(
            "Unable to activate original exec process image"
        );
    }

    if (!process_exec_candidate_commit_current(
        &exec_instance.process,
        &commit_candidate
    )) {
        kernel_panic(
            "Unable to commit replacement process image"
        );
    }

    if (
        process_fd_table_get(
            &exec_instance.file_descriptors,
            exec_descriptor
        ) != &exec_descriptor_file ||
        exec_descriptor_file.reference_count != 2 ||
        exec_descriptor_file.offset != 91
    ) {
        kernel_panic(
            "Exec did not preserve file descriptor state"
        );
    }

    if (
        process_cwd_get(
            &exec_instance
        ) != &exec_cwd_node ||
        exec_cwd_node.reference_count != 2
    ) {
        kernel_panic(
            "Exec did not preserve current directory state"
        );
    }

    if (commit_candidate.prepared) {
        kernel_panic(
            "Committed exec candidate remained prepared"
        );
    }

    if (
        exec_instance.process.id !=
            exec_pid ||
        exec_instance.process.state !=
            PROCESS_STATE_RUNNING
    ) {
        kernel_panic(
            "Exec commit modified process identity or state"
        );
    }

    if (
        exec_instance.process.image !=
            &exec_instance.image ||
        exec_instance.process.memory !=
            &exec_instance.image.memory ||
        exec_instance.process.layout !=
            &exec_instance.image.layout
    ) {
        kernel_panic(
            "Exec commit produced incorrect image ownership links"
        );
    }

    if (
        exec_instance.image
            .memory
            .address_space
            .pml4_physical !=
            new_exec_cr3
    ) {
        kernel_panic(
            "Exec commit retained incorrect address space"
        );
    }

    if (
        (
            paging_read_cr3() &
            PAGE_ADDRESS_MASK_4K
        ) != new_exec_cr3
    ) {
        kernel_panic(
            "Exec commit did not activate replacement address space"
        );
    }

    if (
        exec_instance.process.context.rip !=
            expected_exec_rip ||
        exec_instance.process.context.rsp !=
            expected_exec_rsp ||
        exec_instance.process.context.rflags !=
            0x202
    ) {
        kernel_panic(
            "Exec commit installed incorrect userspace context"
        );
    }

    if (!paging_address_space_activate(
        paging_kernel_address_space()
    )) {
        kernel_panic(
            "Unable to restore kernel address space after exec commit test"
        );
    }

    exec_instance.process.state =
        PROCESS_STATE_READY;

    if (!process_instance_discard(
        &exec_instance
    )) {
        kernel_panic(
            "Unable to discard committed exec process"
        );
    }

    if (
        exec_cwd_node.reference_count != 1
    ) {
        kernel_panic(
            "Exec process discard retained CWD ownership"
        );
    }

    if (exec_descriptor_file.reference_count != 1) {
        kernel_panic(
            "Exec process discard retained descriptor ownership"
        );
    }

    if (!vfs_node_release(
        &exec_cwd_node
    )) {
        kernel_panic(
            "Exec CWD fixture cleanup failed"
        );
    }

    if (
        !vfs_file_release(
            &exec_descriptor_file
        ) ||
        !vfs_node_release(
            &exec_descriptor_node
        )
    ) {
        kernel_panic(
            "Exec descriptor fixture cleanup failed"
        );
    }

    if (
        physical_free_frame_count() !=
        exec_commit_free_before
    ) {
        kernel_panic(
            "Exec commit test leaked physical frames"
        );
    }

    struct kernel_heap_stats exec_commit_heap_after;

    if (!kernel_heap_stats_get(
        &exec_commit_heap_after
    )) {
        kernel_panic(
            "Unable to read heap state after exec commit test"
        );
    }

    if (
        exec_commit_heap_after.allocated_block_count !=
            exec_commit_heap_before.allocated_block_count ||
        exec_commit_heap_after.allocated_bytes !=
            exec_commit_heap_before.allocated_bytes
    ) {
        kernel_panic(
            "Exec commit test leaked kernel heap allocations"
        );
    }

    uint64_t discard_free_before =
        physical_free_frame_count();

    struct process_instance discarded_instance;

    if (!process_instance_prepare_elf64(
        &discarded_instance,
        PROCESS_ELF_LIFECYCLE_TEST_PID + 1,
        &image,
        2,
        argv,
        1,
        envp
    )) {
        kernel_panic(
            "Unable to prepare discardable ELF process instance"
        );
    }

    if (
        discarded_instance.process.state !=
        PROCESS_STATE_READY
    ) {
        kernel_panic(
            "Discardable ELF process instance is not ready"
        );
    }

    if (!process_instance_discard(
        &discarded_instance
    )) {
        kernel_panic(
            "Unable to discard ELF process instance"
        );
    }

    if (
        discarded_instance.process.memory != NULL ||
        discarded_instance.process.layout != NULL
    ) {
        kernel_panic(
            "Discarded ELF process retained borrowed references"
        );
    }

    if (
        physical_free_frame_count() !=
        discard_free_before
    ) {
        kernel_panic(
            "Discarded ELF process instance leaked physical frames"
        );
    }

    uint64_t pid_probe_a;
    uint64_t pid_probe_b;

    if (!process_pid_allocate(
        &pid_probe_a
    )) {
        kernel_panic(
            "Unable to allocate PID rollback probe"
        );
    }

    if (!process_pid_release(
        pid_probe_a
    )) {
        kernel_panic(
            "Unable to release PID rollback probe"
        );
    }

    if (!process_pid_allocate(
        &pid_probe_b
    )) {
        kernel_panic(
            "Unable to reallocate PID rollback probe"
        );
    }

    if (pid_probe_b != pid_probe_a) {
        kernel_panic(
            "PID rollback did not restore allocator capacity"
        );
    }

    if (!process_pid_release(
        pid_probe_b
    )) {
        kernel_panic(
            "Unable to release reallocated PID rollback probe"
        );
    }

    struct kernel_heap_stats dynamic_heap_before;

    if (!kernel_heap_stats_get(
        &dynamic_heap_before
    )) {
        kernel_panic(
            "Unable to read heap state before dynamic process creation"
        );
    }

    struct process_instance *dynamic_instance = process_create_elf64(
        &image,
        2,
        argv,
        1,
        envp
    );

    if (dynamic_instance == NULL) {
        kernel_panic(
            "Unable to dynamically create ELF process"
        );
    }

    if (
        dynamic_instance->process.state !=
        PROCESS_STATE_READY
    ) {
        kernel_panic(
            "Dynamically created ELF process is not ready"
        );
    }

    if (
        dynamic_instance->process.memory !=
        &dynamic_instance->image.memory ||
        dynamic_instance->process.layout !=
        &dynamic_instance->image.layout
    ) {
        kernel_panic(
            "Dynamic ELF process ownership links are incorrect"
        );
    }

    dynamic_instance->process.state = PROCESS_STATE_TERMINATED;

    if (!scheduler_unregister_terminated(
        &dynamic_instance->process
    )) {
        kernel_panic(
            "Unable to unregister dynamic ELF lifecycle process"
        );
    }

    if (!process_release_terminated(
        dynamic_instance
    )) {
        kernel_panic(
            "Unable to release dynamic ELF lifecycle process"
        );
    }

    struct kernel_heap_stats dynamic_heap_after;

    if (!kernel_heap_stats_get(
        &dynamic_heap_after
    )) {
        kernel_panic(
            "Unable to read heap state after dynamic process release"
        );
    }

    if (
        dynamic_heap_after.allocated_block_count !=
            dynamic_heap_before.allocated_block_count ||
        dynamic_heap_after.allocated_bytes !=
            dynamic_heap_before.allocated_bytes
    ) {
        kernel_panic(
            "Dynamic ELF process leaked kernel heap allocations"
        );
    }

    struct process scheduler_fillers[
        SCHEDULER_MAX_PROCESSES
    ];

    uint64_t scheduler_failure_free_before =
        physical_free_frame_count();

    struct kernel_heap_stats scheduler_failure_heap_before;

    if (!kernel_heap_stats_get(
        &scheduler_failure_heap_before
    )) {
        kernel_panic(
            "Unable to read heap state before scheduler rollback test"
        );
    }

    for (size_t index = 0;
         index < SCHEDULER_MAX_PROCESSES;
         ++index) {

        scheduler_fillers[index].id =
            1000 + index;

        scheduler_fillers[index].state =
            PROCESS_STATE_READY;

        scheduler_fillers[index].termination_reason =
            PROCESS_TERMINATION_NONE;

        scheduler_fillers[index].exit_status = 0;

        scheduler_fillers[index].memory = NULL;
        scheduler_fillers[index].layout = NULL;

        if (!scheduler_add(
            &scheduler_fillers[index]
        )) {
            kernel_panic(
                "Unable to fill scheduler for process creation rollback test"
            );
        }
    }

    struct process_instance *failed_instance =
        process_create_elf64(
            &image,
            2,
            argv,
            1,
            envp
        );

    if (failed_instance != NULL) {
        kernel_panic(
            "Dynamic process creation succeeded with full scheduler"
        );
    }

    if (
        physical_free_frame_count() !=
        scheduler_failure_free_before
    ) {
        kernel_panic(
            "Failed dynamic process creation leaked physical frames"
        );
    }

    struct kernel_heap_stats scheduler_failure_heap_after;

    if (!kernel_heap_stats_get(
        &scheduler_failure_heap_after
    )) {
        kernel_panic(
            "Unable to read heap state after scheduler rollback test"
        );
    }

    if (
        scheduler_failure_heap_after.allocated_block_count !=
            scheduler_failure_heap_before.allocated_block_count ||
        scheduler_failure_heap_after.allocated_bytes !=
            scheduler_failure_heap_before.allocated_bytes
    ) {
        kernel_panic(
            "Failed dynamic process creation leaked kernel heap allocations"
        );
    }

    for (size_t index = 0;
         index < SCHEDULER_MAX_PROCESSES;
         ++index) {

        scheduler_fillers[index].state =
            PROCESS_STATE_TERMINATED;

        if (!scheduler_unregister_terminated(
            &scheduler_fillers[index]
        )) {
            kernel_panic(
                "Unable to remove scheduler rollback test filler"
            );
        }
    }

        uint64_t wait_free_before =
        physical_free_frame_count();

    struct kernel_heap_stats wait_heap_before;

    if (!kernel_heap_stats_get(
        &wait_heap_before
    )) {
        kernel_panic(
            "Unable to read heap state before wait lifecycle test"
        );
    }

    struct process_instance *wait_parent =
        process_create_elf64(
            &image,
            2,
            argv,
            1,
            envp
        );

    if (wait_parent == NULL) {
        kernel_panic(
            "Unable to create wait lifecycle parent"
        );
    }

    struct process_instance *wait_child =
        process_create_child_elf64(
            wait_parent,
            &image,
            2,
            argv,
            1,
            envp
        );

    if (wait_child == NULL) {
        kernel_panic(
            "Unable to create wait lifecycle child"
        );
    }

    uint64_t wait_child_pid =
        wait_child->process.id;

    if (
        wait_child->parent != wait_parent ||
        wait_parent->first_child != wait_child
    ) {
        kernel_panic(
            "Wait lifecycle parent-child relationship is incorrect"
        );
    }

    struct process_wait_status wait_status;

    if (
        process_waitpid_peek(
            wait_parent,
            wait_child_pid,
            &wait_status
        ) != PROCESS_WAIT_RESULT_NOT_TERMINATED
    ) {
        kernel_panic(
            "waitpid peek reported running child as terminated"
        );
    }

    if (
        process_wait_peek(
            wait_parent,
            &wait_status
        ) != PROCESS_WAIT_RESULT_NOT_TERMINATED
    ) {
        kernel_panic(
            "wait peek reported running child as terminated"
        );
    }

    if (
        process_waitpid_try_reap(
            wait_parent,
            wait_child_pid,
            &wait_status
        ) != PROCESS_WAIT_RESULT_NOT_TERMINATED
    ) {
        kernel_panic(
            "waitpid unexpectedly reaped running child"
        );
    }

    if (!process_wait_register(
        wait_parent,
        wait_child_pid
    )) {
        kernel_panic(
            "Unable to register wait lifecycle request"
        );
    }

    if (
        !wait_parent->wait_active ||
        wait_parent->wait_child_pid !=
            wait_child_pid
    ) {
        kernel_panic(
            "Wait lifecycle request metadata is incorrect"
        );
    }

    wait_parent->process.state =
        PROCESS_STATE_BLOCKED;

    wait_child->process.state =
        PROCESS_STATE_TERMINATED;

    wait_child->process.termination_reason =
        PROCESS_TERMINATION_EXITED;

    wait_child->process.exit_status = 37;

    if (!scheduler_unregister_terminated(
        &wait_child->process
    )) {
        kernel_panic(
            "Unable to unregister wait lifecycle child"
        );
    }

    uint64_t wait_notify_rflags;

    __asm__ volatile (
        "pushfq\n\t"
        "popq %0"
        : "=r"(wait_notify_rflags)
        :
        : "memory"
    );

    interrupts_disable();

    if (!process_wait_notify_terminated(
        wait_child
    )) {
        kernel_panic(
            "Terminated child failed to wake waiting parent"
        );
    }

    if ((wait_notify_rflags & (1ULL << 9)) != 0) {
        interrupts_enable();
    }

    if (
        wait_parent->process.state !=
            PROCESS_STATE_READY ||
        !wait_parent->wait_active ||
        wait_parent->wait_child_pid !=
            wait_child_pid
    ) {
        kernel_panic(
            "Waiting parent resumed with incorrect wait state"
        );
    }

    if (
        process_waitpid_peek(
            wait_parent,
            wait_child_pid,
            &wait_status
        ) != PROCESS_WAIT_RESULT_TERMINATED
    ) {
        kernel_panic(
            "waitpid peek failed to observe terminated child"
        );
    }

    if (
        wait_status.pid != wait_child_pid ||
        wait_status.termination_reason !=
            PROCESS_TERMINATION_EXITED ||
        wait_status.exit_status != 37
    ) {
        kernel_panic(
            "waitpid peek returned incorrect child status"
        );
    }

    if (
        wait_parent->first_child != wait_child ||
        !wait_parent->wait_active
    ) {
        kernel_panic(
            "waitpid peek consumed child lifecycle state"
        );
    }

    struct process_wait_status wait_any_status;

    if (
        process_wait_peek(
            wait_parent,
            &wait_any_status
        ) != PROCESS_WAIT_RESULT_TERMINATED
    ) {
        kernel_panic(
            "wait peek failed to observe terminated child"
        );
    }

    if (
        wait_any_status.pid != wait_child_pid ||
        wait_any_status.termination_reason !=
            PROCESS_TERMINATION_EXITED ||
        wait_any_status.exit_status != 37
    ) {
        kernel_panic(
            "wait peek returned incorrect child status"
        );
    }

    if (
        wait_parent->first_child != wait_child ||
        !wait_parent->wait_active
    ) {
        kernel_panic(
            "wait peek consumed child lifecycle state"
        );
    }

    if (
        process_waitpid_try_reap(
            wait_parent,
            wait_child_pid,
            &wait_status
        ) != PROCESS_WAIT_RESULT_REAPED
    ) {
        kernel_panic(
            "waitpid failed to reap terminated child"
        );
    }

    if (
        wait_parent->wait_active ||
        wait_parent->wait_child_pid != 0
    ) {
        kernel_panic(
            "Reaped wait request remained active"
        );
    }

    if (
        wait_status.pid != wait_child_pid ||
        wait_status.termination_reason !=
            PROCESS_TERMINATION_EXITED ||
        wait_status.exit_status != 37
    ) {
        kernel_panic(
            "waitpid returned incorrect child status"
        );
    }

    if (wait_parent->first_child != NULL) {
        kernel_panic(
            "Reaped child remained linked to parent"
        );
    }

    if (
        process_waitpid_try_reap(
            wait_parent,
            wait_child_pid,
            &wait_status
        ) != PROCESS_WAIT_RESULT_NO_CHILD
    ) {
        kernel_panic(
            "waitpid reaped child more than once"
        );
    }

    wait_parent->process.state =
        PROCESS_STATE_TERMINATED;

    wait_parent->process.termination_reason =
        PROCESS_TERMINATION_EXITED;

    wait_parent->process.exit_status = 0;

    if (!scheduler_unregister_terminated(
        &wait_parent->process
    )) {
        kernel_panic(
            "Unable to unregister wait lifecycle parent"
        );
    }

    if (!process_release_terminated(
        wait_parent
    )) {
        kernel_panic(
            "Unable to release wait lifecycle parent"
        );
    }

    if (
        physical_free_frame_count() !=
        wait_free_before
    ) {
        kernel_panic(
            "Wait lifecycle test leaked physical frames"
        );
    }

    struct kernel_heap_stats wait_heap_after;

    if (!kernel_heap_stats_get(
        &wait_heap_after
    )) {
        kernel_panic(
            "Unable to read heap state after wait lifecycle test"
        );
    }

    if (
        wait_heap_after.allocated_block_count !=
            wait_heap_before.allocated_block_count ||
        wait_heap_after.allocated_bytes !=
            wait_heap_before.allocated_bytes
    ) {
        kernel_panic(
            "Wait lifecycle test leaked kernel heap allocations"
        );
    }

    struct process_instance instance;

    if (!process_instance_prepare_elf64(
        &instance,
        PROCESS_ELF_LIFECYCLE_TEST_PID,
        &image,
        2,
        argv,
        1,
        envp
    )) {
        kernel_panic(
            "Unable to prepare ELF-backed process instance"
        );
    }

    struct process_memory *memory = &instance.image.memory;
    struct process_layout *layout = &instance.image.layout;
    struct process *process = &instance.process;

    if (
        layout->kind !=
        PROCESS_LAYOUT_KIND_ELF64
    ) {
        kernel_panic(
            "ELF process layout has incorrect ownership kind"
        );
    }

    if (layout->code_base != 0) {
        kernel_panic(
            "ELF process layout retained legacy code base"
        );
    }

    if (
        layout->entry_point !=
        image.entry_point
    ) {
        kernel_panic(
            "ELF process layout entry point is incorrect"
        );
    }

    if (
        layout->initial_rsp <
        layout->stack.base_address ||
        layout->initial_rsp >=
        layout->stack.stack_top
    ) {
        kernel_panic(
            "ELF process initial RSP is outside user stack"
        );
    }

    if (
        (layout->initial_rsp & 0xFULL) != 0
    ) {
        kernel_panic(
            "ELF process initial RSP is not 16-byte aligned"
        );
    }

    if (
        layout->loaded_image.segments == NULL ||
        layout->loaded_image.segment_count != 1
    ) {
        kernel_panic(
            "ELF process layout ownership metadata is incorrect"
        );
    }

    uint64_t stack_cursor =
        layout->initial_rsp;

    uint64_t argc =
        process_elf_lifecycle_test_read_u64(
            memory,
            stack_cursor
        );

    if (argc != 2) {
        kernel_panic(
            "ELF process initial argc is incorrect"
        );
    }

    stack_cursor += sizeof(uint64_t);

    uint64_t argv0 =
        process_elf_lifecycle_test_read_u64(
            memory,
            stack_cursor
        );

    stack_cursor += sizeof(uint64_t);

    uint64_t argv1 =
        process_elf_lifecycle_test_read_u64(
            memory,
            stack_cursor
        );

    stack_cursor += sizeof(uint64_t);

    uint64_t argv_null =
        process_elf_lifecycle_test_read_u64(
            memory,
            stack_cursor
        );

    stack_cursor += sizeof(uint64_t);

    uint64_t envp0 =
        process_elf_lifecycle_test_read_u64(
            memory,
            stack_cursor
        );

    stack_cursor += sizeof(uint64_t);

    uint64_t envp_null =
        process_elf_lifecycle_test_read_u64(
            memory,
            stack_cursor
        );

    if (
        argv_null != 0 ||
        envp_null != 0
    ) {
        kernel_panic(
            "ELF process initial stack terminators are incorrect"
        );
    }

    process_elf_lifecycle_test_expect_string(
        memory,
        argv0,
        "hello"
    );

    process_elf_lifecycle_test_expect_string(
        memory,
        argv1,
        "world"
    );

    process_elf_lifecycle_test_expect_string(
        memory,
        envp0,
        "TERM=myos"
    );

    if (
        process->context.rip !=
        image.entry_point
    ) {
        kernel_panic(
            "ELF process context RIP is incorrect"
        );
    }

    if (
        process->context.rsp !=
        layout->initial_rsp
    ) {
        kernel_panic(
            "ELF process context RSP is incorrect"
        );
    }

    process->state =
        PROCESS_STATE_TERMINATED;

    process->termination_reason =
        PROCESS_TERMINATION_EXITED;

    process->exit_status = 0;

    if (!process_reclaim_resources(
        process
    )) {
        kernel_panic(
            "Unable to reclaim ELF lifecycle process resources"
        );
    }

    if (
        process->memory != NULL ||
        process->layout != NULL
    ) {
        kernel_panic(
            "ELF lifecycle process retained reclaimed references"
        );
    }

    if (
        layout->kind != PROCESS_LAYOUT_KIND_NONE ||
        layout->code_base != 0 ||
        layout->entry_point != 0 ||
        layout->initial_rsp != 0 ||
        layout->stack.guard_address != 0 ||
        layout->stack.base_address != 0 ||
        layout->stack.stack_top != 0 ||
        layout->stack.page_count != 0 ||
        layout->loaded_image.segments != NULL ||
        layout->loaded_image.segment_count != 0
    ) {
        kernel_panic(
            "ELF lifecycle layout retained reclaimed resources"
        );
    }

    if (
        memory->address_space.pml4_physical != 0 ||
        memory->address_space.pml4_virtual != NULL ||
        memory->address_space.kernel_half_shared
    ) {
        kernel_panic(
            "ELF lifecycle address space remained alive"
        );
    }

    if (
        physical_free_frame_count() !=
        free_before
    ) {
        kernel_panic(
            "ELF lifecycle test leaked physical frames"
        );
    }

    diagnostics_write(
        "[process] ELF-backed lifecycle test passed\n"
    );
}
