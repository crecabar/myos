// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/arch.h"
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/paging.h"
#include "arch/x86_64/rtc.h"
#include "arch/x86_64/serial.h"
#include "arch/x86_64/stack.h"
#include "boot/active_paging_audit.h"
#include "boot/boot.h"
#include "boot/initramfs.h"
#include "boot/memory_reclaim.h"
#include "boot/physical_range_audit.h"
#include "boot/reclaim_preflight.h"
#include "config/boot_config.h"
#include "console/console_device.h"
#include "core/panic.h"
#include "core/device/registry.h"
#include "diagnostics/diagnostics.h"
#include "drivers/pseudo.h"
#include "fs/devfs.h"
#include "fs/initramfs.h"
#include "init/boot_banner.h"
#include "init/display.h"
#include "init/userspace.h"
#include "input/input.h"
#include "memory/boot_paging.h"
#include "memory/device_mapping.h"
#include "memory/direct_mapping.h"
#include "memory/heap.h"
#include "memory/kernel_mapping.h"
#include "memory/memory.h"
#include "process/pid.h"
#include "scheduler/scheduler.h"
#include "vfs/mount.h"
#include "vfs/root.h"

#if MYOS_RUNTIME_DIAGNOSTICS
#include "tests/runtime_diagnostics.h"
#endif

#if MYOS_KERNEL_TESTS
#include "arch/x86_64/tests/interrupt_wait_test.h"
#include "tests/acpi_test.h"
#include "tests/block_device_test.h"
#include "tests/boot_config_test.h"
#include "tests/boot_info_test.h"
#include "tests/boot_module_test.h"
#include "tests/bus_test.h"
#include "tests/character_device_test.h"
#include "tests/console_device_test.h"
#include "tests/console_line_buffer_test.h"
#include "tests/core_device_fd_test.h"
#include "tests/devfs_test.h"
#include "tests/device_registry_test.h"
#include "tests/device_test.h"
#include "tests/elf64_test.h"
#include "tests/elf64_loader_test.h"
#include "tests/framebuffer_test.h"
#include "tests/input_test.h"
#include "tests/initramfs_boot_test.h"
#include "tests/initramfs_format_test.h"
#include "tests/initramfs_test.h"
#include "tests/kernel_heap_test.h"
#include "tests/process_cwd_test.h"
#include "tests/process_elf_lifecycle_test.h"
#include "tests/process_executable_test.h"
#include "tests/process_fd_table_test.h"
#include "tests/process_file_test.h"
#include "tests/process_lifecycle_test.h"
#include "tests/process_memory_test.h"
#include "tests/process_namespace_test.h"
#include "tests/process_path_test.h"
#include "tests/pseudo_device_test.h"
#include "tests/ps2_scancode_set1_test.h"
#include "tests/ps2_mouse_packet_test.h"
#include "tests/runtime_memory_test.h"
#include "tests/scheduler_slot_test.h"
#include "tests/syscall_test.h"
#include "tests/user_copy_test.h"
#include "tests/user_processes.h"
#include "tests/vfs_character_device_test.h"
#include "tests/vfs_directory_test.h"
#include "tests/vfs_file_io_test.h"
#include "tests/vfs_file_test.h"
#include "tests/vfs_lookup_test.h"
#include "tests/vfs_mount_test.h"
#include "tests/vfs_node_test.h"
#include "tests/vfs_node_operations_test.h"
#include "tests/vfs_parent_test.h"
#include "tests/vfs_path_test.h"
#endif

#define KERNEL_RUNTIME_STACK_SIZE 32768

static struct boot_info kernel_boot_info;
static struct kernel_boot_config kernel_boot_config;
static struct kernel_display kernel_display;
static struct initramfs *kernel_root_filesystem;
static struct devfs *kernel_device_filesystem;
static struct vfs_mount_table kernel_mount_table;
static struct device_registry kernel_device_registry;
static struct pseudo_devices kernel_pseudo_devices;
static struct console_device kernel_console_device;

static uint8_t kernel_runtime_stack[
    KERNEL_RUNTIME_STACK_SIZE
] __attribute__((aligned(16)));

static void kernel_input_system_action(
    enum input_system_action action
);

static void kernel_mount_root_filesystem(void);

static void kernel_initialize_core_devices(void);

static bool kernel_mount_bootstrap_device_namespace(
    struct initramfs *filesystem,
    struct vfs_mount_table *mounts,
    struct devfs **device_filesystem,
    struct vfs_node **device_mountpoint
);

static void kernel_rollback_bootstrap_device_namespace(
    struct vfs_mount_table *mounts,
    struct devfs *device_filesystem,
    struct vfs_node *device_mountpoint
);

static _Noreturn void kernel_main_continue(void);

_Noreturn void kernel_main(void)
{
    serial_init();
    diagnostics_init();

    diagnostics_write("\x1b[2J\x1b[H");

    boot_init(
        &kernel_boot_info
    );

    if (!boot_info_validate(
        &kernel_boot_info
    )) {
        kernel_panic(
            "Invalid normalized boot information"
        );
    }

    boot_config_parse(
        kernel_boot_info.command_line,
        &kernel_boot_config
    );

    memory_init(
        kernel_boot_info.direct_map_offset,
        kernel_boot_info.memory_regions,
        kernel_boot_info.memory_region_count
    );

    if (!paging_init()) {
        kernel_panic("Unable to initialize paging");
    }

    if (!kernel_mapping_install()) {
        kernel_panic(
            "Unable to install kernel-owned mappings"
        );
    }

    if (!device_mapping_map_framebuffer(
        &kernel_boot_info.framebuffer
    )) {
        kernel_panic(
            "Unable to install kernel framebuffer mapping"
        );
    }

    arch_stack_enter(
        (uint64_t) &kernel_runtime_stack[
            KERNEL_RUNTIME_STACK_SIZE
        ],
        kernel_main_continue
    );
}

static void kernel_initialize_core_devices(void)
{
    if (!device_registry_initialize(
        &kernel_device_registry
    )) {
        kernel_panic(
            "Unable to initialize kernel device registry"
        );
    }

    enum pseudo_devices_initialize_result result =
        pseudo_devices_initialize(
            &kernel_pseudo_devices,
            &kernel_device_registry
        );

    if (
        result !=
            PSEUDO_DEVICES_INITIALIZE_RESULT_INITIALIZED
    ) {
        kernel_panic(
            "Unable to initialize core pseudo devices"
        );
    }

    enum console_device_initialize_result console_result =
        console_device_initialize(
            &kernel_console_device,
            &kernel_display.console,
            &kernel_device_registry
        );

    if (
        console_result !=
            CONSOLE_DEVICE_INITIALIZE_RESULT_INITIALIZED
    ) {
        kernel_panic(
            "Unable to initialize system console device"
        );
    }

    diagnostics_write(
        "[device] Core null/zero/console devices registered\n"
    );
}

static void kernel_input_system_action(
    enum input_system_action action)
{
    switch (action) {
        case INPUT_SYSTEM_ACTION_RESET:
            diagnostics_write(
                "[input] Ctrl+Alt+Delete: resetting system\n"
            );

            arch_reset();

        default:
            kernel_panic(
                "Unknown kernel input system action"
            );
    }
}

static bool kernel_mount_bootstrap_device_namespace(
    struct initramfs *filesystem,
    struct vfs_mount_table *mounts,
    struct devfs **device_filesystem,
    struct vfs_node **device_mountpoint)
{
    if (
        filesystem == NULL ||
        mounts == NULL ||
        device_filesystem == NULL ||
        device_mountpoint == NULL
    ) {
        return false;
    }

    struct vfs_node *root =
        initramfs_root(
            filesystem
        );

    if (
        root == NULL ||
        root->type !=
            VFS_NODE_TYPE_DIRECTORY
    ) {
        return false;
    }

    /*
     * /dev remains an ordinary directory owned by the root filesystem. The
     * VFS mount topology covers it with the devfs root.
     */
    struct vfs_node *dev =
        NULL;

    if (
        initramfs_create_directory(
            filesystem,
            root,
            "dev",
            sizeof("dev") - 1U,
            &dev
        ) !=
            INITRAMFS_NAMESPACE_RESULT_SUCCESS ||
        dev == NULL ||
        dev->type !=
            VFS_NODE_TYPE_DIRECTORY
    ) {
        return false;
    }

    struct devfs *mounted_devfs =
        NULL;

    if (
        devfs_mount(
            &kernel_device_registry,
            &mounted_devfs
        ) !=
            DEVFS_MOUNT_RESULT_MOUNTED ||
        mounted_devfs == NULL
    ) {
        return false;
    }

    struct vfs_node *mounted_root =
        devfs_root(
            mounted_devfs
        );

    if (mounted_root == NULL) {
        if (!devfs_unmount(
            mounted_devfs
        )) {
            kernel_panic(
                "Unable to roll back invalid devfs root"
            );
        }

        return false;
    }

    if (
        vfs_mount_table_attach(
            mounts,
            dev,
            mounted_root
        ) !=
            VFS_MOUNT_RESULT_SUCCESS
    ) {
        if (!devfs_unmount(
            mounted_devfs
        )) {
            kernel_panic(
                "Unable to roll back failed devfs mount"
            );
        }

        return false;
    }

    *device_filesystem =
        mounted_devfs;

    *device_mountpoint =
        dev;

    return true;
}

static void kernel_rollback_bootstrap_device_namespace(
    struct vfs_mount_table *mounts,
    struct devfs *device_filesystem,
    struct vfs_node *device_mountpoint)
{
    if (
        mounts == NULL ||
        device_filesystem == NULL ||
        device_mountpoint == NULL
    ) {
        kernel_panic(
            "Invalid bootstrap device namespace rollback"
        );
    }

    if (
        vfs_mount_table_detach(
            mounts,
            device_mountpoint
        ) !=
            VFS_MOUNT_RESULT_SUCCESS
    ) {
        kernel_panic(
            "Unable to detach bootstrap devfs mount"
        );
    }

    if (!devfs_unmount(
        device_filesystem
    )) {
        kernel_panic(
            "Unable to unmount bootstrap devfs"
        );
    }
}

static void kernel_mount_root_filesystem(void)
{
    if (
        kernel_root_filesystem != NULL ||
        kernel_device_filesystem != NULL ||
        vfs_root_get() != NULL
    ) {
        kernel_panic(
            "System root filesystem already installed"
        );
    }

    const struct boot_module *module =
        NULL;

    if (!boot_initramfs_find(
        &kernel_boot_info,
        &module
    )) {
        kernel_panic(
            "Unable to locate boot initramfs"
        );
    }

    if (
        module == NULL ||
        module->size == 0 ||
        module->size > SIZE_MAX
    ) {
        kernel_panic(
            "Boot initramfs has invalid range"
        );
    }

    struct initramfs *filesystem =
        NULL;

    enum initramfs_mount_result mount_result =
        initramfs_mount(
            (const void *)
                (uintptr_t)
                module->virtual_base,
            (size_t) module->size,
            &filesystem
        );

    if (
        mount_result !=
            INITRAMFS_MOUNT_RESULT_MOUNTED ||
        filesystem == NULL
    ) {
        kernel_panic(
            "Unable to mount boot initramfs"
        );
    }

    struct vfs_node *root =
        initramfs_root(
            filesystem
        );

    if (root == NULL) {
        if (!initramfs_unmount(
            filesystem
        )) {
            kernel_panic(
                "Unable to roll back invalid root filesystem"
            );
        }

        kernel_panic(
            "Mounted initramfs has no root"
        );
    }

    if (!vfs_mount_table_initialize(
        &kernel_mount_table
    )) {
        if (!initramfs_unmount(
            filesystem
        )) {
            kernel_panic(
                "Unable to roll back mount topology initialization"
            );
        }

        kernel_panic(
            "Unable to initialize system mount topology"
        );
    }

    struct devfs *device_filesystem =
        NULL;

    struct vfs_node *device_mountpoint =
        NULL;

    /*
     * Build the complete bootstrap namespace before publishing the system root.
     * /dev is an initramfs-owned mountpoint covered by the live devfs root.
     */
    if (!kernel_mount_bootstrap_device_namespace(
        filesystem,
        &kernel_mount_table,
        &device_filesystem,
        &device_mountpoint
    )) {
        if (!initramfs_unmount(
            filesystem
        )) {
            kernel_panic(
                "Unable to roll back bootstrap device namespace"
            );
        }

        kernel_panic(
            "Unable to mount bootstrap device namespace"
        );
    }

    /*
     * Root publication is the commit point. If publication fails, tear down
     * the VFS mount relation before releasing either filesystem.
     */
    if (!vfs_root_install(
        root
    )) {
        kernel_rollback_bootstrap_device_namespace(
            &kernel_mount_table,
            device_filesystem,
            device_mountpoint
        );

        if (!initramfs_unmount(
            filesystem
        )) {
            kernel_panic(
                "Unable to roll back failed root installation"
            );
        }

        kernel_panic(
            "Unable to install system VFS root"
        );
    }

    /*
     * From this point onward the namespace is globally visible. The kernel
     * retains the filesystem objects and mount-table storage for the lifetime
     * of the system namespace.
     */
    kernel_root_filesystem =
        filesystem;

    kernel_device_filesystem =
        device_filesystem;

    diagnostics_write(
        "[fs] devfs mounted at /dev\n"
    );

    diagnostics_write(
        "[fs] Initramfs mounted as system root\n"
    );
}

static _Noreturn void kernel_main_continue(void)
{
    if (!direct_mapping_install()) {
        kernel_panic(
            "Unable to install MyOS-owned direct map"
        );
    }

    if (!kernel_heap_init()) {
        kernel_panic("Unable to initialize kernel heap");
    }

    display_init(
        &kernel_display,
        &kernel_boot_info.framebuffer
    );

    struct cpu_info cpu;
    cpu_info_read(&cpu);

    struct smbios_system_info system;

    smbios_system_info_read(
        kernel_boot_info.smbios_entry_32,
        kernel_boot_info.smbios_entry_64,
        &system
    );

    /*
     * System identification has been copied into kernel-owned storage.
     * The original SMBIOS entry-point pointers are no longer needed.
     */
    kernel_boot_info.smbios_entry_32 = NULL;
    kernel_boot_info.smbios_entry_64 = NULL;

    enum boot_mode boot_mode =
        kernel_boot_config.mode == KERNEL_BOOT_MODE_TEST
            ? BOOT_MODE_TEST
            : BOOT_MODE_NORMAL;

    boot_banner_print(
        &cpu,
        &system,
        boot_mode
    );

    /*
     * Borrowed firmware entry-point references must not remain reachable through
     * the persistent boot_info after their consumers have finished.
     *
     * The command line is intentionally retained because it is now a kernel-owned
     * snapshot rather than a bootloader reference.
     */
    if (
        kernel_boot_info.smbios_entry_32 != NULL ||
        kernel_boot_info.smbios_entry_64 != NULL
    ) {
        kernel_panic(
            "Transient boot information remains referenced"
        );
    }

#if MYOS_RUNTIME_DIAGNOSTICS
    diagnostics_write(
        "[boot] Transient boot-info references released\n"
    );
#endif

#if MYOS_KERNEL_TESTS
    if (
        kernel_boot_config.mode ==
        KERNEL_BOOT_MODE_TEST
    ) {
        boot_module_test_capture_before_reclaim(
            &kernel_boot_info
        );
    }
#endif

    /*
     * Establish the production device registry and bootstrap pseudo devices
     * after the boot banner is visible. Device publication is independent of
     * the bootloader-backed initramfs namespace and precedes later userspace
     * exposure through /dev.
     */
    kernel_initialize_core_devices();

    /*
     * Consume the preserved boot initramfs while its normalized descriptor and
     * module mapping are known-good. The mounted filesystem borrows immutable
     * archive bytes that remain reserved for the lifetime of the system.
     */
    kernel_mount_root_filesystem();

    /*
     * Install kernel-owned GDT, TSS and IDT before returning any
     * bootloader-reclaimable frame to the physical allocator.
     *
     * Interrupt controllers and maskable interrupts remain disabled
     * until arch_init(), after general memory reclamation.
     */
    arch_early_init();

    struct boot_paging_inventory boot_inventory;

    if (!boot_paging_reclaim_preflight(
        &boot_inventory
    )) {
        kernel_panic(
            "Inherited page-table reclaim preflight failed"
        );
    }

    uint64_t inherited_table_frames =
        boot_inventory.pml4_frames +
        boot_inventory.pdpt_frames +
        boot_inventory.pd_frames +
        boot_inventory.pt_frames;

    diagnostics_printf(
        "[boot-paging] Reclaim preflight passed: "
        "%u inherited table frames remain reserved\n",
        inherited_table_frames
    );

#if MYOS_RUNTIME_DIAGNOSTICS
    runtime_diagnostics_pre_reclaim();

    struct boot_reclaim_preflight_report premature_report;

    if (
        boot_reclaim_preflight(
            &kernel_boot_info,
            &premature_report
        )
    ) {
        kernel_panic(
            "General boot-memory preflight accepted inherited page tables"
        );
    }

    diagnostics_write(
        "[boot-memory] Premature preflight rejected\n"
    );
#endif

    uint64_t reclaimed_table_frames = 0;

    if (!boot_paging_reclaim(
        &reclaimed_table_frames
    )) {
        kernel_panic(
            "Unable to reclaim inherited page tables"
        );
    }

    if (
        reclaimed_table_frames !=
        inherited_table_frames
    ) {
        kernel_panic(
            "Boot page-table reclaim result differs from preflight"
        );
    }

#if MYOS_RUNTIME_DIAGNOSTICS
    runtime_diagnostics_reclaimed_frame_reuse();
#endif

    diagnostics_printf(
        "[boot-paging] Reclaimed %u inherited page-table frames\n",
        reclaimed_table_frames
    );

    struct boot_reclaim_preflight_report reclaim_report;

    if (
        !boot_reclaim_preflight(
            &kernel_boot_info,
            &reclaim_report
        )
    ) {
        kernel_panic(
            "General boot-memory preflight failed"
        );
    }

    diagnostics_printf(
        "[boot-memory] Read-only preflight passed: "
        "pending=%u free=%u\n",
        reclaim_report.inventory.pending_frames,
        reclaim_report.free_frames
    );

    struct boot_physical_range_audit_report range_audit;

    if (
        !boot_physical_range_audit(
            &kernel_boot_info,
            &reclaim_report,
            &range_audit
        )
    ) {
        kernel_panic(
            "Bootloader physical-range audit failed"
        );
    }

    diagnostics_printf(
        "[boot-memory] Physical-range audit passed: "
        "regions=%u pending=%u "
        "kernel-pages=%u framebuffer-pages=%u\n",
        range_audit.reclaimable_regions,
        range_audit.pending_frames,
        range_audit.kernel_image_pages_checked,
        range_audit.framebuffer_pages_checked
    );

    struct boot_active_paging_audit_report paging_audit;

    if (
        !boot_active_paging_audit(
            &kernel_boot_info,
            &paging_audit
        )
    ) {
        kernel_panic(
            "Active paging-structure audit failed"
        );
    }

    if (paging_audit.pml4_tables != 1) {
        kernel_panic(
            "Active paging audit found an unexpected PML4 count"
        );
    }

    diagnostics_printf(
        "[boot-memory] Active paging audit passed: "
        "PML4=%u PDPT=%u PD=%u PT=%u "
        "1G-leaves=%u 2M-leaves=%u free=%u\n",
        paging_audit.pml4_tables,
        paging_audit.pdpt_tables,
        paging_audit.pd_tables,
        paging_audit.pt_tables,
        paging_audit.large_1g_mappings,
        paging_audit.large_2m_mappings,
        paging_audit.free_frames
    );

#if MYOS_RUNTIME_DIAGNOSTICS
    diagnostics_write(
        "\n--- Bootloader-reclaimable physical ranges ---\n"
    );

    for (
        size_t index = 0;
        index < kernel_boot_info.memory_region_count;
        ++index
    ) {
        const struct memory_region *region =
            &kernel_boot_info.memory_regions[index];

        if (
            region->type !=
            MEMORY_REGION_BOOTLOADER_RECLAIMABLE
        ) {
            continue;
        }

        diagnostics_printf(
            "  [%x, %x)\n",
            region->base,
            region->base + region->length
        );
    }

    runtime_diagnostics_bootloader_frame_inventory(
        &kernel_boot_info
    );

    runtime_diagnostics_general_reclaim_rejections(
        &kernel_boot_info
    );
#endif

    /*
     * All bootloader-data consumers have finished, the kernel owns
     * its runtime mappings and stack, and the read-only audits have
     * completed. Transfer the remaining eligible frames before arch_init().
     */
    struct boot_memory_reclaim_result general_reclaim;

    if (
        !boot_memory_reclaim_remaining(
            &kernel_boot_info,
            &general_reclaim
        )
    ) {
        kernel_panic(
            "General bootloader memory reclamation rejected"
        );
    }

    diagnostics_printf(
        "[boot-memory] Reclaimed %u remaining frames: "
        "free before=%u free after=%u\n",
        general_reclaim.reclaimed_frames,
        general_reclaim.free_before,
        general_reclaim.free_after
    );

#if MYOS_RUNTIME_DIAGNOSTICS
    /*
     * Reclamation must be one-shot. A rejected second call must not
     * change the physical allocator's accounting.
     */
    struct boot_memory_reclaim_result repeated_reclaim;

    if (
        boot_memory_reclaim_remaining(
            &kernel_boot_info,
            &repeated_reclaim
        ) ||
        physical_free_frame_count() !=
            general_reclaim.free_after
    ) {
        kernel_panic(
            "General bootloader reclaim accepted a repeated transfer"
        );
    }

    /*
     * Exercise a frame that was actually recovered by this operation,
     * rather than relying on the allocator's normal allocation order.
     */
    if (general_reclaim.reclaimed_frames != 0) {
        uint64_t probe_frame =
            general_reclaim.first_reclaimed_frame;

        if (
            memory_bootloader_frame_reclaim_pending(
                probe_frame
            ) ||
            !physical_alloc_frame_at(
                probe_frame
            )
        ) {
            kernel_panic(
                "Unable to allocate a generally reclaimed frame"
            );
        }

        volatile uint64_t *probe =
            memory_physical_to_virtual(
                probe_frame
            );

        probe[0] =
            0x1122334455667788ULL;

        if (
            probe[0] !=
            0x1122334455667788ULL
        ) {
            kernel_panic(
                "Generally reclaimed frame readback failed"
            );
        }

        if (
            !physical_free_frame(
                probe_frame
            ) ||
            physical_free_frame_count() !=
                general_reclaim.free_after
        ) {
            kernel_panic(
                "Generally reclaimed frame reuse leaked ownership"
            );
        }
    }

    diagnostics_write(
        "[tests] General bootloader reclaim and frame reuse passed\n"
    );
#endif

    input_init();

    arch_init(
        kernel_boot_info.rsdp_snapshot,
        kernel_boot_info.rsdp_snapshot_size
    );

    diagnostics_write("[arch] x86-64 initialized\n");

    uint64_t first_process_pid = 1;

#if MYOS_KERNEL_TESTS
    if (
        kernel_boot_config.mode ==
        KERNEL_BOOT_MODE_TEST
    ) {
        /*
         * Temporary compatibility range for legacy test fixtures that still
         * assign synthetic process identifiers directly.
         */
        first_process_pid =
            1000;
    }
#endif

    if (!process_pid_initialize(
        first_process_pid
    )) {
        kernel_panic(
            "Unable to initialize process identifier allocator"
        );
    }

    scheduler_init();
    diagnostics_write("[scheduler] Initialized\n");

    diagnostics_write("[kernel] Initialization complete\n");

    /* RTC */
    struct rtc_time time;
    if (rtc_read_time(&time)) {
        diagnostics_printf(
            "[clock] %u:%u:%u UTC\n",
            (uint64_t) time.hours,
            (uint64_t) time.minutes,
            (uint64_t) time.seconds
        );
    } else {
        diagnostics_write("[clock] RTC unavailable\n");
    }

#if MYOS_RUNTIME_DIAGNOSTICS
    diagnostics_write(
        "\n--- kernel runtime test suite ---\n"
    );
    runtime_diagnostics_run(
        &kernel_boot_info.framebuffer
    );
#endif

#if MYOS_KERNEL_TESTS
    if (
        kernel_boot_config.mode ==
        KERNEL_BOOT_MODE_TEST
    ) {
        diagnostics_write(
            "\n--- kernel test suite ---\n"
        );

        boot_module_test_run(
            &kernel_boot_info
        );

        initramfs_boot_test_run(
            &kernel_mount_table
        );

        boot_info_test_run();
        boot_config_test_run();

        device_test_run();
        character_device_test_run();
        block_device_test_run();
        device_registry_test_run();
        pseudo_device_test_run();
        console_device_test_run();
        console_line_buffer_test_run();
        bus_test_run();

        acpi_test_run(
            kernel_boot_info.rsdp_snapshot,
            kernel_boot_info.rsdp_snapshot_size
        );

        elf64_test_run();
        elf64_loader_test_run();
        interrupt_wait_test_run();
        framebuffer_test_run();
        ps2_scancode_set1_test_run();
        ps2_mouse_packet_test_run();
        input_test_run();
        initramfs_format_test_run();
        initramfs_test_run();
        kernel_heap_test_run();
        vfs_node_test_run();
        vfs_node_operations_test_run();
        vfs_file_test_run();
        vfs_file_io_test_run();
        vfs_directory_test_run();
        vfs_character_device_test_run();
        devfs_test_run();
        vfs_lookup_test_run();
        vfs_mount_test_run();
        vfs_parent_test_run();
        vfs_path_test_run();
        process_fd_table_test_run();
        process_namespace_test_run();
        process_cwd_test_run();
        process_path_test_run();
        process_executable_test_run(
            &kernel_mount_table
        );
        process_file_test_run();
        core_device_fd_test_run(
            &kernel_mount_table
        );
        runtime_memory_test_run();
        syscall_test_run();
        process_memory_test_run();
        user_copy_test_run();
        process_lifecycle_test_run();
        process_elf_lifecycle_test_run();
        scheduler_slot_test_run();
        user_process_tests_prepare(
            &kernel_mount_table
        );
    }
#endif

    if (
        kernel_boot_config.mode ==
        KERNEL_BOOT_MODE_NORMAL
    ) {
        struct vfs_node *root =
            vfs_root_get();

        if (root == NULL) {
            kernel_panic(
                "Normal boot has no system VFS root"
            );
        }

        if (!userspace_init_start(
            root,
            &kernel_mount_table
        )) {
            kernel_panic(
                "Unable to launch userspace /init"
            );
        }
    }

    input_system_action_handler_set(
        kernel_input_system_action
    );

    /*
     * A chord may have been recognized before the immediate handler
     * became active. Honor one pending action before entering the
     * scheduler.
     */
    enum input_system_action pending_action;

    if (
        input_system_action_take(
            &pending_action
        )
    ) {
        kernel_input_system_action(
            pending_action
        );
    }

    diagnostics_write("[kernel] Starting scheduler\n");
    scheduler_run();
}
