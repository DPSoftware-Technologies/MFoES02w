#include "spi_dev.h"

#include <vector>
#include <cerrno>
#include <cstring>

namespace southbridge {

SpiDevice::SpiDevice(const std::string& path, uint32_t speed_hz,
                     uint8_t mode, uint8_t bits)
    : fd_(-1), path_(path), speed_(speed_hz), mode_(mode), bits_(bits)
{
    open_device();
}

SpiDevice::~SpiDevice() {
    if (fd_ >= 0) ::close(fd_);
}

void SpiDevice::transfer(const uint8_t* tx, uint8_t* rx, size_t len) const {
    struct spi_ioc_transfer tr{};
    tr.tx_buf        = reinterpret_cast<uintptr_t>(tx);
    tr.rx_buf        = reinterpret_cast<uintptr_t>(rx);
    tr.len           = static_cast<uint32_t>(len);
    tr.speed_hz      = speed_;
    tr.bits_per_word = bits_;
    tr.cs_change     = 0;

    if (::ioctl(fd_, SPI_IOC_MESSAGE(1), &tr) < 0)
        throw std::runtime_error("SPI transfer failed: " + std::string(strerror(errno)));
}

void SpiDevice::write(const uint8_t* tx, size_t len) const {
    std::vector<uint8_t> dummy(len, 0xFF);
    transfer(tx, dummy.data(), len);
}

void SpiDevice::open_device() {
    fd_ = ::open(path_.c_str(), O_RDWR);
    if (fd_ < 0)
        throw std::runtime_error("Cannot open " + path_ + ": " + strerror(errno));

    auto check = [&](int r, const char* msg) {
        if (r < 0) throw std::runtime_error(std::string(msg) + ": " + strerror(errno));
    };
    check(ioctl(fd_, SPI_IOC_WR_MODE,          &mode_),  "SPI_IOC_WR_MODE");
    check(ioctl(fd_, SPI_IOC_WR_BITS_PER_WORD, &bits_),  "SPI_IOC_WR_BITS_PER_WORD");
    check(ioctl(fd_, SPI_IOC_WR_MAX_SPEED_HZ,  &speed_), "SPI_IOC_WR_MAX_SPEED_HZ");
}

} // namespace southbridge