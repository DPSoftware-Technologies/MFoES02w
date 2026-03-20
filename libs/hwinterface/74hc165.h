#pragma once
#include "gpio_sysfs.h"
#include <cstdint>
#include <unistd.h>

class HC165 {
    GpioPin pl;   // latch (active LOW)
    GpioPin clk;  // clock
    GpioPin data; // Q7 serial out

    void delayUs(int us) { usleep(us); }

public:
    // pin numbers are BCM, offset handled by GpioPin
    HC165(int pl_pin, int clk_pin, int data_pin)
        : pl(pl_pin, true),
          clk(clk_pin, true),
          data(data_pin, false)
    {
        pl.set(1);   // idle high
        clk.set(0);
    }

    // Read 8 bits from one 74HC165
    uint8_t read8() {
        // Latch parallel inputs
        pl.set(0);
        delayUs(1);
        pl.set(1);

        uint8_t result = 0;
        for (int i = 7; i >= 0; i--) {
            result |= (data.get() << i);
            clk.set(1);
            delayUs(1);
            clk.set(0);
            delayUs(1);
        }
        return result;
    }

    // Read N daisy-chained 74HC165s (Q7→SER of next chip)
    void readN(uint8_t* buf, int count) {
        // Latch all chips simultaneously
        pl.set(0);
        delayUs(1);
        pl.set(1);

        for (int b = count - 1; b >= 0; b--) {
            buf[b] = 0;
            for (int i = 7; i >= 0; i--) {
                buf[b] |= (data.get() << i);
                clk.set(1);
                delayUs(1);
                clk.set(0);
                delayUs(1);
            }
        }
    }
};