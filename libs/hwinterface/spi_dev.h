#pragma once

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
    SpiDevice(const std::string& path, uint32_t speed_hz,uint8_t mode = 0, uint8_t bits = 8);

    ~SpiDevice();

    /* full-duplex transfer: tx → MOSI, rx ← MISO, same length */
    void transfer(const uint8_t* tx, uint8_t* rx, size_t len) const;

    /* write only (rx discarded) */
    void write(const uint8_t* tx, size_t len) const;

    int  fd()    const { return fd_; }
    auto path()  const { return path_; }

private:
    int         fd_;
    std::string path_;
    uint32_t    speed_;
    uint8_t     mode_;
    uint8_t     bits_;

    void open_device();

    /* non-copyable */
    SpiDevice(const SpiDevice&) = delete;
    SpiDevice& operator=(const SpiDevice&) = delete;
};

} // namespace southbridge
