#pragma once

#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

/* Buzzer on IO_BUZ, driven by the RP2040 PWM at 50% duty.
 *
 * A passive buzzer needs the carrier, so the tone is the PWM frequency itself.
 * An active (self-oscillating) buzzer ignores the frequency and simply sounds
 * while the pin is driven, so the same calls work on either part; only the
 * pitch is lost.
 *
 * The blocking calls use vTaskDelay once the scheduler is running and busy
 * sleeps before that, so they are safe from main() as well as from a task. */

/* PWM duty while a tone plays, in percent. 50% is the loudest for a passive
 * buzzer; drop it to trade volume for current. */
#define BUZZER_DUTY_PCT 50

struct BuzzerNote {
    uint16_t freq_hz; /* 0 = rest, i.e. silence for the duration */
    uint16_t ms;
};

/* Claims the pin and its PWM slice. Repeat calls are no-ops that return true. */
bool buzzer_init(void);

/* Starts / stops a continuous tone. freq_hz of 0 is the same as buzzer_off(). */
void buzzer_on(uint32_t freq_hz);
void buzzer_off(void);

/* Sounds one tone and returns when it has finished. */
void buzzer_tone(uint32_t freq_hz, uint32_t ms);

/* Plays a sequence, silencing the pin at the end. */
void buzzer_play(const BuzzerNote *notes, size_t count);
