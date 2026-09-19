#include "buzzer.h"

#include "hardware/clocks.h"
#include "hardware/pwm.h"
#include "pico/binary_info.h"
#include "pico/stdlib.h"

#include "IODef.h"

bi_decl(bi_1pin_with_name(IO_BUZ, "buzzer"));

namespace {

    bool g_ready = false;
    uint g_slice = 0;
    uint g_chan = 0;

    /* vTaskDelay once the scheduler owns the CPU, a busy sleep before that, so
     * the self test can beep from either side of vTaskStartScheduler(). */
    void wait_ms(uint32_t ms) {
        if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
            vTaskDelay(pdMS_TO_TICKS(ms));
        } else {
            sleep_ms(ms);
        }
    }

} // namespace

bool buzzer_init(void) {
    if (g_ready) {
        return true;
    }

    gpio_set_function(IO_BUZ, GPIO_FUNC_PWM);
    g_slice = pwm_gpio_to_slice_num(IO_BUZ);
    g_chan = pwm_gpio_to_channel(IO_BUZ);

    pwm_set_chan_level(g_slice, g_chan, 0);
    pwm_set_enabled(g_slice, false);

    g_ready = true;
    return true;
}

void buzzer_on(uint32_t freq_hz) {
    if (!g_ready || freq_hz == 0) {
        buzzer_off();
        return;
    }

    /* clk_sys = div * (wrap + 1) * freq. Pick the smallest integer divider that
     * keeps the counter inside 16 bits, then solve for wrap. */
    const uint32_t counts = clock_get_hz(clk_sys) / freq_hz;
    uint32_t div = (counts / 65536u) + 1u;
    if (div > 255u) {
        div = 255u; /* below ~8 Hz on a 125 MHz clk_sys; inaudible anyway */
    }

    uint32_t wrap = counts / div;
    if (wrap < 2u) {
        wrap = 2u;
    } else if (wrap > 65536u) {
        wrap = 65536u;
    }

    pwm_set_clkdiv(g_slice, (float)div);
    pwm_set_wrap(g_slice, (uint16_t)(wrap - 1u));
    pwm_set_chan_level(g_slice, g_chan, (uint16_t)((wrap * BUZZER_DUTY_PCT) / 100u));
    pwm_set_enabled(g_slice, true);
}

void buzzer_off(void) {
    if (!g_ready) {
        return;
    }
    pwm_set_chan_level(g_slice, g_chan, 0); /* park the pin low, not floating */
    pwm_set_enabled(g_slice, false);
}

void buzzer_tone(uint32_t freq_hz, uint32_t ms) {
    buzzer_on(freq_hz);
    wait_ms(ms);
    buzzer_off();
}

void buzzer_play(const BuzzerNote *notes, size_t count) {
    if (notes == nullptr) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        buzzer_tone(notes[i].freq_hz, notes[i].ms);
    }
    buzzer_off();
}
