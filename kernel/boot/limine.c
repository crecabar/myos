// SPDX-License-Identifier: GPL-2.0-only

#include "boot.h"
#include "../core/panic.h"

#include <stddef.h>
#include <stdint.h>

#include <limine.h>

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

void boot_init(
    struct boot_info *boot_info)
{
    if (boot_info == NULL) {
        kernel_panic(
            "boot_init received NULL boot_info"
        );
    }

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

    struct limine_framebuffer_response *framebuffer_response =
        framebuffer_request.response;

    if (framebuffer_response->framebuffer_count == 0) {
        kernel_panic(
            "No framebuffer entries"
        );
    }

    if (framebuffer_response->framebuffers[0] == NULL) {
        kernel_panic(
            "Framebuffer entry missing"
        );
    }

    struct limine_framebuffer *limine_framebuffer =
        framebuffer_response->framebuffers[0];

    if (limine_framebuffer->bpp != 32) {
        kernel_panic(
            "Unsupported framebuffer bpp"
        );
    }

    if (limine_framebuffer->red_mask_size != 8 ||
        limine_framebuffer->green_mask_size != 8 ||
        limine_framebuffer->blue_mask_size != 8)
    {
        kernel_panic(
            "Unsupported framebuffer color layout"
        );
    }

    boot_info->framebuffer.address = limine_framebuffer->address;
    boot_info->framebuffer.width = limine_framebuffer->width;
    boot_info->framebuffer.height = limine_framebuffer->height;
    boot_info->framebuffer.pitch = limine_framebuffer->pitch;
    boot_info->framebuffer.bpp = limine_framebuffer->bpp;
    boot_info->framebuffer.red_mask_size = limine_framebuffer->red_mask_size;
    boot_info->framebuffer.red_mask_shift = limine_framebuffer->red_mask_shift;
    boot_info->framebuffer.green_mask_size = limine_framebuffer->green_mask_size;
    boot_info->framebuffer.green_mask_shift = limine_framebuffer->green_mask_shift;
    boot_info->framebuffer.blue_mask_size = limine_framebuffer->blue_mask_size;
    boot_info->framebuffer.blue_mask_shift = limine_framebuffer->blue_mask_shift;
    boot_info->direct_map_offset = hhdm_request.response->offset;
}
