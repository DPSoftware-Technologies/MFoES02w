#pragma once

#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#include "pcf8575.h"

/* Power-on self test.
 *
 * Runs once at boot as its own task, because everything it touches goes
 * through the I2C worker and that only runs under the scheduler. It probes the
 * devices that must be on the bus, lights every panel LED at once for
 * SELFTEST_LED_HOLD_MS and turns them all off again, reads the port back to
 * catch a channel that did not drive, checks the encoder lines, then chirps
 * the buzzer: two rising notes for a pass, one low buzz for a fail.
 *
 * After it finishes the LEDs belong to the main controller: nothing in the
 * firmware drives them again except NB_CMD_SET_LEDS. Tasks that share the
 * hardware call selftest_wait() before their loop, so nothing writes the LEDs
 * underneath the test. */

/* How long every LED stays lit together. */
#define SELFTEST_LED_HOLD_MS 1000

struct SelftestResult {
    bool i2c_bus;                       /* the worker took a transfer at all */
    bool rtc;                           /* DS3231 acknowledged */
    bool panel[PCF8575_CHIP_COUNT];     /* each expander acknowledged */
    bool leds;                          /* LED pins read back as driven */
    bool encoder;                       /* lines idle high, button not stuck */
    bool passed;                        /* every check above */
};

/* Creates the self test task. Call before vTaskStartScheduler(). */
bool selftest_start(UBaseType_t task_priority);

/* False until the test has finished. */
bool selftest_done(void);

/* Blocks the caller until the test finishes, or until `wait` elapses. Returns
 * selftest_done(). Polls, rather than adding a kernel object for something
 * that happens once per boot. */
bool selftest_wait(TickType_t wait = portMAX_DELAY);

/* Valid once selftest_done() is true; all-false before that. */
const SelftestResult &selftest_result(void);
