#pragma once
/*
 * spi_device.h – thin RAII wrapper around Linux /dev/spidevX.X
 */

#include <cstdint>
#include <cstddef>
#include <stdexcept>
#include <string>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

namespace southbridge {

class SpiDevice {
public:
    SpiDevice(const std::string& path, uint32_t speed_hz,
              uint8_t mode = 0, uint8_t bits = 8)
        : fd_(-1), path_(path), speed_(speed_hz), mode_(mode), bits_(bits)
    {
        open_device();
    }

    ~SpiDevice() {
        if (fd_ >= 0) ::close(fd_);
    }

    /* full-duplex transfer: tx → MOSI, rx ← MISO, same length */
    void transfer(const uint8_t* tx, uint8_t* rx, size_t len) const {
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

    /* write only (rx discarded) */
    void write(const uint8_t* tx, size_t len) const {
        std::vector<uint8_t> dummy(len, 0xFF);
        transfer(tx, dummy.data(), len);
    }

    int  fd()    const { return fd_; }
    auto path()  const { return path_; }

private:
    int         fd_;
    std::string path_;
    uint32_t    speed_;
    uint8_t     mode_;
    uint8_t     bits_;

    void open_device() {
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

    /* non-copyable */
    SpiDevice(const SpiDevice&)            = delete;
    SpiDevice& operator=(const SpiDevice&) = delete;
};

} // namespace southbridge
