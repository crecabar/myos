// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file process_elf_lifecycle_test.c
 * @brief ELF-backed process lifecycle regression tests.
 */

#include "process_elf_lifecycle_test.h"

#include "../core/panic.h"
#include "../diagnostics/diagnostics.h"
#include "../elf/elf64.h"
#include "../memory/memory.h"
#include "../memory/heap.h"
#include "../process/create.h"
#include "../process/image.h"
#include "../process/instance.h"
#include "../process/layout.h"
#include "../process/memory.h"
#include "../process/process.h"
#include "../scheduler/scheduler.h"
#include <stddef.h>
#include <stdint.h>

#define PROCESS_ELF_LIFECYCLE_TEST_IMAGE_SIZE 0x2000U
#define PROCESS_ELF_LIFECYCLE_TEST_VADDR      0x0000000000400000ULL

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

    struct process_instance *dynamic_instance = process_create_elf64(
        PROCESS_ELF_LIFECYCLE_TEST_PID + 2,
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

    struct process scheduler_fillers[
        SCHEDULER_MAX_PROCESSES
    ];

    uint64_t scheduler_failure_free_before =
    physical_free_frame_count();

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
            PROCESS_ELF_LIFECYCLE_TEST_PID + 3,
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
