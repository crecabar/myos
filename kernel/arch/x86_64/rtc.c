// SPDX-License-Identifier: GPL-2.0-only

#include "rtc.h"

#include <stddef.h>
#include <stdint.h>

#define CMOS_ADDRESS_PORT 0x70
#define CMOS_DATA_PORT    0x71

#define CMOS_REGISTER_SECONDS  0x00
#define CMOS_REGISTER_MINUTES  0x02
#define CMOS_REGISTER_HOURS    0x04
#define CMOS_REGISTER_STATUS_A 0x0A
#define CMOS_REGISTER_STATUS_B 0x0B

#define CMOS_STATUS_A_UPDATE_IN_PROGRESS (1U << 7)
#define CMOS_STATUS_B_24_HOUR            (1U << 1)
#define CMOS_STATUS_B_BINARY             (1U << 2)

#define CMOS_HOUR_PM (1U << 7)

static void rtc_out8(uint16_t port, uint8_t value);
static uint8_t rtc_in8(uint16_t port);
static uint8_t rtc_read_register(uint8_t reg);
static bool rtc_update_in_progress(void);
static uint8_t rtc_bcd_to_binary(uint8_t value);
static void rtc_read_raw(struct rtc_time *time);
static bool rtc_time_equal(const struct rtc_time *left, const struct rtc_time *right);

static void rtc_out8(uint16_t port, uint8_t value)
{
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

static uint8_t rtc_in8(uint16_t port)
{
    uint8_t value;

    __asm__ volatile (
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

static uint8_t rtc_read_register(uint8_t reg)
{
    rtc_out8(CMOS_ADDRESS_PORT, reg);

    return rtc_in8(CMOS_DATA_PORT);
}

static bool rtc_update_in_progress(void)
{
    return (rtc_read_register(CMOS_REGISTER_STATUS_A) &
            CMOS_STATUS_A_UPDATE_IN_PROGRESS) != 0;
}

static uint8_t rtc_bcd_to_binary(uint8_t value)
{
    return (uint8_t) ((value & 0x0F) + ((value >> 4) * 10));
}

static void rtc_read_raw(struct rtc_time *time)
{
    time->seconds = rtc_read_register(CMOS_REGISTER_SECONDS);
    time->minutes = rtc_read_register(CMOS_REGISTER_MINUTES);
    time->hours = rtc_read_register(CMOS_REGISTER_HOURS);
}

static bool rtc_time_equal(
    const struct rtc_time *left,
    const struct rtc_time *right)
{
    return left->hours == right->hours &&
           left->minutes == right->minutes &&
           left->seconds == right->seconds;
}

bool rtc_read_time(struct rtc_time *time)
{
    if (time == NULL) return false;

    struct rtc_time first;
    struct rtc_time second;

    do {
        while (rtc_update_in_progress()) {
        }

        rtc_read_raw(&first);

        while (rtc_update_in_progress()) {
        }

        rtc_read_raw(&second);
    } while (!rtc_time_equal(&first, &second));

    uint8_t status_b = rtc_read_register(CMOS_REGISTER_STATUS_B);

    bool binary_mode = (status_b & CMOS_STATUS_B_BINARY) != 0;
    bool hour_24_mode = (status_b & CMOS_STATUS_B_24_HOUR) != 0;
    bool pm = (second.hours & CMOS_HOUR_PM) != 0;

    second.hours &= (uint8_t) ~CMOS_HOUR_PM;

    if (!binary_mode) {
        second.seconds = rtc_bcd_to_binary(second.seconds);
        second.minutes = rtc_bcd_to_binary(second.minutes);
        second.hours = rtc_bcd_to_binary(second.hours);
    }

    if (!hour_24_mode) {
        if (pm) {
            second.hours = (uint8_t) ((second.hours % 12) + 12);
        } else if (second.hours == 12) {
            second.hours = 0;
        }
    }

    *time = second;

    return true;
}
