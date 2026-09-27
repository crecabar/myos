# MyOS 1.0 Chungungo Reference Platform

This document defines the hardware support contract for the first MyOS version intended to run as a usable system on **Chungungo**, the project's Dell OptiPlex 7010 reference workstation.

The goal is not to support every device exposed by the machine. MyOS 1.0 supports the hardware needed to boot, interact with the system, access its primary storage, and provide a practical path toward networking, graphics, and audio.

The configuration policy is:

```text
y = vital: built into kernel.elf
m = desirable: loadable MyOS kernel module
n = unsupported in MyOS 1.0: not compiled
```

## Reference machine

Known platform characteristics:

```text
System:       Dell OptiPlex 7010
Motherboard:  Dell 0KRC95 A02
Firmware:     Dell A29
Firmware API: UEFI, ACPI
CPU:          Intel Xeon E3-1275 v2
Generation:   Ivy Bridge
Topology:     4 cores / 8 logical CPUs
Memory:       16 GiB DDR3-1600
Chipset:      Intel Q77
```

MyOS should detect capabilities rather than keying normal operation on the Dell product name.

## PCI hardware contract

| Device | PCI ID | MyOS 1.0 | Purpose |
| --- | --- | --- | --- |
| Ivy Bridge host/DRAM controller | `8086:0158` | infrastructure | enumerated; no dedicated operational driver required initially |
| Ivy Bridge integrated graphics | `8086:016a` | `m` | native display/modesetting path |
| Intel xHCI | `8086:1e31` | `y` | USB 2/3 host controller |
| Intel MEI | `8086:1e3a` | `n` | outside 1.0 scope |
| Intel KT serial controller | `8086:1e3d` | `y` | 16550-compatible serial |
| Intel 82579LM Ethernet | `8086:1502` | `m` | Gigabit Ethernet |
| Intel EHCI #2 | `8086:1e2d` | `y` | USB 2 host controller |
| Intel HDA | `8086:1e20` | `m` | audio controller |
| Intel EHCI #1 | `8086:1e26` | `y` | USB 2 host controller |
| Intel PCI bridge | `8086:244e` | infrastructure | PCI topology |
| Intel Q77 LPC | `8086:1e47` | `n` initially | chipset functions not required for 1.0 |
| Intel AHCI | `8086:1e02` | `y` | primary SATA storage |
| Intel SMBus | `8086:1e22` | `n` | outside 1.0 scope |

## Boot-critical built-ins

The following components form the 1.0 bootstrap closure and must be available without loading files from the root filesystem.

```text
CONFIG_X86_64=y
CONFIG_UEFI=y
CONFIG_ACPI=y
CONFIG_ACPI_MADT=y
CONFIG_ACPI_MCFG=y
CONFIG_ACPI_HPET=y

CONFIG_LAPIC=y
CONFIG_IOAPIC=y
CONFIG_PCI=y

CONFIG_SERIAL_8250=y
CONFIG_FRAMEBUFFER=y

CONFIG_BLOCK=y
CONFIG_AHCI=y
CONFIG_ATA=y
CONFIG_GPT=y
CONFIG_MFS=y

CONFIG_USB=y
CONFIG_USB_HUB=y
CONFIG_USB_EHCI=y
CONFIG_USB_XHCI=y
CONFIG_USB_HID=y
CONFIG_INPUT_KEYBOARD=y
CONFIG_INPUT_MOUSE=y

CONFIG_MODULES=y
```

Names are architectural targets, not a commitment that the current Makefile already exposes these exact symbols.

### Serial recovery path

Chungungo exposes a legacy 16550A-compatible `ttyS0` at I/O port `0x3f8`, IRQ 4, 115200 base baud. It also exposes a PCI serial controller at `8086:1e3d`.

The legacy COM1 path should remain the primary early diagnostic console for MyOS 1.0.

It must not depend on:

- PCI initialization;
- USB;
- graphics;
- the root filesystem;
- userspace.

### Storage bootstrap path

The required disk path is:

```text
PCI
 -> Intel AHCI 8086:1e02
 -> ATA
 -> OWC Aura Pro S MB258
 -> partition layer
 -> MFS root filesystem
 -> /lib/modules/<version>/
```

The controller is AHCI 1.3 capable, exposes six SATA ports with two implemented on the reference machine, and the SSD is attached through the SATA path.

AHCI, the block layer, the partition format used for the MyOS installation, and MFS must therefore be built in.

ATAPI support for the MATSHITA DVD drive is not required to mount the normal root filesystem and should be modular when implemented.

### Interactive input bootstrap path

The normal Chungungo keyboard/mouse receiver is:

```text
USB 03f0:5341
PIXART / HP Wireless Keyboard and Mouse
```

It exposes separate HID interfaces for keyboard and mouse and is currently attached through the EHCI topology.

Therefore this entire path is built in:

```text
PCI
 -> EHCI
 -> USB core
 -> hub
 -> HID
 -> input
 -> keyboard/mouse
```

xHCI is also built in so USB 3 and xHCI-routed USB 2 devices are part of the baseline machine support.

## Loadable drivers

Once MFS is mounted, MyOS may load desirable hardware support from the module store.

### Intel 82579LM Ethernet

```text
PCI 8086:1502
 -> e1000e-like MyOS driver
 -> net_device
 -> Ethernet layer
 -> MyOS network stack
```

Target configuration:

```text
CONFIG_NET=y
CONFIG_ETHERNET=y
CONFIG_E1000E=m
```

The implementation is MyOS-native. The name `e1000e` describes the hardware family and does not imply Linux source or ABI compatibility.

### Intel Ivy Bridge integrated graphics

The native display controller is Intel Ivy Bridge integrated graphics, PCI `8086:016a`.

MyOS 1.0 must not require this module merely to display diagnostics. Early display continues to use the boot framebuffer.

The intended transition is:

```text
UEFI/Limine framebuffer
 -> generic MyOS framebuffer console

later:

graphics core
 -> Intel Ivy Bridge/Gen7 module
 -> native modesetting/display
 -> userspace graphics interface
```

Target configuration:

```text
CONFIG_GRAPHICS=y
CONFIG_BOOT_FRAMEBUFFER=y
CONFIG_DRM_LIKE_CORE=y
CONFIG_DRM_INTEL_IVYBRIDGE=m
```

The exact graphics ABI should be designed around MyOS requirements. It should not claim Linux DRM ABI compatibility unless that is deliberately implemented later.

Native Intel graphics support is a major dependency of the longer-term `startx` / `xclock` objective, but the boot framebuffer prevents it from blocking basic 1.0 boot and diagnostics.

### Intel HDA / Realtek ALC269VB

The PCI audio controller is `8086:1e20`. The machine exposes a Realtek ALC269VB codec and Intel HDMI/DisplayPort audio paths.

Target configuration:

```text
CONFIG_AUDIO=m
CONFIG_HDA=m
CONFIG_HDA_CODEC_REALTEK=m
CONFIG_HDA_CODEC_INTEL_HDMI=m
```

Audio failure must never prevent boot.

The HDA controller and codecs should remain separate abstractions so the generic audio subsystem does not contain Chungungo-specific codec knowledge.

### ATAPI optical drive

The SATA optical device is a MATSHITA DVD+/-RW SW820.

Target configuration:

```text
CONFIG_ATAPI=m
```

Optical media is desirable but not part of the root-storage bootstrap closure.

### USB mass storage

USB mass storage is useful for removable media but is not required for the normal installed-system root path.

Target configuration:

```text
CONFIG_USB_STORAGE=m
```

The module depends on USB core and the relevant built-in host-controller driver.

## Not compiled for MyOS 1.0

The following detected facilities are intentionally outside the 1.0 support contract:

```text
CONFIG_INTEL_MEI=n
CONFIG_I2C_I801=n
CONFIG_FIREWIRE=n
CONFIG_VTD=n
```

This does not mean the hardware is unusable forever. It means MyOS 1.0 makes no support claim and does not include the corresponding driver.

ACPI power-management functionality beyond what is required for discovery and basic shutdown may also remain incomplete in 1.0.

## ACPI requirements

Chungungo firmware exposes, among other tables:

- FACP/FADT;
- DSDT;
- MADT/APIC;
- MCFG;
- HPET;
- SSDTs;
- DMAR.

MyOS 1.0 should use the information it needs incrementally.

Required early targets are:

- RSDP/XSDT discovery;
- MADT for interrupt-controller and CPU topology information;
- MCFG for PCIe ECAM where implemented;
- HPET description where the timer subsystem chooses to use HPET;
- FADT information required by supported platform operations.

DMAR/VT-d may be parsed or reported for diagnostics but IOMMU operation is not part of the 1.0 hardware contract.

## SMP note

The reference CPU has four physical cores and eight logical CPUs.

The hardware target should not hard-code eight CPUs. CPU discovery belongs to ACPI/MADT and architecture initialization.

Whether MyOS 1.0 schedules normal workloads across all discovered CPUs is a kernel milestone separate from merely recognizing the reference hardware.

## Unsupported-device behavior

Discovery of an unsupported PCI or USB device must be harmless.

For example:

```text
[pci] 00:16.0 8086:1e3a class=0780: no driver
```

is a valid result.

An unsupported device must not cause a panic merely because it exists.

## Acceptance criteria

The Chungungo hardware target can be considered supported for MyOS 1.0 when the installed system can, on physical hardware:

1. boot through the supported firmware/boot path;
2. discover the platform and interrupt topology without machine-specific constants;
3. enumerate PCI devices;
4. retain a working serial diagnostic console;
5. initialize AHCI and discover the primary SSD;
6. mount the MyOS root filesystem from that disk;
7. initialize the USB host controllers needed by the machine;
8. receive keyboard and mouse input through USB HID;
9. load configured modules from the root filesystem;
10. load and bind the 82579LM network module;
11. continue booting when optional graphics, audio, optical, or removable-storage modules are absent or fail.

Native Intel graphics and audio may have their own functional acceptance milestones, but their failure must not invalidate the boot-critical platform path.
