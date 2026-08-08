#pragma once

#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

/* PCF8575 16-bit I2C expanders, driven through the i2c_bus queue.
 *
 * The chips share one open-drain INT line on IO_PCF8575_IRQ, asserted low
 * whenever an input changes and released by reading the port. A falling edge
 * wakes the poll task, which reads every chip, debounces, and posts one event
 * per changed pin.
 *
 * The parts are quasi-bidirectional: a pin used as an input must be left
 * written high (weak ~100uA pull-up), and an output can only sink current
 * usefully. So LEDs are wired to 3V3 and light when their bit is driven LOW.
 * The driver keeps an output shadow per chip and always writes 1s to the input
 * bits, so writing an LED can never break the inputs. */

#define PCF8575_CHIP_COUNT 2

/* Sampling period. A pin must read the same on two consecutive samples before
 * it counts, so this is also the debounce time.
 *
 * The task polls at this rate whether or not INT fired. That is deliberate:
 * the chip clears INT on every read, so a second switch closing during a read
 * can leave its pin stably low with no edge left to announce it. Polling finds
 * those; INT only shortens the latency of the first one. */
#define PCF8575_POLL_MS 10

struct Pcf8575Msg {
    uint8_t chip;  /* index into the address table, not the I2C address */
    uint8_t pin;   /* 0-15, P0 first */
    bool active;   /* true = pin pulled low, i.e. switch closed */
    uint16_t port; /* full debounced port word at the time of the change */
};

/* Configures the INT pin, its IRQ and the poll task. Safe to call before the
 * scheduler starts: the first port read happens inside the task, since the I2C
 * worker cannot run any earlier. Repeat calls are no-ops that return true. */
bool pcf8575_init(UBaseType_t task_priority, UBaseType_t queue_depth = 16);

/* Blocks until the next pin change. False on timeout or before init. */
bool pcf8575_wait(Pcf8575Msg &out, TickType_t wait = portMAX_DELAY);

QueueHandle_t pcf8575_event_queue(void);

/* Last debounced port word for a chip. Bit set = pin high = switch open. */
uint16_t pcf8575_inputs(uint8_t chip);

/* Convenience readers using the masks in IODef.h. Bit set = pressed / on.
 * Buttons are packed as chip 0's 16 pins in bits 0-15, then the button pins of
 * the other chip above them: 24 buttons in one word. */
uint32_t pcf8575_buttons(void);
uint16_t pcf8575_toggles(void);

/* LED control on the chip carrying PCF8575_LED_MASK. `state` is a bitmask in
 * the same bit positions as PCF8575_LED_MASK; a set bit lights the LED. */
int pcf8575_set_leds(uint16_t state, TickType_t wait = pdMS_TO_TICKS(100));
int pcf8575_led(uint8_t index, bool on, TickType_t wait = pdMS_TO_TICKS(100));

/* Raw port access, for anything the helpers above do not cover. */
int pcf8575_read_port(uint8_t addr, uint16_t &value, TickType_t wait = pdMS_TO_TICKS(100));
int pcf8575_write_port(uint8_t addr, uint16_t value, TickType_t wait = pdMS_TO_TICKS(100));

/* INT edges seen since boot, and changes the poll found without INT firing.
 * A few of the latter are normal (overlapping presses race the read). If it is
 * the only counter moving, the INT line is not working. */
uint32_t pcf8575_irq_count(void);
uint32_t pcf8575_missed_count(void);
