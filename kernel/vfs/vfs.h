// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file vfs.h
 * @brief Core virtual filesystem object contracts.
 *
 * A vfs_node represents the identity of a filesystem object independently of
 * any particular open operation or process file descriptor.
 *
 * A vfs_file represents one open-file description. It owns one reference to
 * its node and carries state that belongs to that open instance, such as the
 * current file offset and filesystem-specific per-open data.
 *
 * The VFS does not own the storage containing struct vfs_node or
 * struct vfs_file. Filesystems may embed these objects, allocate them
 * dynamically, or provide static storage. Optional destroy operations are
 * responsible for releasing implementation-specific resources and, when
 * appropriate, the storage containing the object itself.
 *
 * VFS reference counts are currently serialized by the kernel's single-CPU
 * execution model. They are not atomic and require explicit synchronization
 * before concurrent VFS access is introduced.
 */

#ifndef MYOS_VFS_VFS_H
#define MYOS_VFS_VFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

/**
 * Maximum length of one filesystem pathname component.
 *
 * Components exclude path separators and are not NUL-terminated by contract.
 */
#define VFS_NAME_MAX 255U

/**
 * Result of resolving one ordinary name inside a directory node.
 */
enum vfs_lookup_result {
    VFS_LOOKUP_RESULT_FOUND,
    VFS_LOOKUP_RESULT_NOT_FOUND,
    VFS_LOOKUP_RESULT_INVALID_ARGUMENT,
    VFS_LOOKUP_RESULT_NOT_DIRECTORY,
    VFS_LOOKUP_RESULT_NOT_SUPPORTED,
    VFS_LOOKUP_RESULT_RESOURCE_EXHAUSTED,
};

/**
 * Result of resolving the parent of one directory node.
 */
enum vfs_parent_result {
    VFS_PARENT_RESULT_FOUND,
    VFS_PARENT_RESULT_NO_PARENT,
    VFS_PARENT_RESULT_INVALID_ARGUMENT,
    VFS_PARENT_RESULT_NOT_DIRECTORY,
    VFS_PARENT_RESULT_NOT_SUPPORTED,
    VFS_PARENT_RESULT_RESOURCE_EXHAUSTED,
};

enum vfs_io_result {
    VFS_IO_RESULT_SUCCESS,
    VFS_IO_RESULT_INVALID_ARGUMENT,
    VFS_IO_RESULT_NOT_SUPPORTED,
    VFS_IO_RESULT_ACCESS_DENIED,
    VFS_IO_RESULT_RESOURCE_EXHAUSTED,
};

enum vfs_open_result {
    VFS_OPEN_RESULT_OPENED,
    VFS_OPEN_RESULT_INVALID_ARGUMENT,
    VFS_OPEN_RESULT_NOT_SUPPORTED,
    VFS_OPEN_RESULT_ACCESS_DENIED,
    VFS_OPEN_RESULT_RESOURCE_EXHAUSTED,
};

enum vfs_stat_result {
    VFS_STAT_RESULT_SUCCESS,
    VFS_STAT_RESULT_INVALID_ARGUMENT,
    VFS_STAT_RESULT_NOT_SUPPORTED,
};

enum vfs_seek_result {
    VFS_SEEK_RESULT_SUCCESS,
    VFS_SEEK_RESULT_INVALID_ARGUMENT,
    VFS_SEEK_RESULT_NOT_SUPPORTED,
};

enum vfs_open_access {
    VFS_OPEN_ACCESS_READ  = 1U << 0,
    VFS_OPEN_ACCESS_WRITE = 1U << 1,
};

enum vfs_seek_origin {
    VFS_SEEK_ORIGIN_START,
    VFS_SEEK_ORIGIN_CURRENT,
    VFS_SEEK_ORIGIN_END,
};

struct vfs_stat {
    enum vfs_node_type type;
    uint64_t size;
};

struct vfs_node;
struct vfs_file;

/**
 * Filesystem-specific operations associated with a VFS node.
 */
struct vfs_node_operations {
    /**
     * Opens one VFS node as an independent open-file description.
     *
     * The filesystem owns the policy and storage used to create the
     * vfs_file. On success, result receives exactly one owned live file
     * reference whose node is node and whose access mode matches access.
     *
     * On failure, no file ownership is transferred.
     *
     * @param node Live node to open.
     * @param access Requested read/write access.
     * @param result Receives one owned open-file description.
     *
     * @return Detailed open result.
     */
    enum vfs_open_result (*open)(
        struct vfs_node *node,
        enum vfs_open_access access,
        struct vfs_file **result
    );

    /**
     * Obtains metadata for one VFS node.
     *
     * The callback must report metadata belonging to node. The generic VFS
     * verifies that the reported object type agrees with node->type before
     * publishing the result to its caller.
     *
     * @param node Live node to inspect.
     * @param result Receives node metadata.
     *
     * @return Detailed stat result.
     */
    enum vfs_stat_result (*stat)(
        struct vfs_node *node,
        struct vfs_stat *result
    );

    /**
     * Looks up one ordinary child name inside a directory.
     *
     * name describes exactly one pathname component and is not required to be
     * NUL-terminated. "." and ".." are handled by the generic pathname layer
     * and are never passed to this callback.
     *
     * On success, the callback returns a borrowed live node pointer. Ownership
     * remains with the filesystem. The generic VFS retains the returned node
     * before exposing it to the lookup caller.
     *
     * Returning NULL means that no child with this name exists.
     *
     * @param directory Directory in which to search.
     * @param name Component bytes.
     * @param name_length Number of component bytes.
     *
     * @return Borrowed child node, or NULL when not found.
     */
    struct vfs_node *(*lookup)(
        struct vfs_node *directory,
        const char *name,
        size_t name_length
    );

    /**
     * Returns the logical parent of one directory.
     *
     * On success, the callback returns a borrowed live node pointer. Ownership
     * remains with the filesystem. The generic VFS retains the returned node
     * before exposing it to the caller.
     *
     * Returning NULL means that this directory has no filesystem-provided
     * parent. Namespace-root handling is performed by the pathname layer and
     * does not require the root node to return itself.
     *
     * @param directory Directory whose parent is requested.
     *
     * @return Borrowed parent node, or NULL when no parent is available.
     */
    struct vfs_node *(*parent)(
        struct vfs_node *directory
    );

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
 * Operations associated with one open-file description.
 *
 * Read and write callbacks operate at the offset supplied by the generic VFS.
 * They must not modify file->offset directly. On successful transfer, the
 * generic VFS advances the shared open-file offset by the reported byte count.
 *
 * Callbacks must not report more bytes transferred than requested.
 */
struct vfs_file_operations {
    /**
     * Reads bytes from one open-file description.
     *
     * offset is the generic VFS offset at which the operation begins. The
     * callback must not modify file->offset.
     * must not modify file->offset.
     *
     * On success, bytes_read receives a value no greater than size. On failure,
     * bytes_read must remain unchanged.
     */
    enum vfs_io_result (*read)(
        struct vfs_file *file,
        uint64_t offset,
        void *buffer,
        size_t size,
        size_t *bytes_read
    );

    /**
     * Writes bytes to one open-file description.
     *
     * offset is the generic VFS offset at which the operation begins. The
     * callback must not modify file->offset.
     *
     * On success, bytes_written receives a value no greater than size. On
     * failure, bytes_written must remain unchanged.
     */
    enum vfs_io_result (*write)(
        struct vfs_file *file,
        uint64_t offset,
        const void *buffer,
        size_t size,
        size_t *bytes_written
    );

    /**
     * Called exactly once when the final file reference is released.
     *
     * The callback is optional. The referenced node is still live while this
     * callback executes, and file->reference_count has already reached zero.
     * The callback must not release the node reference owned by the file; the
     * generic VFS releases that reference after the callback returns.
     *
     * The callback may release the storage containing the file. The VFS must
     * therefore not dereference the file after invoking this operation.
     *
     * @param file Open-file description whose lifetime has ended.
     */
    void (*destroy)(
        struct vfs_file *file
    );
};

/**
 * Represents the identity of a filesystem object.
 *
 * A node exists independently of open-file state. Every live vfs_file retains
 * its node for the entire open-file lifetime.
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
 * Represents one open-file description independently of descriptor numbers.
 *
 * Multiple descriptors may retain the same vfs_file, while separate vfs_file
 * objects referring to the same node keep independent per-open state.
 *
 * A live file owns exactly one reference to node. The node remains retained
 * through the optional file destroy callback and is released immediately
 * afterward by the generic VFS.
 *
 * offset is generic per-open state and starts at zero. access records the
 * read/write permissions requested when this open-file description was
 * created. Both belong to the open-file description and are therefore shared
 * by every descriptor referring to the same vfs_file.
 *
 * private_data is owned and interpreted by the filesystem or device
 * implementation; the generic VFS neither allocates nor releases it directly.
 */
struct vfs_file {
    size_t reference_count;

    struct vfs_node *node;

    uint64_t offset;

    enum vfs_open_access access;

    const struct vfs_file_operations *operations;

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

/**
 * Initializes an open-file description with one owning reference.
 *
 * Initialization first validates access and retains node. If the node is
 * invalid, dead, cannot be retained because its reference count would
 * overflow, or access contains an invalid combination, initialization fails
 * without modifying file.
 *
 * On success, file owns one node reference until its final release. offset is
 * initialized to zero and access records the requested open mode.
 *
 * @param file File storage supplied by the filesystem or VFS caller.
 * @param node Live filesystem object represented by the open file.
 * @param access Requested read/write access.
 * @param operations Optional per-open operations.
 * @param private_data Filesystem-specific per-open state, or NULL.
 *
 * @return true when the file was initialized; false otherwise.
 */
bool vfs_file_initialize(
    struct vfs_file *file,
    struct vfs_node *node,
    enum vfs_open_access access,
    const struct vfs_file_operations *operations,
    void *private_data
);

/**
 * Acquires one additional reference to a live open-file description.
 *
 * Files whose lifetime has ended cannot be resurrected. Reference-count
 * overflow is rejected.
 *
 * @param file Live open-file description to retain.
 *
 * @return true when the reference was acquired; false otherwise.
 */
bool vfs_file_retain(
    struct vfs_file *file
);

/**
 * Releases one reference to an open-file description.
 *
 * Releasing the final file reference changes its reference count to zero,
 * invokes the optional file destroy operation exactly once while node is still
 * live, and finally releases the node reference owned by the file.
 *
 * The destroy operation may free the storage containing file, so callers must
 * treat the file pointer as invalid after a successful final release.
 *
 * @param file Open-file description whose reference is released.
 *
 * @return true when a live file reference was released; false otherwise.
 */
bool vfs_file_release(
    struct vfs_file *file
);

/**
 * Looks up one ordinary child component inside a directory.
 *
 * A successful lookup returns one owning reference through result. The caller
 * must eventually release that reference with vfs_node_release().
 *
 * result is modified only when VFS_LOOKUP_RESULT_FOUND is returned.
 *
 * The component must be non-empty, at most VFS_NAME_MAX bytes long, contain
 * neither '/' nor embedded NUL bytes, and must not be "." or "..".
 *
 * @param directory Live directory node.
 * @param name Pathname component bytes.
 * @param name_length Number of component bytes.
 * @param result Receives one owned child-node reference.
 *
 * @return Detailed lookup result.
 */
enum vfs_lookup_result vfs_node_lookup(
    struct vfs_node *directory,
    const char *name,
    size_t name_length,
    struct vfs_node **result
);

/**
 * Resolves the logical parent of one directory.
 *
 * A successful lookup returns one owning reference through result. The caller
 * must eventually release that reference with vfs_node_release().
 *
 * result is modified only when VFS_PARENT_RESULT_FOUND is returned.
 *
 * @param directory Live directory node.
 * @param result Receives one owned parent-node reference.
 *
 * @return Detailed parent-resolution result.
 */
enum vfs_parent_result vfs_node_parent(
    struct vfs_node *directory,
    struct vfs_node **result
);

enum vfs_open_result vfs_node_open(
    struct vfs_node *node,
    enum vfs_open_access access,
    struct vfs_file **result
);

enum vfs_io_result vfs_file_read(
    struct vfs_file *file,
    void *buffer,
    size_t size,
    size_t *bytes_read
);

enum vfs_io_result vfs_file_write(
    struct vfs_file *file,
    const void *buffer,
    size_t size,
    size_t *bytes_written
);

enum vfs_stat_result vfs_node_stat(
    struct vfs_node *node,
    struct vfs_stat *result
);

enum vfs_seek_result vfs_file_seek(
    struct vfs_file *file,
    int64_t offset,
    enum vfs_seek_origin origin,
    uint64_t *result
);

#endif
