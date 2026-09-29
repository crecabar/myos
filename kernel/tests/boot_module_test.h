// SPDX-License-Identifier: GPL-2.0-only

#ifndef MYOS_TESTS_BOOT_MODULE_TEST_H
#define MYOS_TESTS_BOOT_MODULE_TEST_H

#include "../boot/boot.h"

void boot_module_test_capture_before_reclaim(
    const struct boot_info *boot_info
);

void boot_module_test_run(
    const struct boot_info *boot_info
);

#endif
