// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file fd_syscalls.S
 * @brief Ring-3 regression program for descriptor file syscalls.
 *
 * This fixture intentionally consumes the published MyOS syscall ABI
 * independently of kernel headers.
 *
 * The regression covers:
 *
 *   open("/file", READ | WRITE)
 *   fstat(fd)
 *   close(fd)
 *   close(fd) -> BAD_DESCRIPTOR
 *   open("/missing", READ) -> NOT_FOUND
 *
 *   read(fd, partially-unmapped-range, size) -> BAD_ADDRESS
 *   write(fd, partially-unmapped-range, size) -> BAD_ADDRESS
 *   successful multi-chunk read
 *   successful multi-chunk write
 *   read-back verification of written data
 *
 *   SEEK_START / SEEK_CURRENT / SEEK_END
 *   positive and negative seek offsets
 *   seek-underflow rejection without offset mutation
 *   INT64_MIN signed-offset decoding
 *   representable seeks beyond EOF
 *   syscall-result overflow without offset mutation
 *   invalid seek origin
 *   seek on a closed descriptor
 *
 * The hostile I/O ranges begin in valid stack memory but extend into the
 * unmapped page above the initial userspace stack. BAD_ADDRESS must therefore
 * be detected before any backend-visible I/O occurs.
 */

.section .text
.global _start
.type _start, @function

/*
 * Published MyOS syscall ABI v1 numbers.
 */
.set SYSCALL_EXIT,       2
.set SYSCALL_FD_OPEN,    7
.set SYSCALL_FD_CLOSE,   8
.set SYSCALL_FD_READ,    9
.set SYSCALL_FD_WRITE,  10
.set SYSCALL_FD_LSEEK,  11
.set SYSCALL_FD_FSTAT,  12

/*
 * Published open access flags.
 */
.set OPEN_ACCESS_READ,   1
.set OPEN_ACCESS_WRITE,  2

/*
 * Published file types.
 */
.set FILE_TYPE_REGULAR_FILE, 1

/*
 * Published syscall errors are returned negated.
 */
.set ERROR_INVALID_ARGUMENT,  -1
.set ERROR_BAD_ADDRESS,       -2
.set ERROR_BAD_DESCRIPTOR,    -6
.set ERROR_NOT_FOUND,         -7
.set ERROR_OVERFLOW,         -11

/*
 * Published seek origins.
 */
.set SEEK_ORIGIN_START,       0
.set SEEK_ORIGIN_CURRENT,     1
.set SEEK_ORIGIN_END,         2

/*
 * Test-file and buffer geometry.
 *
 * 600 bytes deliberately exceeds the kernel's current 256-byte descriptor
 * I/O staging buffer, exercising more than one staging iteration.
 *
 * The stack allocation is rounded to a 16-byte multiple.
 */
.set TEST_FILE_SIZE,             600
.set STACK_BUFFER_SIZE,          608

/*
 * The hostile pointer starts 300 bytes below the top of the mapped stack
 * page and requests 600 bytes. Therefore its first 300 bytes are mapped while
 * the remaining 300 bytes extend into the unmapped page above the stack.
 *
 * This specifically tests full-range prevalidation rather than merely a NULL
 * or immediately-invalid pointer.
 */
.set HOSTILE_VALID_PREFIX_SIZE,  300
.set HOSTILE_RANGE_SIZE,         600

_start:
    cld

    /*
     * Capture the exclusive top of the current mapped stack page before
     * changing RSP.
     *
     * Existing MyOS Ring-3 pointer regressions establish that the initial
     * userspace RSP resides in the highest mapped stack page.
     */
    movq %rsp, %r15
    addq $0xfff, %r15
    andq $-4096, %r15

    /*
     * R15 now points 300 bytes below the mapped stack boundary.
     *
     * [R15, R15 + 300) is valid.
     * [R15 + 300, R15 + 600) is unmapped.
     */
    subq $HOSTILE_VALID_PREFIX_SIZE, %r15

    /*
     * Populate the valid prefix. This is particularly important for the
     * hostile WRITE test: an implementation that validates one chunk at a
     * time could otherwise write the first valid portion before discovering
     * the invalid remainder.
     */
    movq %r15, %rdi
    movq $HOSTILE_VALID_PREFIX_SIZE, %rcx
    movb $0x33, %al
    rep stosb

    /*
     * ----------------------------------------------------------------------
     * Basic OPEN / FSTAT / CLOSE regressions
     * ----------------------------------------------------------------------
     */

    /*
     * Open /file read/write.
     *
     * RDI = pathname address
     * RSI = pathname length
     * RDX = access flags
     */
    movq $SYSCALL_FD_OPEN, %rax
    leaq .Lfile_path(%rip), %rdi
    movq $(.Lfile_path_end - .Lfile_path), %rsi
    movq $(OPEN_ACCESS_READ | OPEN_ACCESS_WRITE), %rdx
    int $0x80

    /*
     * Any negative value is a syscall error.
     */
    testq %rax, %rax
    js .Lfail

    /*
     * Preserve the descriptor across returning syscalls.
     */
    movq %rax, %r12

    /*
     * Reserve exactly one syscall_file_stat:
     *
     *   +0  uint64_t type
     *   +8  uint64_t size
     */
    subq $16, %rsp

    movq $SYSCALL_FD_FSTAT, %rax
    movq %r12, %rdi
    movq %rsp, %rsi
    int $0x80

    testq %rax, %rax
    jne .Lfstat_fail

    cmpq $FILE_TYPE_REGULAR_FILE, 0(%rsp)
    jne .Lfstat_fail

    cmpq $TEST_FILE_SIZE, 8(%rsp)
    jne .Lfstat_fail

    addq $16, %rsp

    /*
     * Close the live descriptor.
     */
    movq $SYSCALL_FD_CLOSE, %rax
    movq %r12, %rdi
    int $0x80

    testq %rax, %rax
    jne .Lfail

    /*
     * Closing the same descriptor again must report BAD_DESCRIPTOR.
     */
    movq $SYSCALL_FD_CLOSE, %rax
    movq %r12, %rdi
    int $0x80

    cmpq $ERROR_BAD_DESCRIPTOR, %rax
    jne .Lfail

    /*
     * A missing pathname must report NOT_FOUND.
     */
    movq $SYSCALL_FD_OPEN, %rax
    leaq .Lmissing_path(%rip), %rdi
    movq $(.Lmissing_path_end - .Lmissing_path), %rsi
    movq $OPEN_ACCESS_READ, %rdx
    int $0x80

    cmpq $ERROR_NOT_FOUND, %rax
    jne .Lfail

    /*
     * ----------------------------------------------------------------------
     * Hostile READ / WRITE range regressions
     * ----------------------------------------------------------------------
     */

    /*
     * Reserve one ordinary userspace I/O buffer.
     */
    subq $STACK_BUFFER_SIZE, %rsp
    movq %rsp, %r13

    /*
     * Open /file read/write at offset zero.
     */
    movq $SYSCALL_FD_OPEN, %rax
    leaq .Lfile_path(%rip), %rdi
    movq $(.Lfile_path_end - .Lfile_path), %rsi
    movq $(OPEN_ACCESS_READ | OPEN_ACCESS_WRITE), %rdx
    int $0x80

    testq %rax, %rax
    js .Lfail

    movq %rax, %r12

    /*
     * The READ destination begins in writable userspace but extends into an
     * unmapped page.
     *
     * The syscall must reject the complete range before the VFS backend sees
     * any read operation or advances the open-file offset.
     */
    movq $SYSCALL_FD_READ, %rax
    movq %r12, %rdi
    movq %r15, %rsi
    movq $HOSTILE_RANGE_SIZE, %rdx
    int $0x80

    cmpq $ERROR_BAD_ADDRESS, %rax
    jne .Lfail

    /*
     * Perform the analogous hostile WRITE.
     *
     * Its first 300 source bytes are valid, which catches implementations
     * that copy/write one chunk before discovering that the full request is
     * invalid.
     */
    movq $SYSCALL_FD_WRITE, %rax
    movq %r12, %rdi
    movq %r15, %rsi
    movq $HOSTILE_RANGE_SIZE, %rdx
    int $0x80

    cmpq $ERROR_BAD_ADDRESS, %rax
    jne .Lfail

    /*
     * A complete 600-byte read must still begin at offset zero.
     *
     * Therefore either hostile syscall having advanced the OFD offset will
     * make this transfer short and fail the test.
     */
    movq $SYSCALL_FD_READ, %rax
    movq %r12, %rdi
    movq %r13, %rsi
    movq $TEST_FILE_SIZE, %rdx
    int $0x80

    cmpq $TEST_FILE_SIZE, %rax
    jne .Lfail

    /*
     * The original file is initialized entirely to 0xa5.
     *
     * This also detects a hostile WRITE that modified the backing store before
     * discovering the invalid tail of the userspace range.
     */
    movq %r13, %rdi
    movq $TEST_FILE_SIZE, %rcx

.Lverify_initial_data:
    cmpb $0xa5, (%rdi)
    jne .Lfail

    incq %rdi
    decq %rcx
    jne .Lverify_initial_data

    /*
     * Close the read/write OFD. Reopening /file creates a new OFD whose offset
     * begins at zero while the filesystem backing data persists.
     */
    movq $SYSCALL_FD_CLOSE, %rax
    movq %r12, %rdi
    int $0x80

    testq %rax, %rax
    jne .Lfail

    /*
     * ----------------------------------------------------------------------
     * Successful multi-chunk WRITE
     * ----------------------------------------------------------------------
     */

    /*
     * Fill the userspace source buffer with a new recognizable value.
     */
    movq %r13, %rdi
    movq $TEST_FILE_SIZE, %rcx
    movb $0x5a, %al
    rep stosb

    /*
     * Reopen at offset zero.
     */
    movq $SYSCALL_FD_OPEN, %rax
    leaq .Lfile_path(%rip), %rdi
    movq $(.Lfile_path_end - .Lfile_path), %rsi
    movq $(OPEN_ACCESS_READ | OPEN_ACCESS_WRITE), %rdx
    int $0x80

    testq %rax, %rax
    js .Lfail

    movq %rax, %r12

    /*
     * 600 bytes exceeds the current kernel staging buffer and therefore
     * exercises multiple internal descriptor-write iterations.
     */
    movq $SYSCALL_FD_WRITE, %rax
    movq %r12, %rdi
    movq %r13, %rsi
    movq $TEST_FILE_SIZE, %rdx
    int $0x80

    cmpq $TEST_FILE_SIZE, %rax
    jne .Lfail

    movq $SYSCALL_FD_CLOSE, %rax
    movq %r12, %rdi
    int $0x80

    testq %rax, %rax
    jne .Lfail

    /*
     * ----------------------------------------------------------------------
     * Successful multi-chunk READ-back
     * ----------------------------------------------------------------------
     */

    /*
     * Clear the destination first so stale source data cannot make a broken
     * read appear successful.
     */
    movq %r13, %rdi
    movq $TEST_FILE_SIZE, %rcx
    xorl %eax, %eax
    rep stosb

    /*
     * Reopen read-only at offset zero.
     */
    movq $SYSCALL_FD_OPEN, %rax
    leaq .Lfile_path(%rip), %rdi
    movq $(.Lfile_path_end - .Lfile_path), %rsi
    movq $OPEN_ACCESS_READ, %rdx
    int $0x80

    testq %rax, %rax
    js .Lfail

    movq %rax, %r12

    movq $SYSCALL_FD_READ, %rax
    movq %r12, %rdi
    movq %r13, %rsi
    movq $TEST_FILE_SIZE, %rdx
    int $0x80

    cmpq $TEST_FILE_SIZE, %rax
    jne .Lfail

    /*
     * Verify every byte written previously survived the complete
     * syscall -> VFS -> backing-store -> syscall round trip.
     */
    movq %r13, %rdi
    movq $TEST_FILE_SIZE, %rcx

.Lverify_written_data:
    cmpb $0x5a, (%rdi)
    jne .Lfail

    incq %rdi
    decq %rcx
    jne .Lverify_written_data

        /*
     * ----------------------------------------------------------------------
     * LSEEK regressions
     * ----------------------------------------------------------------------
     *
     * The descriptor is still open on the regular file. Its current offset
     * is TEST_FILE_SIZE because the preceding complete read consumed all
     * 600 bytes.
     *
     * Seek operations below deliberately exercise all three origins, signed
     * offsets, transactional failures and the syscall result boundary.
     */

    /*
     * SEEK_START: move to an absolute offset inside the file.
     */
    movq $SYSCALL_FD_LSEEK, %rax
    movq %r12, %rdi
    movq $100, %rsi
    movq $SEEK_ORIGIN_START, %rdx
    int $0x80

    cmpq $100, %rax
    jne .Lfail

    /*
     * SEEK_CURRENT with a positive offset.
     *
     * 100 + 50 = 150.
     */
    movq $SYSCALL_FD_LSEEK, %rax
    movq %r12, %rdi
    movq $50, %rsi
    movq $SEEK_ORIGIN_CURRENT, %rdx
    int $0x80

    cmpq $150, %rax
    jne .Lfail

    /*
     * SEEK_CURRENT with a negative signed offset.
     *
     * 150 - 25 = 125.
     *
     * RSI contains the ordinary x86-64 two's-complement representation of
     * -25; the syscall boundary must decode it as a signed int64_t.
     */
    movq $SYSCALL_FD_LSEEK, %rax
    movq %r12, %rdi
    movq $-25, %rsi
    movq $SEEK_ORIGIN_CURRENT, %rdx
    int $0x80

    cmpq $125, %rax
    jne .Lfail

    /*
     * SEEK_END with a negative offset.
     *
     * The fixture reports a 600-byte file, so:
     *
     *   600 - 100 = 500.
     */
    movq $SYSCALL_FD_LSEEK, %rax
    movq %r12, %rdi
    movq $-100, %rsi
    movq $SEEK_ORIGIN_END, %rdx
    int $0x80

    cmpq $500, %rax
    jne .Lfail

    /*
     * Seeking before offset zero must fail transactionally.
     *
     * SEEK_START -1 has no valid unsigned VFS result.
     */
    movq $SYSCALL_FD_LSEEK, %rax
    movq %r12, %rdi
    movq $-1, %rsi
    movq $SEEK_ORIGIN_START, %rdx
    int $0x80

    cmpq $ERROR_INVALID_ARGUMENT, %rax
    jne .Lfail

    /*
     * Confirm the failed seek did not modify the shared OFD offset.
     *
     * SEEK_CURRENT 0 is also a convenient observable "tell me the current
     * offset" operation.
     */
    movq $SYSCALL_FD_LSEEK, %rax
    movq %r12, %rdi
    xorq %rsi, %rsi
    movq $SEEK_ORIGIN_CURRENT, %rdx
    int $0x80

    cmpq $500, %rax
    jne .Lfail

    /*
     * Exercise the INT64_MIN decoder edge explicitly.
     *
     * 0x8000000000000000 represents INT64_MIN in the syscall register ABI.
     * Applying it from SEEK_START necessarily underflows and must leave the
     * existing offset unchanged.
     */
    movq $SYSCALL_FD_LSEEK, %rax
    movq %r12, %rdi
    movabsq $0x8000000000000000, %rsi
    movq $SEEK_ORIGIN_START, %rdx
    int $0x80

    cmpq $ERROR_INVALID_ARGUMENT, %rax
    jne .Lfail

    movq $SYSCALL_FD_LSEEK, %rax
    movq %r12, %rdi
    xorq %rsi, %rsi
    movq $SEEK_ORIGIN_CURRENT, %rdx
    int $0x80

    cmpq $500, %rax
    jne .Lfail

    /*
     * Regular-file seeks beyond EOF are valid.
     *
     * Move exactly to the greatest offset representable as a successful
     * syscall_result_t.
     */
    movq $SYSCALL_FD_LSEEK, %rax
    movq %r12, %rdi
    movabsq $0x7fffffffffffffff, %rsi
    movq $SEEK_ORIGIN_START, %rdx
    int $0x80

    movabsq $0x7fffffffffffffff, %r14

    cmpq %r14, %rax
    jne .Lfail

    /*
     * The VFS itself can represent INT64_MAX + 1, but the syscall ABI cannot
     * return that value as a successful non-negative syscall_result_t.
     *
     * This operation must therefore return OVERFLOW and, crucially, restore
     * the previous OFD offset.
     */
    movq $SYSCALL_FD_LSEEK, %rax
    movq %r12, %rdi
    movq $1, %rsi
    movq $SEEK_ORIGIN_CURRENT, %rdx
    int $0x80

    cmpq $ERROR_OVERFLOW, %rax
    jne .Lfail

    /*
     * Verify the overflow was transactional.
     *
     * If process_file_seek_bounded() had left the VFS mutation visible, this
     * would observe 0x8000000000000000 rather than INT64_MAX.
     */
    movq $SYSCALL_FD_LSEEK, %rax
    movq %r12, %rdi
    xorq %rsi, %rsi
    movq $SEEK_ORIGIN_CURRENT, %rdx
    int $0x80

    cmpq %r14, %rax
    jne .Lfail

    /*
     * Unknown ABI seek origins are rejected before they reach VFS.
     * The OFD offset must again remain unchanged.
     */
    movq $SYSCALL_FD_LSEEK, %rax
    movq %r12, %rdi
    xorq %rsi, %rsi
    movq $99, %rdx
    int $0x80

    cmpq $ERROR_INVALID_ARGUMENT, %rax
    jne .Lfail

    movq $SYSCALL_FD_LSEEK, %rax
    movq %r12, %rdi
    xorq %rsi, %rsi
    movq $SEEK_ORIGIN_CURRENT, %rdx
    int $0x80

    cmpq %r14, %rax
    jne .Lfail

    /*
     * Close the live descriptor after all seek operations.
     */
    movq $SYSCALL_FD_CLOSE, %rax
    movq %r12, %rdi
    int $0x80

    testq %rax, %rax
    jne .Lfail

    /*
     * LSEEK on the now-closed descriptor must report BAD_DESCRIPTOR.
     */
    movq $SYSCALL_FD_LSEEK, %rax
    movq %r12, %rdi
    xorq %rsi, %rsi
    movq $SEEK_ORIGIN_START, %rdx
    int $0x80

    cmpq $ERROR_BAD_DESCRIPTOR, %rax
    jne .Lfail

    addq $STACK_BUFFER_SIZE, %rsp

    /*
     * Every descriptor syscall behaved according to the published ABI.
     */
    movq $SYSCALL_EXIT, %rax
    xorq %rdi, %rdi
    int $0x80

.Lfstat_fail:
    addq $16, %rsp

.Lfail:
    movq $SYSCALL_EXIT, %rax
    movq $1, %rdi
    int $0x80

.Lhang:
    jmp .Lhang

.size _start, . - _start


.section .rodata

/*
 * Pathnames are explicit byte sequences. Their syscall lengths exclude any
 * NUL terminator; none is required by the MyOS pathname ABI.
 */
.Lfile_path:
    .ascii "/file"
.Lfile_path_end:

.Lmissing_path:
    .ascii "/missing"
.Lmissing_path_end:
