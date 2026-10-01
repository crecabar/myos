// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Cristian Recabarren
 */

/**
 * @file device.h
 * @brief Generic kernel device identity and lifetime contracts.
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
 */

#ifndef MYOS_CORE_DEVICE_DEVICE_H
#define MYOS_CORE_DEVICE_DEVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Device identifier value reserved to mean "no identity".
 */
#define DEVICE_IDENTIFIER_INVALID 0ULL

/**
 * Maximum number of bytes in one stable internal device name.
 *
 * The terminating NUL stored by struct device is not included in this limit.
 */
#define DEVICE_NAME_MAX 63U

/**
 * Describes the origin of a generic device object.
 *
 * This is deliberately not a functional device class. Character, block,
 * display, input, network, and other subsystem boundaries are defined
 * separately.
 */
enum device_kind {
    DEVICE_KIND_PHYSICAL,
    DEVICE_KIND_VIRTUAL,
    DEVICE_KIND_PSEUDO,
};

/**
 * Describes generic device lifetime state.
 *
 * ACTIVE devices may acquire new references.
 *
 * REMOVING devices are no longer available for new acquisitions but remain
 * alive while existing references are released.
 *
 * GONE devices have completed logical removal. They remain allocated until
 * their final reference is released.
 */
enum device_state {
    DEVICE_STATE_ACTIVE,
    DEVICE_STATE_REMOVING,
    DEVICE_STATE_GONE,
};

struct device;

/**
 * Optional lifetime operations associated with one device object.
 */
struct device_operations {
    /**
     * Called exactly once when the final reference to a GONE device is
     * released.
     *
     * reference_count has already reached zero when the callback runs.
     *
     * The callback may release the storage containing device. The generic
     * device core must therefore not dereference device afterward.
     *
     * @param device Device whose lifetime has ended.
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
 * private_data belongs entirely to the consumer implementing the concrete
 * device.
 *
 * reference_count and state are managed by the device core. They are
 * currently serialized by MyOS's single-CPU execution model and will require
 * synchronization before concurrent device-core access is introduced.
 */
struct device {
    uint64_t identifier;

    char name[
        DEVICE_NAME_MAX + 1U
    ];

    size_t name_length;

    enum device_kind kind;
    enum device_state state;

    size_t reference_count;

    const struct device_operations *operations;

    void *private_data;
};

/**
 * Initializes one caller-owned device object.
 *
 * identifier must not be DEVICE_IDENTIFIER_INVALID.
 *
 * name is supplied as an explicit byte sequence and need not be
 * NUL-terminated. It must be non-empty, at most DEVICE_NAME_MAX bytes long,
 * contain no embedded NUL, and contain no '/' because device identity must
 * not encode a VFS pathname.
 *
 * On success, device begins ACTIVE with one owning reference. The internal
 * name is copied and NUL-terminated.
 *
 * Invalid arguments leave device unchanged.
 *
 * @param device Caller-owned device storage.
 * @param identifier Non-zero opaque identity.
 * @param name Stable internal name bytes.
 * @param name_length Number of name bytes.
 * @param kind Device origin kind.
 * @param operations Optional lifetime operations.
 * @param private_data Consumer-owned implementation state.
 *
 * @return true when initialization succeeded; false otherwise.
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
 * Acquires one additional reference to an ACTIVE device.
 *
 * REMOVING and GONE devices cannot acquire new references. Reference-count
 * overflow is rejected.
 *
 * @param device Device to retain.
 *
 * @return true when the reference was acquired; false otherwise.
 */
bool device_retain(
    struct device *device
);

/**
 * Releases one existing device reference.
 *
 * References may be released while the device is ACTIVE, REMOVING, or GONE.
 * However, the final owning reference may be released only after the device
 * reaches GONE.
 *
 * Releasing the final reference sets reference_count to zero and invokes the
 * optional destroy callback exactly once.
 *
 * @param device Device whose reference is released.
 *
 * @return true when a reference was released; false for invalid state or when
 *         releasing the final reference before DEVICE_STATE_GONE.
 */
bool device_release(
    struct device *device
);

/**
 * Begins logical removal of one ACTIVE device.
 *
 * After success, the device enters REMOVING and device_retain() rejects new
 * references. Existing references remain valid and may be released.
 *
 * @param device Device to remove.
 *
 * @return true for the ACTIVE -> REMOVING transition; false otherwise.
 */
bool device_begin_removal(
    struct device *device
);

/**
 * Completes logical removal of one REMOVING device.
 *
 * The device enters GONE but remains allocated while references exist.
 *
 * @param device Device whose removal has completed.
 *
 * @return true for the REMOVING -> GONE transition; false otherwise.
 */
bool device_finish_removal(
    struct device *device
);

#endif
