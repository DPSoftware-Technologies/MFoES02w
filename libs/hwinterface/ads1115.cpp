#include "ads1115.h"
#include <unistd.h>

void ADS1115::writeReg16(uint8_t reg, uint16_t val) {
    uint8_t buf[3] = {
        reg,
        (uint8_t)(val >> 8),
        (uint8_t)(val & 0xFF)
    };
    write(i2c.getFd(), buf, 3);
}

uint16_t ADS1115::readReg16(uint8_t reg) {
    write(i2c.getFd(), &reg, 1);
    uint8_t buf[2];
    read(i2c.getFd(), buf, 2);
    return (buf[0] << 8) | buf[1];
}

ADS1115::ADS1115(I2CBus& bus, uint8_t address)
    : i2c(bus), addr(address)
{
    i2c.setAddr(addr);
}

int16_t ADS1115::readRaw(Mux mux, Gain gain) {
    uint16_t config = OS_SINGLE | mux | gain | MODE_SINGLE | DR_128SPS;
    writeReg16(REG_CONFIG, config);

    usleep(9000);

    for (int i = 0; i < 50; i++) {
        uint16_t status = readReg16(REG_CONFIG);
        if (status & 0x8000) break;
        usleep(1000);
    }

    return (int16_t)readReg16(REG_CONV);
}

float ADS1115::readVoltage(Mux mux, Gain gain) {
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
    return (readRaw(mux, gain) / 32767.0f) * fsr;
}
