// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file programs.c
 * @brief Built-in user-mode program images.
 */

#include "programs.h"

static const uint8_t hello_program_data[] = {
    // write(message, 24)
    // mov rax, 4
    0x48, 0xC7, 0xC0, 0x04, 0x00, 0x00, 0x00,
    //mov rdi, 0x400025
    0x48, 0xC7, 0xC7, 0x25, 0x00, 0x40, 0x00,
    // mov rsi, 24
    0x48, 0xC7, 0xC6, 0x18, 0x00, 0x00, 0x00,
    // int 0x80
    0xCD, 0x80,
    // exit(0)
    // mov rax, 2
    0x48, 0xC7, 0xC0, 0x02, 0x00, 0x00, 0x00,
    // xor rdi, rdi
    0x48, 0x31, 0xFF,
    // int 0x80
    0xCD, 0x80,
    // Unreachable fallback loop.
    0xEB, 0xFE,
    // "Hello world from ring3!\n"
    0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x20,
    0x77, 0x6F, 0x72, 0x6C, 0x64, 0x20,
    0x66, 0x72, 0x6F, 0x6D, 0x20,
    0x72, 0x69, 0x6E, 0x67, 0x33, 0x21,
    0x0A,
};

static const struct user_program hello_program = {
    .data = hello_program_data,
    .size = sizeof(hello_program_data),
};

static const uint8_t counter_program_data[] = {
    /* mov rbx, 0 */
    0x48, 0xC7, 0xC3, 0x00, 0x00, 0x00, 0x00,

    /*
     * loop:
     *
     * putc('0' + rbx)
     */
    0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00, /* mov rax, 1 */
    0x48, 0x89, 0xDF,                                          /* mov rdi, rbx */
    0x48, 0x83, 0xC7, 0x30,                               /* add rdi, '0' */
    0xCD, 0x80,                                                     /* int 0x80 */

    /* putc('\n') */
    0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00,
    0x48, 0xC7, 0xC7, 0x0A, 0x00, 0x00, 0x00,
    0xCD, 0x80,

    /*
     * Small workload so timer-driven preemption has time to occur.
     *
     * mov rcx, 0x02000000
     */
    0x48, 0xC7, 0xC1, 0x00, 0x00, 0x00, 0x02,

    /* workload_loop: dec rcx */
    0x48, 0xFF, 0xC9,

    /* jnz workload_loop (-5 bytes) */
    0x75, 0xFB,

    /* inc rbx */
    0x48, 0xFF, 0xC3,

    /* cmp rbx, 10 */
    0x48, 0x83, 0xFB, 0x0A,

    /*
     * jne loop
     *
     * From the byte after this instruction back to the first
     * "mov rax, 1" of the loop.
     */
    0x75, 0xCB,

    /* exit(0) */
    0x48, 0xC7, 0xC0, 0x02, 0x00, 0x00, 0x00,
    0x48, 0x31, 0xFF,
    0xCD, 0x80,

    /* unreachable fallback loop */
    0xEB, 0xFE,
};

static const struct user_program counter_program = {
    .data = counter_program_data,
    .size = sizeof(counter_program_data),
};

static const uint8_t malicious_page_fault_program_data[] = {
    /* putc('P') */
    0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00, /* mov rax, 1 */
    0x48, 0xC7, 0xC7, 0x50, 0x00, 0x00, 0x00, /* mov rdi, 'P' */
    0xCD, 0x80,                               /* int 0x80 */

    /* putc('\n') */
    0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00,
    0x48, 0xC7, 0xC7, 0x0A, 0x00, 0x00, 0x00,
    0xCD, 0x80,

    /*
     * Deliberately read from virtual address zero.
     *
     * xor rax, rax
     * mov rbx, [rax]
     */
    0x48, 0x31, 0xC0,
    0x48, 0x8B, 0x18,

    /* Unreachable fallback loop. */
    0xEB, 0xFE,
};

static const struct user_program malicious_page_fault_program = {
    .data = malicious_page_fault_program_data,
    .size = sizeof(malicious_page_fault_program_data),
};

static const uint8_t malicious_ud2_program_data[] = {
    /*
     * write("[evil] UD2\n", 11)
     *
     * The message begins at 0x40001B:
     *
     *   mov rax, 4      7 bytes
     *   mov rdi, ...    7 bytes
     *   mov rsi, 11     7 bytes
     *   int 0x80        2 bytes
     *   ud2             2 bytes
     *   fallback loop   2 bytes
     *                  --------
     *                  27 bytes = 0x1B
     */

    /* mov rax, 4 */
    0x48, 0xC7, 0xC0, 0x04, 0x00, 0x00, 0x00,

    /* mov rdi, 0x40001B */
    0x48, 0xC7, 0xC7, 0x1B, 0x00, 0x40, 0x00,

    /* mov rsi, 11 */
    0x48, 0xC7, 0xC6, 0x0B, 0x00, 0x00, 0x00,

    /* int 0x80 */
    0xCD, 0x80,

    /*
     * Deliberately raise #UD.
     *
     * ud2
     */
    0x0F, 0x0B,

    /* Unreachable fallback loop. */
    0xEB, 0xFE,

    /* "[evil] UD2\n" */
    0x5B, 0x65, 0x76, 0x69, 0x6C, 0x5D,
    0x20,
    0x55, 0x44, 0x32,
    0x0A,
};

static const struct user_program malicious_ud2_program = {
    .data = malicious_ud2_program_data,
    .size = sizeof(malicious_ud2_program_data),
};

static const uint8_t malicious_hlt_program_data[] = {
    /*
     * write("[evil] HLT\n", 11)
     *
     * The message begins at 0x40001A:
     *
     *   mov rax, 4      7 bytes
     *   mov rdi, ...    7 bytes
     *   mov rsi, 11     7 bytes
     *   int 0x80        2 bytes
     *   hlt             1 byte
     *   fallback loop   2 bytes
     *                  --------
     *                  26 bytes = 0x1A
     */

    /* mov rax, 4 */
    0x48, 0xC7, 0xC0, 0x04, 0x00, 0x00, 0x00,

    /* mov rdi, 0x40001A */
    0x48, 0xC7, 0xC7, 0x1A, 0x00, 0x40, 0x00,

    /* mov rsi, 11 */
    0x48, 0xC7, 0xC6, 0x0B, 0x00, 0x00, 0x00,

    /* int 0x80 */
    0xCD, 0x80,

    /*
     * Deliberately attempt a privileged instruction from CPL3.
     *
     * hlt
     */
    0xF4,

    /* Unreachable fallback loop. */
    0xEB, 0xFE,

    /* "[evil] HLT\n" */
    0x5B, 0x65, 0x76, 0x69, 0x6C, 0x5D,
    0x20,
    0x48, 0x4C, 0x54,
    0x0A,
};

static const struct user_program malicious_hlt_program = {
    .data = malicious_hlt_program_data,
    .size = sizeof(malicious_hlt_program_data),
};

static const uint8_t malicious_x87_program_data[] = {
    /*
     * write("[evil] x87\n", 11)
     *
     * mov rax, 4
     */
    0x48, 0xC7, 0xC0, 0x04, 0x00, 0x00, 0x00,

    /* mov rdi, 0x40001B */
    0x48, 0xC7, 0xC7, 0x1B, 0x00, 0x40, 0x00,

    /* mov rsi, 11 */
    0x48, 0xC7, 0xC6, 0x0B, 0x00, 0x00, 0x00,

    /* int 0x80 */
    0xCD, 0x80,

    /*
     * fldz
     *
     * This instruction must raise #NM while CR0.TS is set.
     */
    0xD9, 0xEE,

    /* Unreachable fallback loop. */
    0xEB, 0xFE,

    /* "[evil] x87\n" */
    0x5B, 0x65, 0x76, 0x69, 0x6C, 0x5D,
    0x20, 0x78, 0x38, 0x37, 0x0A,
};

static const struct user_program malicious_x87_program = {
    .data = malicious_x87_program_data,
    .size = sizeof(malicious_x87_program_data),
};

static const uint8_t survivor_program_data[] = {
    /*
     * write("SURVIVED\n", 9)
     *
     * The message begins at 0x400025.
     */

    /* mov rax, 4 */
    0x48, 0xC7, 0xC0, 0x04, 0x00, 0x00, 0x00,

    /* mov rdi, 0x400025 */
    0x48, 0xC7, 0xC7, 0x25, 0x00, 0x40, 0x00,

    /* mov rsi, 9 */
    0x48, 0xC7, 0xC6, 0x09, 0x00, 0x00, 0x00,

    /* int 0x80 */
    0xCD, 0x80,

    /* exit(0) */

    /* mov rax, 2 */
    0x48, 0xC7, 0xC0, 0x02, 0x00, 0x00, 0x00,

    /* xor rdi, rdi */
    0x48, 0x31, 0xFF,

    /* int 0x80 */
    0xCD, 0x80,

    /* Unreachable fallback loop. */
    0xEB, 0xFE,

    /* "SURVIVED\n" */
    0x53, 0x55, 0x52, 0x56,
    0x49, 0x56, 0x45, 0x44,
    0x0A,
};

static const struct user_program survivor_program = {
    .data = survivor_program_data,
    .size = sizeof(survivor_program_data),
};

const struct user_program *user_program_hello(void)
{
    return &hello_program;
}

const struct user_program *user_program_counter(void)
{
    return &counter_program;
}

const struct user_program *user_program_malicious_page_fault(void)
{
    return &malicious_page_fault_program;
}

const struct user_program *user_program_malicious_ud2(void)
{
    return &malicious_ud2_program;
}

const struct user_program *user_program_malicious_hlt(void)
{
    return &malicious_hlt_program;
}

const struct user_program *user_program_malicious_x87(void)
{
    return &malicious_x87_program;
}

const struct user_program *user_program_survivor(void)
{
    return &survivor_program;
}
