#pragma once
#include "i2c_dev.h"
#include <cstdint>

class ADS1115 {
    I2CBus& i2c;
    uint8_t addr;

    static constexpr uint8_t  REG_CONV    = 0x00;
    static constexpr uint8_t  REG_CONFIG  = 0x01;
    static constexpr uint16_t OS_SINGLE   = 0x8000;
    static constexpr uint16_t MODE_SINGLE = 0x0100;
    static constexpr uint16_t DR_128SPS   = 0x0080;

    void     writeReg16(uint8_t reg, uint16_t val);
    uint16_t readReg16(uint8_t reg);

public:
    enum Gain : uint16_t {
        PGA_6V144 = 0x0000,
        PGA_4V096 = 0x0200,
        PGA_2V048 = 0x0400,
        PGA_1V024 = 0x0600,
        PGA_0V512 = 0x0800,
        PGA_0V256 = 0x0A00,
    };

    enum Mux : uint16_t {
        AIN0_GND  = 0x4000,
        AIN1_GND  = 0x5000,
        AIN2_GND  = 0x6000,
        AIN3_GND  = 0x7000,
        AIN0_AIN1 = 0x0000,
        AIN0_AIN3 = 0x1000,
        AIN1_AIN3 = 0x2000,
        AIN2_AIN3 = 0x3000,
    };

    ADS1115(I2CBus& bus, uint8_t address = 0x48);

    int16_t readRaw(Mux mux = AIN0_GND, Gain gain = PGA_4V096);
    float   readVoltage(Mux mux = AIN0_GND, Gain gain = PGA_4V096);
};
