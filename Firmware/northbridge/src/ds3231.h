#pragma once

#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

/* DS3231 / DS3231M real-time clock, driven through the i2c_bus queue.
 *
 * Every call is a blocking queued transaction, so any task may use this and
 * transfers stay serialised against other devices on the bus. i2c_bus_init()
 * must have run first.
 *
 * The chip is always driven in 24-hour mode; a device left in 12-hour mode by
 * other firmware is still read correctly. */

#define DS3231_I2C_ADDR 0x68

/* Negative returns. Non-negative means success; I2C errors from i2c_bus.h
 * (-10 and below) pass through unchanged. */
enum Ds3231Error : int {
    DS3231_ERR_BAD_TIME = -30, /* registers held a nonsense date */
    DS3231_ERR_ARG = -31,      /* caller passed an out-of-range field */
};

struct Ds3231Time {
    uint16_t year; /* full year, 2000-2199 */
    uint8_t month; /* 1-12 */
    uint8_t day;   /* 1-31 */
    uint8_t hour;  /* 0-23 */
    uint8_t minute;
    uint8_t second;
    uint8_t weekday; /* 1-7, 1 = Sunday; derived, not trusted from the chip */
};

/* Probes the chip and enables the oscillator on battery (clears EOSC).
 * Returns false if the device does not answer. */
bool ds3231_init(TickType_t wait = pdMS_TO_TICKS(100));

int ds3231_get_time(Ds3231Time &out, TickType_t wait = pdMS_TO_TICKS(100));

/* Weekday is recomputed from the date; the caller's value is ignored. */
int ds3231_set_time(const Ds3231Time &t, TickType_t wait = pdMS_TO_TICKS(100));

/* Oscillator Stop Flag: true once the chip has lost both VCC and battery, i.e.
 * the time it reports is meaningless. Stays set until cleared. */
int ds3231_lost_power(bool &lost, TickType_t wait = pdMS_TO_TICKS(100));
int ds3231_clear_lost_power(TickType_t wait = pdMS_TO_TICKS(100));

/* Internal temperature in hundredths of a degree C (0.25 C resolution).
 * Signed: -025 means -0.25 C. */
int ds3231_temperature_centi(int16_t &centi, TickType_t wait = pdMS_TO_TICKS(100));

/* ---------------- Build-time reference ---------------- */

/* The build stamp as a Ds3231Time (weekday filled in). */
Ds3231Time ds3231_build_time(void);

/* Lexicographic compare: -1 if a is earlier, 0 if equal, 1 if a is later. */
int ds3231_compare(const Ds3231Time &a, const Ds3231Time &b);

/* Seeds the RTC from the build stamp when it cannot be trusted: the oscillator
 * stopped, the registers are unreadable/nonsense, or the RTC reads EARLIER than
 * the firmware was compiled (which no correctly set clock ever does).
 *
 * Returns 1 if the RTC was written, 0 if the running time was kept, negative on
 * I2C failure. `force` writes unconditionally.
 *
 * Accuracy ceiling: the stamp is the compiler's local time, so a freshly seeded
 * clock is late by however long build + flash took. Good enough to order log
 * entries; set the RTC properly when a real time source shows up. */
int ds3231_sync_to_build_time(bool force = false, TickType_t wait = pdMS_TO_TICKS(100));

/* 1 = Sunday. Exposed because set_time uses it. */
uint8_t ds3231_weekday(uint16_t year, uint8_t month, uint8_t day);
