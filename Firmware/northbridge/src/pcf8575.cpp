#include "pcf8575.h"

#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "pico/binary_info.h"

#include "IODef.h"
#include "i2c_bus.h"

bi_decl(bi_1pin_with_name(IO_PCF8575_IRQ, "PCF8575 INT"));

namespace {

    const uint8_t kAddr[PCF8575_CHIP_COUNT] = {PCF8575_0_ADDR, PCF8575_1_ADDR};

    QueueHandle_t g_events = nullptr;
    TaskHandle_t g_task = nullptr;

    uint16_t g_stable[PCF8575_CHIP_COUNT]; /* accepted state, 1 = pin high */
    uint16_t g_prev[PCF8575_CHIP_COUNT];   /* previous raw sample, for debounce */
    uint16_t g_shadow[PCF8575_CHIP_COUNT]; /* what we last wrote; 1 = released */
    volatile uint32_t g_irq_count = 0;
    uint32_t g_missed = 0;

    void pcf8575_isr(void) {
        const uint32_t events = gpio_get_irq_event_mask(IO_PCF8575_IRQ);
        if ((events & GPIO_IRQ_EDGE_FALL) == 0) {
            return; /* shared bank IRQ: this edge belongs to another pin */
        }
        gpio_acknowledge_irq(IO_PCF8575_IRQ, GPIO_IRQ_EDGE_FALL);
        ++g_irq_count;

        /* INT stays low until the port is read, so bounce cannot produce more
         * than one edge per read cycle. No need to mask the pin. */
        BaseType_t higher_woken = pdFALSE;
        vTaskNotifyGiveFromISR(g_task, &higher_woken);
        portYIELD_FROM_ISR(higher_woken);
    }

    void emit(uint8_t chip, uint8_t pin, bool active, uint16_t port) {
        const Pcf8575Msg msg{chip, pin, active, port};
        xQueueSend(g_events, &msg, 0); /* drop rather than stall the poll task */
    }

    /* One sample of every chip. A bit is accepted once two consecutive samples
     * agree on it; bits still bouncing keep their old value and are retried on
     * the next poll, so nothing depends on another INT edge arriving.
     * Returns true if anything changed. */
    bool scan_and_emit(void) {
        bool changed = false;

        for (uint8_t chip = 0; chip < PCF8575_CHIP_COUNT; ++chip) {
            uint16_t raw = 0;
            if (pcf8575_read_port(kAddr[chip], raw) != 0) {
                continue;
            }

            const uint16_t agreed = (uint16_t)~(raw ^ g_prev[chip]);
            g_prev[chip] = raw;

            const uint16_t diff = (uint16_t)((g_stable[chip] ^ raw) & agreed);
            if (diff == 0) {
                continue;
            }

            g_stable[chip] = (uint16_t)((g_stable[chip] & ~agreed) | (raw & agreed));
            changed = true;

            for (uint8_t pin = 0; pin < 16; ++pin) {
                if (diff & (1u << pin)) {
                    emit(chip, pin, (g_stable[chip] & (1u << pin)) == 0, g_stable[chip]);
                }
            }
        }
        return changed;
    }

    void pcf8575_task(void *) {
        /* First read latches the startup state and releases any pending INT.
         * It happens here, not in init(), because the I2C worker only runs once
         * the scheduler is going. */
        for (uint8_t chip = 0; chip < PCF8575_CHIP_COUNT; ++chip) {
            uint16_t value = 0xFFFF;
            pcf8575_read_port(kAddr[chip], value);
            g_stable[chip] = value;
            g_prev[chip] = value;
        }

        for (;;) {
            /* INT shortens the wait; the timeout guarantees the poll regardless. */
            const bool woken = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(PCF8575_POLL_MS)) != 0;

            if (scan_and_emit() && !woken) {
                ++g_missed;
            }
        }
    }

} // namespace

int pcf8575_read_port(uint8_t addr, uint16_t &value, TickType_t wait) {
    /* No register pointer on this part: a plain 2-byte read returns P0-P7 then
     * P8-P15. */
    uint8_t raw[2];
    const int rc = i2c_read(addr, raw, sizeof(raw), wait);
    if (rc < 0) {
        return rc;
    }
    value = (uint16_t)(raw[0] | ((uint16_t)raw[1] << 8));
    return 0;
}

int pcf8575_write_port(uint8_t addr, uint16_t value, TickType_t wait) {
    const uint8_t raw[2] = {(uint8_t)(value & 0xFF), (uint8_t)(value >> 8)};
    return i2c_write(addr, raw, sizeof(raw), wait);
}

bool pcf8575_init(UBaseType_t task_priority, UBaseType_t queue_depth) {
    if (g_events != nullptr) {
        return true;
    }

    for (uint8_t chip = 0; chip < PCF8575_CHIP_COUNT; ++chip) {
        g_stable[chip] = 0xFFFF;
        g_prev[chip] = 0xFFFF;
        g_shadow[chip] = 0xFFFF; /* every pin released, LEDs dark */
    }

    g_events = xQueueCreate(queue_depth, sizeof(Pcf8575Msg));
    if (g_events == nullptr) {
        return false;
    }

    if (xTaskCreate(pcf8575_task, "pcf8575", configMINIMAL_STACK_SIZE, nullptr, task_priority, &g_task) != pdPASS) {
        g_events = nullptr;
        return false;
    }

    gpio_init(IO_PCF8575_IRQ);
    gpio_set_dir(IO_PCF8575_IRQ, GPIO_IN);
    gpio_pull_up(IO_PCF8575_IRQ); /* INT is open-drain, active low */

    /* Raw handler rather than gpio_set_irq_callback(): that callback is global
     * to the bank, so claiming it would lock out every other GPIO IRQ user.
     * Cortex-M0+ has no BASEPRI, so the FreeRTOS port masks all interrupts in a
     * critical section and the SDK's default IRQ priority is safe for the
     * FromISR call in the handler. */
    gpio_add_raw_irq_handler(IO_PCF8575_IRQ, pcf8575_isr);
    gpio_set_irq_enabled(IO_PCF8575_IRQ, GPIO_IRQ_EDGE_FALL, true);
    irq_set_enabled(IO_IRQ_BANK0, true);
    return true;
}

bool pcf8575_wait(Pcf8575Msg &out, TickType_t wait) {
    if (g_events == nullptr) {
        return false;
    }
    return xQueueReceive(g_events, &out, wait) == pdPASS;
}

QueueHandle_t pcf8575_event_queue(void) { return g_events; }

uint16_t pcf8575_inputs(uint8_t chip) { return chip < PCF8575_CHIP_COUNT ? g_stable[chip] : 0xFFFF; }

uint32_t pcf8575_buttons(void) {
    /* Chip 0 is all buttons; chip 1 carries the rest in PCF8575_BTN_MASK. */
    const uint32_t low = (uint32_t)(uint16_t)~g_stable[0];
    const uint32_t high = (uint32_t)(uint16_t)(~g_stable[PCF8575_LED_CHIP] & PCF8575_BTN_MASK);
    return low | (high << 16);
}

uint16_t pcf8575_toggles(void) {
    const uint16_t active = (uint16_t)(~g_stable[PCF8575_LED_CHIP] & PCF8575_TOGGLE_MASK);
    return (uint16_t)(active >> __builtin_ctz(PCF8575_TOGGLE_MASK));
}

int pcf8575_set_leds(uint16_t state, TickType_t wait) {
    /* Input pins must stay written high, and an LED lights when its pin is
     * driven low, so the requested bits are inverted into the shadow. */
    const uint16_t word = (uint16_t)((0xFFFFu & ~PCF8575_LED_MASK) | (~state & PCF8575_LED_MASK));
    const int rc = pcf8575_write_port(kAddr[PCF8575_LED_CHIP], word, wait);
    if (rc < 0) {
        return rc;
    }
    g_shadow[PCF8575_LED_CHIP] = word;
    return 0;
}

int pcf8575_led(uint8_t index, bool on, TickType_t wait) {
    const uint16_t bit = (uint16_t)(1u << (__builtin_ctz(PCF8575_LED_MASK) + index));
    if ((bit & PCF8575_LED_MASK) == 0) {
        return I2C_ERR_ARG;
    }

    /* Shadow holds drive levels, so a lit LED is a cleared bit. */
    uint16_t state = (uint16_t)(~g_shadow[PCF8575_LED_CHIP] & PCF8575_LED_MASK);
    if (on) {
        state |= bit;
    } else {
        state &= (uint16_t)~bit;
    }
    return pcf8575_set_leds(state, wait);
}

uint32_t pcf8575_irq_count(void) { return g_irq_count; }

uint32_t pcf8575_missed_count(void) { return g_missed; }
