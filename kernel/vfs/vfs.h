// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file vfs.h
 * @brief Core virtual filesystem object contracts.
 *
 * A vfs_node represents the identity of a filesystem object independently of
 * any particular open operation or process file descriptor.
 *
 * The VFS does not own the storage containing struct vfs_node. A filesystem
 * may embed a node in another object, allocate it dynamically, or provide
 * static storage. When the final reference is released, the optional destroy
 * operation is responsible for releasing filesystem-specific resources and,
 * when appropriate, the storage containing the node itself.
 *
 * Node reference counts are currently serialized by the kernel's single-CPU
 * execution model. They are not atomic and require explicit synchronization
 * before concurrent VFS access is introduced.
 */

#ifndef MYOS_VFS_VFS_H
#define MYOS_VFS_VFS_H

#include <stdbool.h>
#include <stddef.h>

/**
 * Identifies the semantic kind of a VFS object.
 *
 * The initial interface intentionally contains only the object classes needed
 * by the M8 filesystem and device-node roadmap. Additional kinds may be added
 * without changing node ownership semantics.
 */
enum vfs_node_type {
    VFS_NODE_TYPE_REGULAR_FILE,
    VFS_NODE_TYPE_DIRECTORY,
    VFS_NODE_TYPE_CHARACTER_DEVICE,
};

struct vfs_node;

/**
 * Filesystem-specific operations associated with a VFS node.
 */
struct vfs_node_operations {
    /**
     * Called exactly once when the final node reference is released.
     *
     * The callback is optional. When present, it receives a node whose
     * reference count has already reached zero.
     *
     * The callback may release the storage containing the node. The VFS must
     * therefore not dereference the node after invoking this operation.
     *
     * @param node Node whose lifetime has ended.
     */
    void (*destroy)(
        struct vfs_node *node
    );
};

/**
 * Represents the identity of a filesystem object.
 *
 * A node exists independently of open-file state. Future vfs_file objects
 * will retain a node while an open-file description refers to it.
 *
 * reference_count greater than zero means the node is live. A transition from
 * one reference to zero ends the lifetime permanently; a zero-reference node
 * cannot be retained again.
 *
 * private_data is owned and interpreted by the filesystem implementation.
 * The generic VFS neither allocates nor releases it directly.
 */
struct vfs_node {
    enum vfs_node_type type;

    size_t reference_count;

    const struct vfs_node_operations *operations;

    void *private_data;
};

/**
 * Initializes a VFS node with one owning reference.
 *
 * This function allocates no resources and takes no ownership of node storage
 * or private_data. The caller becomes the holder of the initial reference.
 *
 * @param node Node storage supplied by the filesystem.
 * @param type Semantic node type.
 * @param operations Optional filesystem-specific operations.
 * @param private_data Filesystem-specific state, or NULL.
 *
 * @return true when the node was initialized; false for invalid input.
 */
bool vfs_node_initialize(
    struct vfs_node *node,
    enum vfs_node_type type,
    const struct vfs_node_operations *operations,
    void *private_data
);

/**
 * Acquires one additional reference to a live VFS node.
 *
 * Nodes whose lifetime has ended cannot be resurrected. Reference-count
 * overflow is rejected.
 *
 * @param node Live node to retain.
 *
 * @return true when the reference was acquired; false otherwise.
 */
bool vfs_node_retain(
    struct vfs_node *node
);

/**
 * Releases one reference to a VFS node.
 *
 * Releasing the final reference changes the reference count to zero and then
 * invokes the optional filesystem destroy operation exactly once.
 *
 * The destroy operation may free the storage containing node, so callers must
 * treat the node pointer as invalid after a successful final release.
 *
 * @param node Node whose reference is released.
 *
 * @return true when a live reference was released; false otherwise.
 */
bool vfs_node_release(
    struct vfs_node *node
);

#endif