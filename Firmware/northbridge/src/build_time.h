#pragma once

#include <stdint.h>

/* Compile time of build_time.cpp, parsed from __DATE__ / __TIME__.
 *
 * This is the build machine's LOCAL time, and it is only ever a floor: the
 * board is flashed some seconds or minutes after the compiler ran. Use it to
 * seed an RTC that has never been set, not as a clock source.
 *
 * CMake regenerates the stamp on every build (cmake/build_stamp.cmake), so it
 * cannot go stale behind an incremental build. Compiled standalone, it falls
 * back to __DATE__ / __TIME__ of this file. */
struct BuildStamp {
    uint16_t year; /* full year, e.g. 2026 */
    uint8_t month; /* 1-12 */
    uint8_t day;   /* 1-31 */
    uint8_t hour;  /* 0-23 */
    uint8_t minute;
    uint8_t second;
};

BuildStamp build_stamp(void);
