#include "hardware/clocks.h"
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "IODef.h"
#include "build_time.h"
#include "ds3231.h"
#include "dspi.h"
#include "dspi_cmd.h"
#include "encoder.h"
#include "i2c_bus.h"
#include "nb_protocol.h"
#include "pcf8575.h"
#include "selftest.h"

namespace {

    /* Frame types and payload layouts live in nb_protocol.h, which the Pi side
     * includes too, so neither end can drift from the other. */
    constexpr uint8_t kTypePing = NB_CMD_PING;
    constexpr uint8_t kTypePanel = NB_CMD_PANEL_EVT;
    constexpr uint8_t kTypeLeds = NB_CMD_SET_LEDS;
    constexpr uint8_t kTypeGetPanel = NB_CMD_GET_PANEL;
    constexpr uint8_t kTypeGetTime = NB_CMD_GET_TIME;
    constexpr uint8_t kTypeSetTime = NB_CMD_SET_TIME;
    constexpr uint8_t kTypeGetInfo = NB_CMD_GET_INFO;
    constexpr uint8_t kTypeEncEvt = NB_CMD_ENC_EVT;
    constexpr uint8_t kTypeGetEnc = NB_CMD_GET_ENC;

    /* Set once at startup, reported in the info reply. */
    bool g_rtc_ok = false;
    bool g_panel_ok = false;
    bool g_encoder_ok = false;
    uint16_t g_led_state = 0;

    /* ---- Command handlers. One per frame type, no frame parsing. ---- */

    /* Master sets the panel LEDs. No reply. This is the ONLY thing that lights
     * them once the self test has finished; the firmware never drives them on
     * its own. A command that lands while the test still owns the LEDs is
     * remembered and applied by panel_task the moment the test releases them,
     * rather than blocking the dispatch task for a second. */
    void on_set_leds(const DspiMsg &req, DspiReply &, void *) {
        if (req.len >= 1) {
            g_led_state = (uint16_t)(req.payload[0] & 0x0f);
            printf("leds set by master: %x%s\n", g_led_state, selftest_done() ? "" : " (deferred, selftest running)");
            if (selftest_done()) {
                pcf8575_set_leds((uint16_t)(g_led_state << __builtin_ctz(PCF8575_LED_MASK)));
            }
        }
    }

    /* Master asks for the panel state. */
    void on_get_panel(const DspiMsg &, DspiReply &reply, void *) {
        NbPanelWire panel{};
        panel.buttons = pcf8575_buttons();
        panel.toggles = (uint8_t)pcf8575_toggles();
        panel.leds = (uint8_t)g_led_state;
        reply.write(panel);
    }

    /* Master asks where the encoder currently sits. */
    void on_get_encoder(const DspiMsg &, DspiReply &reply, void *) {
        NbEncoderWire wire{};
        wire.position = encoder_position();
        wire.delta = 0; /* a poll reports state, not movement */
        wire.pressed = encoder_button() ? 1 : 0;
        reply.write(wire);
    }

    /* Master asks for the time, with the RTC die temperature thrown in. */
    void on_get_time(const DspiMsg &, DspiReply &reply, void *) {
        Ds3231Time now{};
        if (ds3231_get_time(now) != 0) {
            return; /* no reply: the master's request times out and retries */
        }

        NbTimeWire wire{};
        wire.year = now.year;
        wire.month = now.month;
        wire.day = now.day;
        wire.hour = now.hour;
        wire.minute = now.minute;
        wire.second = now.second;
        wire.weekday = now.weekday;

        int16_t centi = 0;
        wire.temp_centi = (ds3231_temperature_centi(centi) == 0) ? centi : 0;

        reply.write(wire);
    }

    /* Master sets the RTC. The Pi has NTP; the build stamp we seed from is only
     * ever a floor, so this is how the clock becomes actually correct. */
    void on_set_time(const DspiMsg &req, DspiReply &reply, void *) {
        NbStatusWire status{NB_OK};

        if (req.len < sizeof(NbTimeWire)) {
            status.code = NB_ERR_ARG;
            reply.write(status);
            return;
        }

        NbTimeWire wire{};
        memcpy(&wire, req.payload, sizeof(wire));

        Ds3231Time t{};
        t.year = wire.year;
        t.month = wire.month;
        t.day = wire.day;
        t.hour = wire.hour;
        t.minute = wire.minute;
        t.second = wire.second;

        const int rc = ds3231_set_time(t);
        if (rc == DS3231_ERR_ARG) {
            status.code = NB_ERR_ARG;
        } else if (rc < 0) {
            status.code = g_rtc_ok ? NB_ERR_HARDWARE : NB_ERR_UNSUPPORTED;
        } else {
            printf("rtc set by master: %04u-%02u-%02u %02u:%02u:%02u\n", t.year, t.month, t.day, t.hour, t.minute,
                   t.second);
        }

        reply.write(status);
    }

    /* Master asks what this board is and what it is running. */
    void on_get_info(const DspiMsg &, DspiReply &reply, void *) {
        NbInfoWire info{};
        info.proto_version = NB_PROTO_VERSION;
        info.fw_major = NB_FW_VERSION_MAJOR;
        info.fw_minor = NB_FW_VERSION_MINOR;
        info.fw_patch = NB_FW_VERSION_PATCH;

        const BuildStamp stamp = build_stamp();
        info.build_year = stamp.year;
        info.build_month = stamp.month;
        info.build_day = stamp.day;
        info.build_hour = stamp.hour;
        info.build_minute = stamp.minute;
        info.build_second = stamp.second;

        info.uptime_s = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS / 1000);

        pico_unique_board_id_t id{};
        pico_get_unique_board_id(&id);
        memcpy(&info.board_id, id.id, sizeof(info.board_id));

        info.features = (uint8_t)((g_rtc_ok ? NB_FEAT_RTC : 0) | (g_panel_ok ? NB_FEAT_PANEL : 0) |
                                  (g_encoder_ok ? NB_FEAT_ENCODER : 0) |
                                  (selftest_result().passed ? NB_FEAT_POST_OK : 0));

        reply.write(info);
    }

    /* Round trip check: echo the payload straight back. */
    void on_ping(const DspiMsg &req, DspiReply &reply, void *) { reply.write(req.payload, req.len); }

    void on_unknown(const DspiMsg &req, DspiReply &, void *) {
        printf("dspi: no handler for type 0x%02x len=%u\n", req.type, req.len);
    }

    /* Seeds the RTC from the build stamp if it cannot be trusted, then reports
     * the time on every INT edge (once a second). */
    void rtc_task(void *) {
        if (!ds3231_init()) {
            printf("ds3231 not responding at 0x%02x\n", DS3231_I2C_ADDR);
            vTaskDelete(nullptr);
        }
        g_rtc_ok = true;

        const int seeded = ds3231_sync_to_build_time();
        const Ds3231Time reference = ds3231_build_time();
        printf("ds3231 %s (build reference %04u-%02u-%02u %02u:%02u:%02u)\n",
               seeded > 0 ? "seeded from build time" : (seeded == 0 ? "kept running time" : "sync failed"),
               reference.year, reference.month, reference.day, reference.hour, reference.minute, reference.second);

        TickType_t last_wake = xTaskGetTickCount();
        for (;;) {
            Ds3231Time now{};
            int16_t centi = 0;
            if (ds3231_get_time(now) == 0 && ds3231_temperature_centi(centi) == 0) {
                printf("%04u-%02u-%02u %02u:%02u:%02u dow=%u %d.%02d C\n", now.year, now.month, now.day, now.hour,
                       now.minute, now.second, now.weekday, centi / 100, (centi < 0 ? -centi : centi) % 100);
            }
            vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));
        }
    }

    /* Reports expander pin changes to the main controller. Blocks on the queue,
     * so it costs nothing idle. The LEDs are not touched here: they answer to
     * NB_CMD_SET_LEDS and nothing else. */
    void panel_task(void *) {
        selftest_wait();

        /* Apply whatever the master asked for while the test held the LEDs. */
        pcf8575_set_leds((uint16_t)(g_led_state << __builtin_ctz(PCF8575_LED_MASK)));

        for (;;) {
            Pcf8575Msg msg{};
            if (!pcf8575_wait(msg)) {
                continue;
            }

            const uint16_t bit = (uint16_t)(1u << msg.pin);
            const char *kind = "input";
            if (msg.chip != PCF8575_LED_CHIP) {
                kind = "button";
            } else if (bit & PCF8575_BTN_MASK) {
                kind = "button";
            } else if (bit & PCF8575_TOGGLE_MASK) {
                kind = "toggle";
            } else if (bit & PCF8575_LED_MASK) {
                continue; /* our own LED write reading back */
            }

            printf("%s chip%u p%02u %s (buttons=%06lx toggles=%x)\n", kind, msg.chip, msg.pin,
                   msg.active ? "down" : "up", (unsigned long)pcf8575_buttons(), pcf8575_toggles());

            /* Hand the change to the main controller: chip, pin, state, then
             * the full debounced picture so a dropped frame self-corrects. */
            const uint32_t buttons = pcf8575_buttons();
            const uint8_t packet[8] = {
                msg.chip,
                msg.pin,
                msg.active,
                (uint8_t)(buttons & 0xFF),
                (uint8_t)(buttons >> 8),
                (uint8_t)(buttons >> 16),
                (uint8_t)pcf8575_toggles(),
                0,
            };
            dspi_send(kTypePanel, packet, sizeof(packet));
        }
    }

    /* Reports rotation and button changes, and forwards them to the main
     * controller. Blocks on the queue, so it costs nothing idle. */
    void encoder_task(void *) {
        for (;;) {
            EncoderMsg msg{};
            if (!encoder_wait(msg)) {
                continue;
            }

            if (msg.button_edge) {
                printf("encoder button %s (pos=%ld)\n", msg.pressed ? "down" : "up", (long)msg.position);
            } else {
                printf("encoder %+d (pos=%ld err=%lu)\n", msg.delta, (long)msg.position,
                       (unsigned long)encoder_error_count());
            }

            /* Position rides along with every event, so a dropped frame
             * self-corrects the same way the panel events do. */
            NbEncoderWire wire{};
            wire.position = msg.position;
            wire.delta = msg.delta;
            wire.pressed = msg.pressed ? 1 : 0;
            wire.flags = (uint8_t)((msg.button_edge ? NB_ENC_BUTTON : 0) | (msg.delta != 0 ? NB_ENC_MOVED : 0));
            dspi_send(kTypeEncEvt, &wire, sizeof(wire));
        }
    }

    /* Sweeps the 7-bit address space through the I2C queue every 5 s.
     * Reserved ranges (0x00-0x07, 0x78-0x7f) are skipped.
     *
     * A clean sweep costs a few ms; a stuck bus costs 112 * I2C_XFER_TIMEOUT_US
     * (~1.1 s), still inside the period, so the phase never drifts. */
    void i2c_scan_task(void *) {
        printf("i2c scan on sda=%d scl=%d @ %d Hz\n", IO_SYS_I2C_SDA, IO_SYS_I2C_SCL, IO_SYS_I2C_BAUD);

        TickType_t last_wake = xTaskGetTickCount();

        for (;;) {
            int found = 0;
            for (uint8_t addr = 0x08; addr < 0x78; ++addr) {
                if (i2c_probe(addr)) {
                    printf("  device at 0x%02x\n", addr);
                    ++found;
                }
            }
            printf("[%lu ms] i2c scan done, %d device(s)\n", (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS),
                   found);

            vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(5000));
        }
    }

    /* Unconditional heartbeat, so the console always shows something and the
     * link can be judged from the slave side rather than inferred from the
     * master's stats. */
    void heartbeat_task(void *) {
        TickType_t last_wake = xTaskGetTickCount();

        /* Slave mode needs clk_peri at least 12x the master's SCK. Print it
         * rather than assume the default: it caps the usable clock. */
        const uint32_t peri_hz = clock_get_hz(clk_peri);
        printf("clk_peri=%lu Hz -> max slave SCK %lu Hz\n", (unsigned long)peri_hz, (unsigned long)(peri_hz / 12));

        uint32_t beat = 0;
        for (;;) {
            /* Every 5 s, show what the hardware is actually configured as,
             * rather than what the setup code intended. */
            if (beat++ % 5 == 0) {
                dspi_debug_dump();
            }

            /* idle = valid empty frames: the healthy no-traffic case.
             * zero = frames that arrived all zero, i.e. nothing on MOSI. */
            printf("up=%lus isr=%lu rx=%lu idle=%lu zero=%lu crc=%lu tx=%lu resync=%lu\n",
                   (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS / 1000), (unsigned long)dspi_isr_count(),
                   (unsigned long)dspi_rx_count(), (unsigned long)dspi_idle_rx_count(),
                   (unsigned long)dspi_zero_rx_count(), (unsigned long)dspi_crc_errors(),
                   (unsigned long)dspi_tx_count(), (unsigned long)dspi_resync_count());

            /* Ping the master once a second so its rx counter has to move if
             * the slave-to-master path really works. */
            const uint32_t uptime = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;
            dspi_send(kTypePing, &uptime, sizeof(uptime));

            vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));
        }
    }

    // Toggles the LED at 2 Hz and reports each edge to the logger task.
    void blink_task(void *) {
        selftest_wait(); /* the self test holds this LED solid while it runs */

        gpio_init(IO_LED);
        gpio_set_dir(IO_LED, GPIO_OUT);

        uint32_t count = 0;
        bool led_on = false;
        TickType_t last_wake = xTaskGetTickCount();

        for (;;) {
            led_on = !led_on;
            gpio_put(IO_LED, led_on);

            vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(250));
        }
    }
} // namespace

/* Bringup only: set to 1 to skip the whole application and square-wave the pin
 * that drives the master's MISO line, as a plain GPIO. Proves the pin, the
 * solder joint and the wire with no SPI, DMA or protocol involved.
 *
 * Scope the master's MISO pin: a clean 10 kHz square means the path is good and
 * the fault is in the SPI slave setup. Nothing means the wire or pin is wrong. */
#define DSPI_MISO_PIN_TEST 0

int main() {
    stdio_init_all();

#if DSPI_MISO_PIN_TEST
    gpio_init(IO_LED);
    gpio_set_dir(IO_LED, GPIO_OUT);
    gpio_init(DSPI_PIN_SLAVE_OUT);
    gpio_set_dir(DSPI_PIN_SLAVE_OUT, GPIO_OUT);

    bool level = false;
    uint32_t ticks = 0;
    for (;;) {
        level = !level;
        gpio_put(DSPI_PIN_SLAVE_OUT, level);
        sleep_us(50); /* 10 kHz square wave */

        /* Blink the LED and talk every second, so "is it even running" is
         * answered without a serial terminal. */
        if (++ticks % 10000 == 0) {
            gpio_xor_mask(1u << IO_LED);
            printf("pin test: toggling gpio %d\n", DSPI_PIN_SLAVE_OUT);
        }
    }
#endif

    // Announce life before anything can assert, so a silent console means the
    // firmware never started rather than a subsystem refusing to come up.
    printf("northbridge up: dspi cs=%d sck=%d slave_out=%d slave_in=%d irq=%d\n", IO_DSPI_CS, IO_DSPI_SCK,
           DSPI_PIN_SLAVE_OUT, DSPI_PIN_SLAVE_IN, IO_DSPI_IRQ);

    // Worker sits above the tasks that talk to it, so a queued command runs as
    // soon as it is posted instead of waiting for the producer to block.
    hard_assert(i2c_bus_init(tskIDLE_PRIORITY + 3));

    // Panel sits above the periodic tasks: a keypress should not wait behind a
    // bus scan or an RTC read.
    g_panel_ok = pcf8575_init(tskIDLE_PRIORITY + 2);
    hard_assert(g_panel_ok);

    // Same reasoning for the encoder: a detent that waits behind a bus scan is
    // a detent the user feels as a missed click.
    g_encoder_ok = encoder_init(tskIDLE_PRIORITY + 2);
    hard_assert(g_encoder_ok);

    // Link task sits high too: the master is waiting on the other end of every
    // frame it clocks.
    hard_assert(dspi_init(tskIDLE_PRIORITY + 3));

    // Runs once, before anything else drives the LEDs or the buzzer, and every
    // task that shares that hardware waits on selftest_wait() first.
    hard_assert(selftest_start(tskIDLE_PRIORITY + 2));

    xTaskCreate(blink_task, "blink", configMINIMAL_STACK_SIZE, nullptr, tskIDLE_PRIORITY + 2, nullptr);
    // xTaskCreate(i2c_scan_task, "i2cscan", configMINIMAL_STACK_SIZE * 2, nullptr, tskIDLE_PRIORITY + 1, nullptr);
    xTaskCreate(rtc_task, "rtc", configMINIMAL_STACK_SIZE * 2, nullptr, tskIDLE_PRIORITY + 1, nullptr);
    xTaskCreate(panel_task, "panel", configMINIMAL_STACK_SIZE * 2, nullptr, tskIDLE_PRIORITY + 1, nullptr);
    xTaskCreate(encoder_task, "encevt", configMINIMAL_STACK_SIZE * 2, nullptr, tskIDLE_PRIORITY + 1, nullptr);
    // Command dispatch. Handlers run on this task, so they may block and may
    // use the I2C bus; keep them under whatever the master waits for.
    hard_assert(dspi_cmd_init(tskIDLE_PRIORITY + 2));
    dspi_on(kTypePing, on_ping);
    dspi_on(kTypeLeds, on_set_leds);
    dspi_on(kTypeGetPanel, on_get_panel);
    dspi_on(kTypeGetTime, on_get_time);
    dspi_on(kTypeSetTime, on_set_time);
    dspi_on(kTypeGetInfo, on_get_info);
    dspi_on(kTypeGetEnc, on_get_encoder);
    dspi_on_unknown(on_unknown);

    xTaskCreate(heartbeat_task, "beat", configMINIMAL_STACK_SIZE * 2, nullptr, tskIDLE_PRIORITY + 1, nullptr);

    // Never returns unless the heap is too small for the idle/timer tasks.
    vTaskStartScheduler();

    panic("scheduler exited");
}

extern "C" {
void vApplicationMallocFailedHook(void) { panic("FreeRTOS malloc failed"); }

void vApplicationStackOverflowHook(TaskHandle_t task, char *name) {
    (void)task;
    panic("FreeRTOS stack overflow in task %s", name);
}
} // extern "C"
