// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file elf64.h
 * @brief Pure ELF64 executable parsing and validation.
 *
 * Defines the supported ELF64 file-format subset and the validated
 * representation consumed by later executable-loading stages.
 */

#ifndef MYOS_ELF_ELF64_H
#define MYOS_ELF_ELF64_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Number of bytes in the ELF identification field.
 */
#define ELF64_IDENT_SIZE 16U

/**
 * Size of an ELF64 executable header.
 */
#define ELF64_HEADER_SIZE 64U

/**
 * Size of one ELF64 program-header table entry.
 */
#define ELF64_PROGRAM_HEADER_SIZE 56U

/**
 * ELF identification magic bytes.
 */
#define ELF64_MAGIC_0 0x7FU
#define ELF64_MAGIC_1 'E'
#define ELF64_MAGIC_2 'L'
#define ELF64_MAGIC_3 'F'

/**
 * Supported ELF class.
 */
#define ELF64_CLASS_64 2U

/**
 * Supported ELF byte order.
 */
#define ELF64_DATA_LITTLE_ENDIAN 1U

/**
 * Current ELF format version.
 */
#define ELF64_VERSION_CURRENT 1U

/**
 * Supported executable file type.
 */
#define ELF64_TYPE_EXECUTABLE 2U

/**
 * Supported ELF machine architecture.
 */
#define ELF64_MACHINE_X86_64 62U

/**
 * ELF extended program-header count marker.
 *
 * Extended program-header numbering depends on section-header metadata and is
 * intentionally unsupported by the initial MyOS ELF64 parser.
 */
#define ELF64_PROGRAM_HEADER_COUNT_EXTENDED 0xFFFFU

/**
 * ELF program-header type for a loadable segment.
 */
#define ELF64_PROGRAM_TYPE_LOAD 1U

/**
 * ELF program-segment permission flags.
 */
#define ELF64_PROGRAM_FLAG_EXECUTE 0x1U
#define ELF64_PROGRAM_FLAG_WRITE   0x2U
#define ELF64_PROGRAM_FLAG_READ    0x4U

/**
 * Decoded ELF64 program header.
 *
 * This structure represents host-side validated field values. It is not
 * overlaid directly on the input ELF byte buffer.
 */
struct elf64_program_header {
    uint32_t type;
    uint32_t flags;

    uint64_t file_offset;
    uint64_t virtual_address;
    uint64_t physical_address;

    uint64_t file_size;
    uint64_t memory_size;
    uint64_t alignment;
};

/**
 * Validated structural description of an ELF64 executable image.
 *
 * The image borrows the input byte buffer supplied to elf64_parse(). The
 * caller must keep that buffer alive and unchanged while this descriptor or
 * program headers derived from it are in use.
 *
 * This stage validates the ELF identification, executable header, and
 * program-header table structure. It does not map process memory or validate
 * PT_LOAD segment mapping semantics such as userspace boundaries, W^X,
 * p_filesz <= p_memsz, or BSS zero-fill requirements.
 */
struct elf64_image {
    const uint8_t *data;
    size_t size;

    uint64_t entry_point;
    uint64_t program_header_offset;

    uint16_t program_header_count;
    uint16_t load_segment_count;
};

/**
 * Parses and structurally validates an ELF64 executable image.
 *
 * Supported images must be 64-bit, little-endian, current-version x86-64
 * ET_EXEC files using the standard ELF64 executable-header and program-header
 * entry sizes.
 *
 * The function performs no allocation and does not mutate process state,
 * paging structures, or the input buffer.
 *
 * @param data Byte buffer containing the complete ELF image.
 * @param size Number of bytes available in data.
 * @param image Output descriptor cleared before validation and populated on
 * successful validation.
 *
 * @return true when the input is a structurally supported ELF64 executable;
 * false otherwise.
 */
bool elf64_parse(
    const void *data,
    size_t size,
    struct elf64_image *image
);

/**
 * Decodes one program header from a previously validated ELF64 image.
 *
 * @param image Validated ELF64 image.
 * @param index Zero-based program-header index.
 * @param program_header Output program-header descriptor.
 *
 * @return true when the requested program header exists and was decoded;
 * false otherwise.
 */
bool elf64_program_header_get(
    const struct elf64_image *image,
    size_t index,
    struct elf64_program_header *program_header
);

#endif
