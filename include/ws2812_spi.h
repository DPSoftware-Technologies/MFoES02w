#pragma once
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

struct Color {
    uint8_t r, g, b;
};

class WS2812 {
    int fd;
    int numLeds;
    std::vector<Color> leds;

    // Each WS2812 bit → 3 SPI bits
    // At 6.4MHz: 1 SPI bit = 156.25ns
    // T0H=313ns(2bits) T0L=469ns... close enough
    void encodeByte(uint8_t byte, uint8_t* out) {
        // Each input byte → 3 output bytes (24 SPI bits)
        int outIdx = 0;
        uint32_t encoded = 0;

        for (int i = 7; i >= 0; i--) {
            if ((byte >> i) & 1)
                encoded = (encoded << 3) | 0b110;
            else
                encoded = (encoded << 3) | 0b100;
        }

        // Pack 24 bits into 3 bytes
        out[0] = (encoded >> 16) & 0xFF;
        out[1] = (encoded >> 8)  & 0xFF;
        out[2] = (encoded)       & 0xFF;
    }

public:
    WS2812(int numLeds, const char* dev = "/dev/spidev0.0")
        : numLeds(numLeds), leds(numLeds, {0, 0, 0})
    {
        fd = open(dev, O_RDWR);
        if (fd < 0) throw std::runtime_error("Cannot open SPI device");

        // SPI config
        uint8_t mode  = SPI_MODE_0;
        uint8_t bits  = 8;
        uint32_t speed = 6400000; // 6.4 MHz

        ioctl(fd, SPI_IOC_WR_MODE, &mode);
        ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
        ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
    }

    // Set individual LED color
    void setColor(int idx, uint8_t r, uint8_t g, uint8_t b) {
        if (idx < 0 || idx >= numLeds) return;
        leds[idx] = {r, g, b};
    }

    void setColor(int idx, Color c) {
        setColor(idx, c.r, c.g, c.b);
    }

    // Fill all LEDs
    void fill(uint8_t r, uint8_t g, uint8_t b) {
        for (auto& c : leds) c = {r, g, b};
    }

    void clear() { fill(0, 0, 0); }

    // Push to strip
    void show() {
        // 3 bytes per color channel, 3 channels per LED + reset
        int dataSize = numLeds * 9; // 3 bytes * 3 colors
        int resetSize = 80;         // >50µs reset
        std::vector<uint8_t> buf(dataSize + resetSize, 0x00);

        int idx = 0;
        for (const auto& c : leds) {
            // WS2812 order is GRB
            encodeByte(c.g, &buf[idx]); idx += 3;
            encodeByte(c.r, &buf[idx]); idx += 3;
            encodeByte(c.b, &buf[idx]); idx += 3;
        }
        // Last resetSize bytes are already 0x00 = reset pulse

        struct spi_ioc_transfer tr{};
        tr.tx_buf = (unsigned long)buf.data();
        tr.len    = buf.size();
        tr.speed_hz = 6400000;
        tr.bits_per_word = 8;

        if (ioctl(fd, SPI_IOC_MESSAGE(1), &tr) < 0)
            throw std::runtime_error("SPI transfer failed");
    }

    ~WS2812() { close(fd); }
};