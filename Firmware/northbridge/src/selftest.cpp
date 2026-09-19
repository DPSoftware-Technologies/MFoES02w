#include "selftest.h"

#include <stdio.h>

#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include "IODef.h"
#include "buzzer.h"
#include "ds3231.h"
#include "encoder.h"
#include "i2c_bus.h"
#include "pcf8575.h"

namespace {

    const uint8_t kAddr[PCF8575_CHIP_COUNT] = {PCF8575_0_ADDR, PCF8575_1_ADDR};
    constexpr uint8_t kLedAddr = (PCF8575_LED_CHIP == 0) ? PCF8575_0_ADDR : PCF8575_1_ADDR;
    constexpr unsigned kLedCount = __builtin_popcount(PCF8575_LED_MASK);

    /* Two rising notes for a pass, one long low note for a fail: audible from
     * the bench with no console attached. */
    const BuzzerNote kPassTune[] = {{1200, 90}, {0, 40}, {1800, 140}};
    const BuzzerNote kFailTune[] = {{300, 500}};

    SelftestResult g_result{};
    volatile bool g_done = false;

    /* Lights `state` (bit positions of PCF8575_LED_MASK), waits, then reports
     * whether the port read back matches what was driven. Which level that is
     * depends on PCF8575_LED_ACTIVE_LOW. */
    bool leds_show(uint16_t state, uint32_t hold_ms) {
        if (pcf8575_set_leds(state) != 0) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(hold_ms));

        uint16_t port = 0;
        if (pcf8575_read_port(kLedAddr, port) != 0) {
            return false;
        }

        const uint16_t lit = (uint16_t)(state & PCF8575_LED_MASK);
        const uint16_t expect = PCF8575_LED_ACTIVE_LOW ? (uint16_t)(~lit & PCF8575_LED_MASK) : lit;
        const uint16_t got = (uint16_t)(port & PCF8575_LED_MASK);
        if (got != expect) {
            printf("selftest: leds   drove %04x, port reads %04x\n", expect, got);
            return false;
        }
        return true;
    }

    /* Every LED on together for the hold, then all off. No walk: one glance
     * answers "are they alive", and the readback catches the channel that did
     * not drive even when the eye cannot tell four LEDs from three. */
    bool led_test(void) {
        bool ok = leds_show(PCF8575_LED_MASK, SELFTEST_LED_HOLD_MS);
        ok = leds_show(0, 20) && ok;
        return ok;
    }

    void selftest_task(void *) {
        /* Board LED solid for the duration, so the test is visible even if the
         * panel is unpopulated. blink_task takes the pin back afterwards. */
        gpio_init(IO_LED);
        gpio_set_dir(IO_LED, GPIO_OUT);
        gpio_put(IO_LED, 1);

        buzzer_init();

        printf("selftest: start\n");

        /* --- I2C devices. A probe is a real 1-byte read, so an ack proves the
         * worker, the pins and the part all work. --- */
        g_result.rtc = i2c_probe(DS3231_I2C_ADDR);
        printf("selftest: ds3231  @0x%02x %s\n", DS3231_I2C_ADDR, g_result.rtc ? "ok" : "NO ACK");

        for (uint8_t chip = 0; chip < PCF8575_CHIP_COUNT; ++chip) {
            g_result.panel[chip] = i2c_probe(kAddr[chip]);
            printf("selftest: pcf8575 @0x%02x %s\n", kAddr[chip], g_result.panel[chip] ? "ok" : "NO ACK");
        }

        /* Something answering is the only proof the bus itself is not held
         * low; a dead bus nacks everything. */
        g_result.i2c_bus = g_result.rtc || g_result.panel[0] || g_result.panel[PCF8575_LED_CHIP];

        /* --- Panel LEDs. --- */
        if (g_result.panel[PCF8575_LED_CHIP]) {
            printf("selftest: leds   %u on chip%u, all on for %u ms (active %s)\n", kLedCount,
                   (unsigned)PCF8575_LED_CHIP, (unsigned)SELFTEST_LED_HOLD_MS, PCF8575_LED_ACTIVE_LOW ? "low" : "high");
            g_result.leds = led_test();
            printf("selftest: leds   %s\n", g_result.leds ? "ok" : "READBACK MISMATCH");
        } else {
            printf("selftest: leds   skipped, expander absent\n");
        }

        /* --- Encoder. The quadrature lines rest wherever the detent left
         * them, so only the button can be judged: held low at boot means a
         * shorted switch or a missing pull-up. --- */
        g_result.encoder = !encoder_button();
        printf("selftest: encoder a=%u b=%u btn=%s %s\n", (unsigned)encoder_line_a(), (unsigned)encoder_line_b(),
               encoder_button() ? "down" : "up", g_result.encoder ? "ok" : "BUTTON STUCK");

        /* Whatever the inputs read right now. A button stuck low here is a
         * button the main controller sees as pressed, and it may well answer by
         * lighting the LEDs. */
        printf("selftest: inputs  buttons=%06lx toggles=%x chip0=%04x chip1=%04x\n",
               (unsigned long)pcf8575_buttons(), pcf8575_toggles(), pcf8575_inputs(0), pcf8575_inputs(1));

        g_result.passed = g_result.i2c_bus && g_result.rtc && g_result.panel[0] &&
                          g_result.panel[PCF8575_LED_CHIP] && g_result.leds && g_result.encoder;

        printf("selftest: %s\n", g_result.passed ? "PASS" : "FAIL");

        if (g_result.passed) {
            buzzer_play(kPassTune, count_of(kPassTune));
        } else {
            buzzer_play(kFailTune, count_of(kFailTune));
        }

        gpio_put(IO_LED, 0);
        g_done = true; /* last: waiters must not run before the LEDs are free */

        vTaskDelete(nullptr);
    }

} // namespace

bool selftest_start(UBaseType_t task_priority) {
    return xTaskCreate(selftest_task, "selftest", configMINIMAL_STACK_SIZE * 2, nullptr, task_priority, nullptr) ==
           pdPASS;
}

bool selftest_done(void) { return g_done; }

bool selftest_wait(TickType_t wait) {
    const TickType_t start = xTaskGetTickCount();
    while (!g_done) {
        if (wait != portMAX_DELAY && (xTaskGetTickCount() - start) >= wait) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return true;
}

const SelftestResult &selftest_result(void) { return g_result; }
