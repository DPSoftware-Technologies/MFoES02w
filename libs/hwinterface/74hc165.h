#pragma once
#include "gpio_sysfs.h"
#include <cstdint>

class HC165 {
    GpioPin pl;
    GpioPin clk;
    GpioPin data;

    void delayUs(int us);

public:
    HC165(int pl_pin, int clk_pin, int data_pin);

    uint8_t read8();
    void    readN(uint8_t* buf, int count);
};
