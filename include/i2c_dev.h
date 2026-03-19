#pragma once
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <cstdint>
#include <stdexcept>

class I2CBus {
    int fd;
public:
    I2CBus(const char* dev = "/dev/i2c-1") {
        fd = open(dev, O_RDWR);
        if (fd < 0) throw std::runtime_error("Cannot open I2C bus");
    }

    void setAddr(uint8_t addr) {
        if (ioctl(fd, I2C_SLAVE, addr) < 0)
            throw std::runtime_error("Cannot set I2C addr");
    }

    void writeByte(uint8_t reg, uint8_t val) {
        uint8_t buf[2] = {reg, val};
        if (::write(fd, buf, 2) != 2)
            throw std::runtime_error("I2C write failed");
    }

    uint8_t readByte(uint8_t reg) {
        ::write(fd, &reg, 1);
        uint8_t val;
        if (::read(fd, &val, 1) != 1)
            throw std::runtime_error("I2C read failed");
        return val;
    }
    int getFd() const { return fd; }

    ~I2CBus() { close(fd); }
};