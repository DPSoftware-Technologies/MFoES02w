#include "i2c_dev.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <stdexcept>

I2CBus::I2CBus(const char* dev) {
    fd = open(dev, O_RDWR);
    if (fd < 0) throw std::runtime_error("Cannot open I2C bus");
}

void I2CBus::setAddr(uint8_t addr) {
    if (ioctl(fd, I2C_SLAVE, addr) < 0)
        throw std::runtime_error("Cannot set I2C addr");
}

void I2CBus::writeByte(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    if (::write(fd, buf, 2) != 2)
        throw std::runtime_error("I2C write failed");
}

uint8_t I2CBus::readByte(uint8_t reg) {
    ::write(fd, &reg, 1);
    uint8_t val;
    if (::read(fd, &val, 1) != 1)
        throw std::runtime_error("I2C read failed");
    return val;
}

int I2CBus::getFd() const {
    return fd;
}

I2CBus::~I2CBus() {
    close(fd);
}
