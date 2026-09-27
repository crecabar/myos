# MyOS Device Model

This document defines the device, bus, and driver model planned for MyOS.

The immediate purpose is to support the hardware required by the MyOS 1.0 reference machine without coupling kernel subsystems to individual devices. The model should remain small, explicit, and understandable. MyOS is not trying to reproduce the Linux driver core.

## Goals

The device model should provide:

- one representation for discovered hardware devices;
- explicit bus ownership and enumeration;
- explicit driver matching and binding;
- clean separation between kernel subsystems and hardware-specific drivers;
- the ability to build the same driver into the kernel or as a loadable module;
- deterministic initialization and failure reporting;
- enough lifetime management to support probing, removal, and module unloading where safe.

The model should not initially provide:

- automatic dependency solving comparable to a general-purpose package manager;
- hotplug for every bus;
- power-management frameworks beyond what MyOS 1.0 needs;
- a userspace device manager;
- Linux ABI compatibility.

## Core objects

The first implementation should revolve around three small objects:

```c
struct device;
struct driver;
struct bus;
```

A device represents one discovered hardware or logical device. A driver represents code capable of controlling matching devices. A bus owns discovery and bus-specific matching information.

Conceptually:

```text
bus
 |
 +-- enumerates --> device
 |
 +-- owns drivers
          |
          +-- match(device)
          +-- probe(device)
          +-- remove(device)
```

A minimal generic driver interface can begin as:

```c
struct driver {
    const char *name;
    struct bus *bus;

    bool (*match)(const struct device *device);
    int (*probe)(struct device *device);
    void (*remove)(struct device *device);
};
```

The exact structure may evolve while the implementation is written. The important invariant is that generic subsystems do not identify or program concrete hardware.

## Device lifetime

A device should move through explicit states:

```text
discovered
    |
    v
unbound
    |
    | matching driver found
    v
probing
    |
    +---- failure ----> unbound
    |
    v
bound
    |
    | remove/unload
    v
unbound
```

A failed probe must not leave interrupts enabled, DMA active, BAR mappings owned, or subsystem objects registered.

The driver that successfully probes a device owns its device-specific runtime state until removal.

## Bus model

Each bus supplies the operations needed to discover devices and expose bus-specific resources.

The first buses are expected to be:

- PCI/PCIe;
- USB.

ACPI is platform-discovery infrastructure rather than a normal peripheral bus in the first implementation. It supplies information used by interrupt, timer, PCI, power, and platform initialization.

### PCI

PCI should expose a generic `struct pci_device` derived from `struct device`.

At minimum it should record:

- segment, bus, device, and function;
- vendor and device ID;
- class, subclass, and programming interface;
- revision;
- subsystem vendor/device IDs when present;
- BARs;
- interrupt information;
- PCI capabilities needed by supported drivers.

Driver matching should be table driven:

```c
static const struct pci_device_id e1000e_ids[] = {
    { PCI_DEVICE(0x8086, 0x1502) },
    { 0 }
};
```

The PCI core, not the Ethernet driver, owns enumeration and configuration-space access.

PCI configuration access should be abstracted:

```text
pci_config_ops
 |
 +-- legacy CF8/CFC
 |
 +-- PCIe ECAM from ACPI MCFG
```

This avoids making a particular firmware or configuration mechanism part of every PCI driver.

### USB

USB should be layered rather than implemented as one monolithic driver:

```text
USB core
 |
 +-- host-controller driver
 |    +-- EHCI
 |    +-- xHCI
 |
 +-- hub
 |
 +-- device/configuration/interface enumeration
 |
 +-- class drivers
      +-- HID
      +-- mass storage
```

Host-controller drivers own controller-specific rings, schedules, DMA structures, and interrupts. USB class drivers consume generic USB interfaces and endpoints.

## Subsystems

Hardware drivers should register generic subsystem objects.

Examples:

```text
AHCI driver       -> block device
82579LM driver    -> network device
USB HID driver    -> input device
HDA driver        -> audio device
Intel display     -> graphics/display device
16550 driver      -> TTY/console device
```

The block layer must not know AHCI. The network stack must not know Intel 82579LM. The input layer must not know EHCI. The graphics core must not know Ivy Bridge register layouts.

This boundary is a central design rule.

## Driver source organization

Driver location and driver linkage are separate concerns.

A proposed layout is:

```text
kernel/
├── arch/
│   └── x86_64/
├── core/
│   ├── device/
│   └── module/
├── firmware/
│   └── acpi/
├── bus/
│   ├── pci/
│   └── usb/
├── subsys/
│   ├── block/
│   ├── net/
│   ├── input/
│   ├── tty/
│   ├── graphics/
│   └── audio/
└── drivers/
    ├── serial/8250/
    ├── storage/ahci/
    ├── net/intel/e1000e/
    ├── usb/ehci/
    ├── usb/xhci/
    ├── usb/hid/
    ├── graphics/intel/
    └── audio/hda/
```

A source file under `drivers/` is not automatically a module. Its linkage is selected by configuration.

## Build policy: y, m, n

MyOS should use three states for optional hardware support:

```text
y = built into kernel.elf
m = built as a loadable kernel module
n = not built
```

For example:

```text
CONFIG_AHCI=y
CONFIG_E1000E=m
CONFIG_USB_XHCI=y
CONFIG_USB_HID=y
CONFIG_INTEL_MEI=n
```

The same driver implementation should support `y` and `m`. There should not be separate built-in and module versions.

### When a driver must be built in

A component is `y` when it is required before the root filesystem and module store are available, or when losing it would remove the minimum recovery/debug path required by the 1.0 reference platform.

Typical examples are:

- platform discovery required to reach hardware;
- PCI;
- root-storage controller;
- root filesystem;
- serial diagnostics;
- boot framebuffer;
- the USB/input path required for the reference keyboard.

### When a driver should be a module

A component is `m` when:

- the kernel can boot and mount the module store without it;
- it is desirable for normal operation;
- its subsystem already exists when it is loaded;
- unloading it can be made safe, or unloading is explicitly unsupported initially.

### When support is not built

A component is `n` when it is outside the MyOS 1.0 hardware contract.

`n` means the implementation is absent from the image, not that the kernel pretends to support the device.

## Module contract

The module loader should consume MyOS-native relocatable ELF modules. It does not imply Linux `.ko` compatibility.

A module needs enough metadata to describe:

- name;
- MyOS/module ABI version;
- initialization entry point;
- cleanup entry point where supported;
- dependencies;
- exported/imported kernel symbols;
- license metadata if desired by project policy.

Conceptually:

```text
kernel.elf
   |
   +-- exported kernel/module ABI
   |
   +-- module loader
          |
          +-- validate ELF
          +-- resolve symbols
          +-- apply supported relocations
          +-- initialize
          +-- register driver(s)
```

The module ABI is a MyOS ABI and may change until explicitly stabilized.

## Initialization order

The initial dependency order should remain explicit:

```text
boot information
  -> memory management
  -> architecture/interrupt foundations
  -> ACPI
  -> device core
  -> PCI
  -> built-in PCI drivers
  -> block/storage
  -> root filesystem
  -> module loader/module store
  -> optional modules
  -> userspace
```

USB initialization can occur after PCI discovers the host controllers, but the Chungungo 1.0 configuration requires its input path before normal interactive operation.

## Error handling

Probe failures should be observable through diagnostics and should not normally panic the kernel.

Panic is appropriate when a required built-in component cannot establish an invariant required to continue booting. For example, failure to initialize the only configured root-storage path is fatal to a normal disk boot.

Failure of an optional network or audio module is not fatal.

## Design constraints

The first implementation should prefer:

- explicit tables over dynamic metaprogramming;
- small interfaces over generalized frameworks;
- synchronous probe before asynchronous complexity;
- deterministic ownership;
- readable diagnostics;
- no hidden hardware-specific behavior in generic subsystems.

Abstraction should be introduced where Chungungo and QEMU already demonstrate that more than one implementation is useful, or where a hardware boundary is architecturally unavoidable.
