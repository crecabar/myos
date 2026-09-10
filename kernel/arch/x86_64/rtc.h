// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file rtc.h
 * @brief x86-64 CMOS real-time clock access.
 *
 * Provides read-only access to the platform real-time clock used during early
 * MyOS initialization.
 */

#ifndef MYOS_ARCH_X86_64_RTC_H
#define MYOS_ARCH_X86_64_RTC_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Represents a wall-clock time read from the hardware RTC.
 */
struct rtc_time {
    uint8_t hours;
    uint8_t minutes;
    uint8_t seconds;
};

/**
 * Reads a stable time value from the hardware real-time clock.
 *
 * The CMOS RTC may update its registers while they are being read. This
 * function waits until no update is in progress and verifies two consecutive
 * samples before returning.
 *
 * Binary and BCD RTC formats as well as 12-hour and 24-hour modes are handled.
 *
 * MyOS currently interprets the RTC value as UTC and performs no timezone
 * conversion.
 *
 * @param time Structure that receives the current hardware time.
 *
 * @return true when a stable RTC value was obtained; false otherwise.
 */
bool rtc_read_time(struct rtc_time *time);

#endif
