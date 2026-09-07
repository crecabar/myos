// SPDX-License-Identifier: GPL-2.0-only

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#include <limine.h>

#include "arch/x86_64/serial.h"
#include "arch/x86_64/idt.h"
#include "arch/x86_64/paging.h"
#include "drivers/framebuffer.h"
#include "console/console.h"
#include "diagnostics/diagnostics.h"

extern char __kernel_start[];
extern char __kernel_end[];

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] =
    LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] =
    LIMINE_BASE_REVISION(6);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0,
    .response = NULL,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0,
    .response = NULL,
};

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] =
    LIMINE_REQUESTS_END_MARKER;

static _Noreturn void kernel_panic(
    const char *format,
    ...
);

static _Noreturn void kernel_halt(void)
{
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

static _Noreturn void kernel_panic(
    const char *format,
    ...)
{
    va_list arguments;

    diagnostics_write(
        "\n*** KERNEL PANIC ***\n"
    );

    va_start(arguments, format);

    diagnostics_vprintf(
        format,
        arguments
    );

    va_end(arguments);

    diagnostics_write("\n");

    kernel_halt();
}

_Noreturn void kernel_main(void)
{
    /*
     * Phase 1: Early diagnostics.
     *
     * Serial is our only reliable output channel at this point.
     */
    serial_init();
    diagnostics_init();

    diagnostics_write("MyOS is alive!\n");

    /*
     * Phase 2: Validate bootloader environment.
     */
    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        kernel_panic(
            "Unsupported Limine base revision"
        );
    }

    if (framebuffer_request.response == NULL) {
        kernel_panic(
            "Framebuffer unavailable"
        );
    }

    if (hhdm_request.response == NULL) {
        kernel_panic(
            "HHDM unavailable"
        );
    }

    uint64_t hhdm_offset = hhdm_request.response->offset;

    struct limine_framebuffer_response *response =
        framebuffer_request.response;

    if (response->framebuffer_count == 0) {
        kernel_panic(
            "No framebuffer entries"
        );
    }

    if (response->framebuffers[0] == NULL) {
        kernel_panic(
            "Framebuffer entry missing"
        );
    }

    struct limine_framebuffer *framebuffer =
        response->framebuffers[0];

    if (framebuffer->bpp != 32) {
        kernel_panic(
            "Unsupported framebuffer bpp"
        );
    }

    if (framebuffer->red_mask_size != 8 ||
        framebuffer->green_mask_size != 8 ||
        framebuffer->blue_mask_size != 8) {
        kernel_panic(
            "Unsupported framebuffer color layout"
        );
    }

    diagnostics_write("Framebuffer ready\n");

    /*
     * Phase 3: Initialize graphical diagnostics.
     */
    uint32_t red = framebuffer_make_color(framebuffer, 255, 0, 0);
    uint32_t green = framebuffer_make_color(framebuffer, 0, 255, 0);
    uint32_t blue = framebuffer_make_color(framebuffer, 0, 0, 255);
    uint32_t white = framebuffer_make_color(framebuffer, 255, 255, 255);

    framebuffer_fill_rect(
        framebuffer,
        0,
        0,
        100,
        100,
        red
    );

    framebuffer_fill_rect(
        framebuffer,
        100,
        0,
        100,
        100,
        green
    );

    framebuffer_fill_rect(
        framebuffer,
        200,
        0,
        100,
        100,
        blue
    );

    struct console console;

    console_init(
        &console,
        framebuffer,
        20,
        120,
        white,
        2
    );

    diagnostics_attach_console(&console);
    diagnostics_write("MyOS v0.0\n\n");

    /*
     * Phase 4: Initialize CPU architecture facilities.
     *
     * From here on, exceptions handled by our IDT can also
     * report through the graphical console.
     */
    idt_init();

    diagnostics_write("IDT initialized\n");

    /*
     * Phase 5: Runtime diagnostics / temporary tests.
     */
    uintptr_t kernel_start = (uintptr_t) __kernel_start;
    uintptr_t kernel_end = (uintptr_t) __kernel_end;
    uint64_t kernel_size = (uint64_t) (kernel_end - kernel_start);

    diagnostics_printf(
        "Kernel start=%x\n"
        "Kernel end=%x\n"
        "Kernel size=%u bytes\n",
        (uint64_t) kernel_start,
        (uint64_t) kernel_end,
        kernel_size
    );

    uint16_t cs;

    __asm__ volatile (
        "mov %%cs, %0"
        : "=r"(cs)
    );

    diagnostics_printf(
        "CS=%x\n",
        (uint64_t) cs
    );

    struct paging_translation translation;
    uint64_t virtual_address = (uint64_t) kernel_start;

    if (!paging_translate(virtual_address, hhdm_offset, &translation)) {
        diagnostics_write("Unable to translate kernel address\n");
        kernel_halt();
    }

    diagnostics_printf(
        "Paging translation:\n"
        "  VA=%x\n"
        "  PA=%x\n"
        "  PML4E=%x\n"
        "  PDPTE=%x\n"
        "  PDE=%x\n"
        "  PTE=%x\n",
        virtual_address,
        translation.physical_address,
        translation.pml4_entry,
        translation.pdpt_entry,
        translation.pd_entry,
        translation.pt_entry
    );

    kernel_halt();
}
