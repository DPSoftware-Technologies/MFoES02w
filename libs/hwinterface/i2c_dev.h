#pragma once
#include <cstdint>

class I2CBus {
    int fd;

public:
    I2CBus(const char* dev = "/dev/i2c-1");

    void    setAddr(uint8_t addr);
    void    writeByte(uint8_t reg, uint8_t val);
    uint8_t readByte(uint8_t reg);
    int     getFd() const;

    ~I2CBus();
};
