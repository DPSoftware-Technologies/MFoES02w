#pragma once
#include "i2c_dev.h"
#include <cstdint>
#include <unistd.h>

class ADS1115 {
    I2CBus& i2c;
    uint8_t addr;

    // Registers
    static constexpr uint8_t REG_CONV   = 0x00;
    static constexpr uint8_t REG_CONFIG = 0x01;

    // Config bits
    static constexpr uint16_t OS_SINGLE  = 0x8000; // Start single conversion
    static constexpr uint16_t MODE_SINGLE = 0x0100; // Single shot mode
    static constexpr uint16_t DR_128SPS  = 0x0080; // 128 samples/sec

    void writeReg16(uint8_t reg, uint16_t val) {
        uint8_t buf[3] = {
            reg,
            (uint8_t)(val >> 8),
            (uint8_t)(val & 0xFF)
        };
        write(i2c.getFd(), buf, 3);
    }

    uint16_t readReg16(uint8_t reg) {
        write(i2c.getFd(), &reg, 1);
        uint8_t buf[2];
        read(i2c.getFd(), buf, 2);
        return (buf[0] << 8) | buf[1];
    }

public:
    // PGA (gain) options — full scale range
    enum Gain : uint16_t {
        PGA_6V144 = 0x0000,  // ±6.144V
        PGA_4V096 = 0x0200,  // ±4.096V
        PGA_2V048 = 0x0400,  // ±2.048V (default)
        PGA_1V024 = 0x0600,  // ±1.024V
        PGA_0V512 = 0x0800,  // ±0.512V
        PGA_0V256 = 0x0A00,  // ±0.256V
    };

    // Mux — which channel to read
    enum Mux : uint16_t {
        AIN0_GND = 0x4000,  // Single ended A0
        AIN1_GND = 0x5000,  // Single ended A1
        AIN2_GND = 0x6000,  // Single ended A2
        AIN3_GND = 0x7000,  // Single ended A3
        AIN0_AIN1 = 0x0000, // Differential A0-A1
        AIN0_AIN3 = 0x1000, // Differential A0-A3
        AIN1_AIN3 = 0x2000, // Differential A1-A3
        AIN2_AIN3 = 0x3000, // Differential A2-A3
    };

    ADS1115(I2CBus& bus, uint8_t address = 0x48)
        : i2c(bus), addr(address) {
        i2c.setAddr(addr);
    }

    // Read raw 16-bit value
    int16_t readRaw(Mux mux = AIN0_GND, Gain gain = PGA_4V096) {
        uint16_t config = OS_SINGLE | mux | gain | MODE_SINGLE | DR_128SPS;
        writeReg16(REG_CONFIG, config);

        // Wait for conversion (~8ms at 128SPS)
        usleep(9000);

        // Poll OS bit until ready
        for (int i = 0; i < 50; i++) {
            uint16_t status = readReg16(REG_CONFIG);
            if (status & 0x8000) break; // OS=1 means ready
            usleep(1000);
        }

        return (int16_t)readReg16(REG_CONV);
    }

    // Read voltage in volts
    float readVoltage(Mux mux = AIN0_GND, Gain gain = PGA_4V096) {
        // FSR lookup table
        float fsr;
        switch (gain) {
            case PGA_6V144: fsr = 6.144f; break;
            case PGA_4V096: fsr = 4.096f; break;
            case PGA_2V048: fsr = 2.048f; break;
            case PGA_1V024: fsr = 1.024f; break;
            case PGA_0V512: fsr = 0.512f; break;
            case PGA_0V256: fsr = 0.256f; break;
            default:        fsr = 4.096f; break;
        }
        // 16-bit signed, FSR = 32767 counts
        return (readRaw(mux, gain) / 32767.0f) * fsr;
    }
};