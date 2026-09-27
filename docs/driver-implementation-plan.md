# MyOS Driver and Module Implementation Plan

This document translates the device-model design and the Chungungo MyOS 1.0 hardware contract into an implementation sequence.

It is intentionally incremental. Each stage should leave the kernel bootable and testable under QEMU before the next hardware-specific layer is added.

## Principle

Do not start by writing a collection of unrelated hardware drivers.

First establish the common ownership and binding rules that those drivers will use.

The preferred dependency direction is:

```text
hardware-specific driver
        |
        v
generic subsystem interface
        |
        v
kernel service

bus-specific driver
        |
        v
bus core
        |
        v
device core
```

Generic subsystems must not depend on concrete hardware drivers.

## Phase 1: device core

Add a small device core with:

- `struct device`;
- `struct driver`;
- `struct bus`;
- device registration;
- driver registration;
- matching;
- probe/bind;
- remove/unbind;
- diagnostic enumeration.

Suggested area:

```text
kernel/core/device/
include/kernel/device/
```

Tests should cover:

- device registered before driver;
- driver registered before device;
- successful match/probe;
- rejected match;
- failed probe;
- removal;
- no double binding;
- cleanup after probe failure.

No PCI-specific fields should appear in the generic objects.

## Phase 2: ACPI platform discovery

Move required platform discovery behind ACPI-owned interfaces.

Initial tables:

```text
RSDP
XSDT
MADT
MCFG
HPET
FADT as needed
```

Suggested area:

```text
kernel/firmware/acpi/
include/kernel/acpi/
```

ACPI should expose parsed information. Consumers should not repeatedly walk raw firmware tables.

Existing APIC/IOAPIC work should progressively consume MADT-derived topology instead of machine-specific assumptions.

## Phase 3: PCI core

Implement PCI enumeration before AHCI, Ethernet, HDA, xHCI, or native Intel graphics.

Suggested area:

```text
kernel/bus/pci/
include/kernel/pci/
```

Responsibilities:

- enumerate buses/devices/functions;
- read class and identity fields;
- discover BARs;
- expose safe configuration-space reads/writes;
- register PCI devices with the device core;
- match PCI ID tables;
- expose interrupt information required by drivers;
- provide BAR mapping helpers;
- later expose MSI/MSI-X as needed.

Configuration-space access should use an operations interface so both legacy CF8/CFC and ACPI MCFG/ECAM can exist without changing drivers.

First physical acceptance target:

```text
[pci] 00:02.0 8086:016a ...
[pci] 00:14.0 8086:1e31 ...
[pci] 00:19.0 8086:1502 ...
[pci] 00:1f.2 8086:1e02 ...
```

The exact diagnostic format is not normative.

## Phase 4: block subsystem and AHCI

Implement a generic block-device API before exposing the SSD directly to the filesystem.

Suggested areas:

```text
kernel/subsys/block/
kernel/drivers/storage/ahci/
```

Dependency:

```text
PCI
 -> AHCI
 -> block device
 -> partition layer
 -> MFS
```

The first AHCI target is Intel `8086:1e02`.

Start with the smallest reliable command path:

- controller reset/enable as required;
- implemented-port detection;
- command-list/FIS memory setup;
- identify device;
- read sectors;
- write sectors;
- error/timeout handling.

NCQ and power-management features can follow after correctness.

The driver must use the DMA/memory interfaces owned by the kernel rather than assuming physical equality of arbitrary virtual addresses.

## Phase 5: root filesystem and module store

Once disk-backed MFS exists, define the module location:

```text
/lib/modules/<myos-version>/
```

The exact hierarchy may evolve, but module lookup should not be hard-coded throughout drivers or subsystems.

At this point the boot sequence gains a boundary:

```text
pre-root:
    only built-ins are available

post-root:
    loadable modules become available
```

This boundary determines `y` versus `m` for the 1.0 reference configuration.

## Phase 6: module loader

MyOS modules should be native ELF modules, not Linux kernel modules.

Suggested area:

```text
kernel/core/module/
include/kernel/module/
```

Initial loader responsibilities:

- validate ELF class, machine, and module format;
- allocate memory for module sections;
- apply the relocation types MyOS actually emits;
- resolve approved kernel symbols;
- reject unresolved or forbidden symbols;
- run module initialization;
- track loaded modules;
- refuse duplicate module names;
- report dependency or ABI mismatch.

Do not promise stable module ABI before the interfaces are mature.

A simple module may contain one or more driver registrations.

The build system should compile the same driver source as:

```text
CONFIG_FOO=y -> linked into kernel.elf
CONFIG_FOO=m -> linked as MyOS module
CONFIG_FOO=n -> not compiled
```

## Phase 7: USB core and host controllers

For Chungungo 1.0, USB is part of the built-in bootstrap set because the reference keyboard/mouse uses USB HID.

Implement layers independently:

```text
PCI
 -> EHCI/xHCI
 -> USB core
 -> hub
 -> interface enumeration
 -> HID
 -> input subsystem
```

Suggested areas:

```text
kernel/bus/usb/
kernel/drivers/usb/ehci/
kernel/drivers/usb/xhci/
kernel/drivers/usb/hid/
kernel/subsys/input/
```

The first required physical device is USB `03f0:5341`, which exposes keyboard and mouse HID interfaces.

A host-controller driver must not contain keyboard logic.

## Phase 8: network subsystem and 82579LM module

Build a small generic network-device interface and Ethernet layer.

Then implement the Intel 82579LM PCI `8086:1502` as the first network module.

Suggested areas:

```text
kernel/subsys/net/
kernel/drivers/net/intel/e1000e/
```

Initial milestones:

- probe/bind;
- map MMIO BARs;
- reset/init device;
- read hardware MAC address;
- descriptor rings;
- transmit Ethernet frame;
- receive Ethernet frame;
- interrupt handling;
- register generic network device.

IP, ARP, ICMP, UDP, TCP, sockets, and POSIX-facing networking are separate network-stack milestones.

## Phase 9: graphics core and Ivy Bridge module

Keep the boot framebuffer as a built-in fallback.

Introduce a graphics/display abstraction before native Intel support.

Suggested areas:

```text
kernel/subsys/graphics/
kernel/drivers/graphics/intel/
```

The Intel target is Ivy Bridge Gen7, PCI `8086:016a`.

Do not begin by attempting full Linux i915 parity.

An incremental native path can target:

1. device probe;
2. MMIO mapping;
3. display-engine discovery;
4. connector/output discovery;
5. modesetting;
6. scanout buffer;
7. framebuffer handoff;
8. userspace buffer/mapping interface;
9. functionality required by the future X server.

The longer-term acceptance target is the project's `startx` / `xclock` goal.

## Phase 10: HDA audio module

Introduce a generic audio interface, then an Intel HDA controller driver, then codec-specific support.

Suggested areas:

```text
kernel/subsys/audio/
kernel/drivers/audio/hda/
kernel/drivers/audio/codecs/
```

Chungungo targets:

- Intel HDA controller `8086:1e20`;
- Realtek ALC269VB analog codec path;
- Intel HDMI/DisplayPort audio path.

Audio is post-root and non-fatal.

## Phase 11: optional storage classes

After the normal AHCI/MFS root path is reliable:

- add ATAPI as a module for the MATSHITA optical drive;
- add USB mass storage as a module;
- connect both through the generic block/storage interfaces.

Neither belongs to the boot-critical closure for the installed Chungungo system.

## Configuration dependency rules

The configuration system should reject impossible combinations.

Examples:

```text
E1000E=m requires:
    MODULES=y
    PCI=y
    NET!=n
    ETHERNET!=n

USB_HID=y requires:
    USB=y
    INPUT!=n

AHCI=y requires:
    PCI=y
    BLOCK=y

MFS=y as root requires:
    BLOCK=y
    required root-storage path=y
```

A module must never be the only provider of something required to load that same module.

This is the bootstrap rule:

```text
A -> loads B
therefore A cannot depend exclusively on B.
```

## Built-in registration

Built-in drivers should use the same registration path as modules.

Conceptually:

```c
static int ahci_init(void)
{
    return pci_register_driver(&ahci_driver);
}
```

For `CONFIG_AHCI=y`, kernel initialization invokes the registration.

For `CONFIG_AHCI=m`, module initialization invokes the same registration.

This keeps probing behavior identical.

## Driver cleanup contract

Every driver should be designed with cleanup ownership even if module unloading is initially disabled.

A probe sequence should have a reversible ownership chain:

```text
map BAR
 -> allocate state
 -> allocate DMA memory
 -> register IRQ
 -> enable device
 -> register subsystem object
```

Failure unwinds in reverse order.

This prevents later module support from requiring every driver to be rewritten.

## Interrupts and DMA

PCI drivers should obtain interrupt and DMA services through kernel APIs.

Drivers should not:

- program global IOAPIC state directly;
- assume a fixed IRQ from Chungungo;
- translate arbitrary kernel virtual pointers to DMA addresses by casting;
- own generic physical-memory allocator internals.

The kernel should progressively provide:

- IRQ registration/routing;
- MMIO mapping;
- DMA-capable allocation/mapping;
- memory barriers;
- PCI bus mastering control.

MSI/MSI-X can be added when a supported device or performance requirement justifies it.

## Diagnostics

Every bus and driver should expose useful boot diagnostics without requiring a debugger.

Useful examples include:

```text
[pci] 00:1f.2 8086:1e02 class=0106
[driver] ahci bound to 00:1f.2
[ahci] port 0: ATA device
[block] registered disk0
[module] loaded e1000e
[driver] e1000e bound to 00:19.0
```

Diagnostics should describe discovered state, not become part of the ABI.

## QEMU strategy

Physical Chungungo support should not eliminate emulator-driven development.

Where possible, each generic subsystem should be testable using QEMU hardware before or alongside the physical driver target.

The physical-machine-specific acceptance test remains necessary because QEMU emulation cannot prove correctness against Intel Q77/Ivy Bridge hardware.

## Definition of done for the driver architecture

The architecture is established when:

- device, bus, and driver objects have explicit ownership;
- PCI enumeration creates generic device objects;
- at least one built-in PCI driver binds through the common path;
- at least one loadable PCI driver binds through the same path;
- a failed probe leaves the device safely unbound;
- generic subsystems contain no Chungungo PCI/USB IDs;
- configuration can select supported drivers as `y`, `m`, or `n`;
- the kernel reaches the MFS module store using built-ins only;
- unsupported hardware remains visible but harmless.
