#include "encoder.h"

#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "pico/binary_info.h"
#include "pico/stdlib.h"

#include "IODef.h"

bi_decl(bi_1pin_with_name(IO_RE_S1, "encoder A"));
bi_decl(bi_1pin_with_name(IO_RE_S2, "encoder B"));
bi_decl(bi_1pin_with_name(IO_RE_BTN, "encoder button"));

namespace {

    constexpr uint32_t kQuadMask = (1u << IO_RE_S1) | (1u << IO_RE_S2);
    constexpr uint32_t kPinMask = kQuadMask | (1u << IO_RE_BTN);
    constexpr uint32_t kBothEdges = GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL;
    constexpr int8_t kSign = ENCODER_CW_POSITIVE ? 1 : -1;

    /* Quadrature lines first, then the button: the loops below rely on it. */
    const uint kPins[3] = {IO_RE_S1, IO_RE_S2, IO_RE_BTN};

    /* Indexed by (previous state << 2) | current state, state = (A << 1) | B.
     * Legal moves change exactly one line and give +/-1; a two-bit jump means a
     * transition was missed, and reads as 0 so it cannot fake a count. */
    const int8_t kQuadTable[16] = {
        0,  -1, +1, 0,  /* prev 00 */
        +1, 0,  0,  -1, /* prev 01 */
        -1, 0,  0,  +1, /* prev 10 */
        0,  +1, -1, 0,  /* prev 11 */
    };

    QueueHandle_t g_events = nullptr;
    TaskHandle_t g_task = nullptr;

    volatile uint8_t g_state = 0;         /* last quadrature state seen by the ISR */
    volatile int8_t g_sub = 0;            /* edges accumulated inside one detent */
    volatile int32_t g_position = 0;      /* detents since boot */
    volatile int32_t g_queue_delta = 0;   /* detents the task has yet to report */
    volatile int32_t g_poll_delta = 0;    /* detents encoder_take_delta() has yet to return */
    volatile uint32_t g_errors = 0;       /* skipped-state transitions */
    bool g_pressed = false;               /* debounced button, task owned */

    uint8_t read_state(void) { return (uint8_t)((gpio_get(IO_RE_S1) ? 2u : 0u) | (gpio_get(IO_RE_S2) ? 1u : 0u)); }

    void encoder_isr(void) {
        bool quad = false;
        bool any = false;

        /* The bank IRQ is shared with every other GPIO user, so only the events
         * belonging to these pins may be acknowledged. */
        for (unsigned i = 0; i < 2; ++i) {
            const uint32_t events = gpio_get_irq_event_mask(kPins[i]) & kBothEdges;
            if (events != 0) {
                gpio_acknowledge_irq(kPins[i], events);
                quad = true;
            }
        }

        const uint32_t btn_events = gpio_get_irq_event_mask(IO_RE_BTN) & kBothEdges;
        if (btn_events != 0) {
            gpio_acknowledge_irq(IO_RE_BTN, btn_events);
            any = true;
        }

        if (quad) {
            any = true;
            const uint8_t state = read_state();
            if (state != g_state) {
                const int8_t dir = kQuadTable[(g_state << 2) | state];
                if (dir == 0) {
                    ++g_errors; /* skipped a state: bounce or a missed edge */
                }
                g_state = state;

                g_sub = (int8_t)(g_sub + dir * kSign);
                while (g_sub >= ENCODER_STEPS_PER_DETENT) {
                    g_sub = (int8_t)(g_sub - ENCODER_STEPS_PER_DETENT);
                    ++g_position;
                    ++g_queue_delta;
                    ++g_poll_delta;
                }
                while (g_sub <= -ENCODER_STEPS_PER_DETENT) {
                    g_sub = (int8_t)(g_sub + ENCODER_STEPS_PER_DETENT);
                    --g_position;
                    --g_queue_delta;
                    --g_poll_delta;
                }
            }
        }

        if (!any) {
            return; /* shared bank IRQ: this edge belongs to another pin */
        }

        BaseType_t higher_woken = pdFALSE;
        vTaskNotifyGiveFromISR(g_task, &higher_woken);
        portYIELD_FROM_ISR(higher_woken);
    }

    void emit(int8_t delta, bool button_edge) {
        const EncoderMsg msg{delta, g_pressed, button_edge, encoder_position()};
        xQueueSend(g_events, &msg, 0); /* drop rather than stall the task */
    }

    /* Wakes on any edge, waits out the contact bounce, then reports whatever
     * settled: the detents the ISR counted and the button level if it moved.
     * The wait also merges a fast spin into one message carrying several
     * detents, instead of one message per click. */
    void encoder_task(void *) {
        g_state = read_state();
        g_pressed = (gpio_get(IO_RE_BTN) == 0);

        for (;;) {
            if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY) == 0) {
                continue;
            }

            vTaskDelay(pdMS_TO_TICKS(ENCODER_DEBOUNCE_MS));

            taskENTER_CRITICAL();
            const int32_t delta = g_queue_delta;
            g_queue_delta = 0;
            taskEXIT_CRITICAL();

            if (delta != 0) {
                /* One message per detent burst; clamped only because the wire
                 * field is a byte, which a human hand cannot outrun. */
                const int32_t clamped = delta > 127 ? 127 : (delta < -127 ? -127 : delta);
                emit((int8_t)clamped, false);
            }

            const bool pressed = (gpio_get(IO_RE_BTN) == 0);
            if (pressed != g_pressed) {
                g_pressed = pressed;
                emit(0, true);
            }
        }
    }

} // namespace

bool encoder_init(UBaseType_t task_priority, UBaseType_t queue_depth) {
    if (g_events != nullptr) {
        return true;
    }

    g_events = xQueueCreate(queue_depth, sizeof(EncoderMsg));
    if (g_events == nullptr) {
        return false;
    }

    for (unsigned i = 0; i < 3; ++i) {
        gpio_init(kPins[i]);
        gpio_set_dir(kPins[i], GPIO_IN);
        gpio_pull_up(kPins[i]); /* common pin grounded, closed contact reads low */
    }

    g_state = read_state();
    g_pressed = (gpio_get(IO_RE_BTN) == 0);

    if (xTaskCreate(encoder_task, "encoder", configMINIMAL_STACK_SIZE, nullptr, task_priority, &g_task) != pdPASS) {
        vQueueDelete(g_events);
        g_events = nullptr;
        return false;
    }

    /* Raw masked handler rather than gpio_set_irq_callback(): that callback is
     * global to the bank and would lock out the PCF8575 INT handler. */
    gpio_add_raw_irq_handler_masked(kPinMask, encoder_isr);
    for (unsigned i = 0; i < 3; ++i) {
        gpio_set_irq_enabled(kPins[i], kBothEdges, true);
    }
    irq_set_enabled(IO_IRQ_BANK0, true);
    return true;
}

bool encoder_wait(EncoderMsg &out, TickType_t wait) {
    if (g_events == nullptr) {
        return false;
    }
    return xQueueReceive(g_events, &out, wait) == pdPASS;
}

QueueHandle_t encoder_event_queue(void) { return g_events; }

int32_t encoder_position(void) { return g_position; }

bool encoder_button(void) { return g_pressed; }

int32_t encoder_take_delta(void) {
    taskENTER_CRITICAL();
    const int32_t delta = g_poll_delta;
    g_poll_delta = 0;
    taskEXIT_CRITICAL();
    return delta;
}

bool encoder_line_a(void) { return gpio_get(IO_RE_S1); }

bool encoder_line_b(void) { return gpio_get(IO_RE_S2); }

uint32_t encoder_error_count(void) { return g_errors; }
