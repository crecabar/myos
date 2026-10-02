// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file device.h
 * @brief Generic kernel device identity, topology, and lifetime contracts.
 *
 * A device represents one physical, virtual, or pseudo device known to the
 * kernel independently of VFS publication, bus discovery, driver binding, or
 * userspace naming.
 *
 * The device core does not allocate storage for struct device. Consumers may
 * embed it inside a larger object, allocate it dynamically, or provide static
 * storage.
 *
 * Device lifetime and namespace visibility are intentionally separate. Once
 * removal begins, new references cannot be acquired, while references that
 * already exist may keep the object alive until removal completes and those
 * references are released.
 *
 * Parent/child topology follows asymmetric ownership:
 *
 * - a child owns exactly one reference to its parent;
 * - a parent keeps only non-owning links to its children;
 * - the parent reference is released when the child reaches final
 *   destruction;
 * - parent removal requires every attached child to have reached GONE, but
 *   does not require those child objects to have been physically destroyed.
 */

#ifndef MYOS_CORE_DEVICE_DEVICE_H
#define MYOS_CORE_DEVICE_DEVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DEVICE_IDENTIFIER_INVALID 0ULL

#define DEVICE_NAME_MAX 63U

enum device_kind {
    DEVICE_KIND_PHYSICAL,
    DEVICE_KIND_VIRTUAL,
    DEVICE_KIND_PSEUDO,
};

enum device_class {
    DEVICE_CLASS_NONE,
    DEVICE_CLASS_CHARACTER,
    DEVICE_CLASS_BLOCK,
};

enum device_state {
    DEVICE_STATE_ACTIVE,
    DEVICE_STATE_REMOVING,
    DEVICE_STATE_GONE,
};

struct device;

struct device_operations {
    /**
     * Called exactly once when the final reference to a GONE device is
     * released.
     *
     * reference_count has already reached zero when the callback runs.
     *
     * The device has already been unlinked from its parent's child list.
     * If it has a parent, that parent is still kept alive by the child's
     * topology reference for the duration of this callback. The generic
     * device core releases that parent reference after destroy() returns.
     *
     * The callback may release the storage containing device. The generic
     * device core must therefore not dereference device afterward.
     */
    void (*destroy)(
        struct device *device
    );
};

/**
 * Generic kernel device object.
 *
 * identifier is an opaque kernel-internal identity. Global uniqueness is
 * enforced by the future device registry rather than by this object alone.
 *
 * name is a stable internal name copied into device storage. It is not a VFS
 * pathname and must not contain '/'.
 *
 * device_class and class_interface provide explicit discovery of the primary
 * class-specific I/O contract layered over this object. They do not alter
 * generic device ownership and do not imply any userspace pathname.
 *
 * parent is an owning relationship: every attached child owns one reference
 * to its parent. first_child and sibling links are non-owning topology links.
 *
 * Parent assignment is immutable after initialization in the initial device
 * model. Reparenting is deliberately not supported.
 */
struct device {
    uint64_t identifier;

    char name[
        DEVICE_NAME_MAX + 1U
    ];

    size_t name_length;

    enum device_kind kind;

    /*
     * Primary I/O class exposed by this device.
     *
     * class_interface is an opaque borrowed pointer owned by the concrete
     * class implementation. The generic device core does not know its layout.
     *
     * The initial model permits one primary class per device. Device identity,
     * lifetime, topology, and registry publication remain independent from
     * this binding.
     */
    enum device_class device_class;

    void *class_interface;

    enum device_state state;

    size_t reference_count;

    struct device *parent;

    struct device *first_child;

    struct device *previous_sibling;
    struct device *next_sibling;

    const struct device_operations *operations;

    void *private_data;
};

/**
 * Initializes one root/unparented caller-owned device object.
 *
 * On success, device begins ACTIVE with one owning reference and no parent.
 */
bool device_initialize(
    struct device *device,
    uint64_t identifier,
    const char *name,
    size_t name_length,
    enum device_kind kind,
    const struct device_operations *operations,
    void *private_data
);

/**
 * Initializes one caller-owned child device.
 *
 * parent must be a distinct, live ACTIVE device.
 *
 * On success:
 *
 * - child begins ACTIVE with one owning reference;
 * - child owns one additional reference to parent;
 * - child is linked into parent's child topology.
 *
 * The parent's reference is held until the child is finally destroyed, even
 * if both objects have already reached GONE.
 *
 * Invalid arguments or inability to retain parent leave both objects
 * unchanged.
 */
bool device_initialize_child(
    struct device *device,
    struct device *parent,
    uint64_t identifier,
    const char *name,
    size_t name_length,
    enum device_kind kind,
    const struct device_operations *operations,
    void *private_data
);

/**
 * Binds one primary I/O class to an ACTIVE device.
 *
 * Binding is one-shot for the lifetime of the initialized device. class must
 * not be DEVICE_CLASS_NONE and interface must be non-NULL.
 *
 * The interface pointer is borrowed. The class implementation must remain
 * alive for as long as the generic device can be reached.
 *
 * No device reference is acquired.
 */
bool device_class_bind(
    struct device *device,
    enum device_class device_class,
    void *interface
);

/**
 * Discovers one class interface exposed by an ACTIVE device.
 *
 * The requested class must match the device's bound class exactly.
 *
 * The returned interface is borrowed and does not acquire device ownership.
 * REMOVING and GONE devices are deliberately no longer discoverable.
 *
 * @return Borrowed class interface, or NULL when unavailable.
 */
void *device_class_interface(
    struct device *device,
    enum device_class device_class
);

bool device_retain(
    struct device *device
);

/**
 * Releases one existing device reference.
 *
 * The final reference may be released only after the device reaches GONE.
 *
 * If the final device is a child, it is unlinked from its parent's topology
 * before destroy() runs. Its owning parent reference remains held while the
 * child destroy callback executes and is released immediately afterward.
 */
bool device_release(
    struct device *device
);

/**
 * Begins logical removal of one ACTIVE device.
 *
 * Every attached child must already be GONE. GONE children may remain alive
 * due to outstanding references and continue retaining the parent object.
 *
 * After success, no new child can be attached because the parent is no longer
 * ACTIVE, and device_retain() rejects new references.
 */
bool device_begin_removal(
    struct device *device
);

bool device_finish_removal(
    struct device *device
);

#endif
