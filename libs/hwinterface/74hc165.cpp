#include "74hc165.h"
#include <unistd.h>

void HC165::delayUs(int us) {
    usleep(us);
}

HC165::HC165(int pl_pin, int clk_pin, int data_pin)
    : pl(pl_pin, true),
      clk(clk_pin, true),
      data(data_pin, false)
{
    pl.set(1);
    clk.set(0);
}

uint8_t HC165::read8() {
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

void HC165::readN(uint8_t* buf, int count) {
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
