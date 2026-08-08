#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>

#include "nb_protocol.h"

namespace northbridge {

/*  Data link to the northbridge (Pico 2W) over SPI1.
 *
 *  The Pi is the SPI master; the northbridge is a slave and can never start a
 *  transfer. It raises its IRQ line instead, and we clock a frame out to fetch
 *  whatever it has. Every exchange is full duplex and exactly kFrameSize
 *  bytes, so one transfer carries an outbound frame AND an inbound one.
 *
 *  Wiring (BCM numbering):
 *      GPIO 21  SPI1 SCLK   -> northbridge GPIO 10
 *      GPIO 20  SPI1 MOSI   -> northbridge GPIO 12  (its SPI1 RX)
 *      GPIO 19  SPI1 MISO   <- northbridge GPIO 11  (its SPI1 TX)
 *      GPIO 18  SPI1 CE0    -> northbridge GPIO 13
 *      GPIO 24  IRQ in      <- northbridge GPIO 27, active low
 *
 *  Requires `dtoverlay=spi1-1cs` in /boot/firmware/config.txt, which creates
 *  /dev/spidev1.0. SPI1 is the auxiliary block: it only does CPHA=0, so keep
 *  the mode at 0, and the northbridge's PL022 needs clk_peri >= 12x SCK, which
 *  caps the clock near 10 MHz.
 *
 *  Wire format, little-endian CRC, identical to Firmware/northbridge/src/dspi.h:
 *      [0]      magic  kMagic
 *      [1]      type   application defined; kTypeIdle means "nothing here"
 *      [2]      len    payload bytes in use
 *      [3]      flags  kFlagMore = the sender has another frame queued
 *      [4..61]  payload
 *      [62..63] CRC-16/CCITT-FALSE over bytes 0..61                        */

inline constexpr size_t  kFrameSize   = 64;
inline constexpr size_t  kPayloadMax  = 58;
inline constexpr uint8_t kMagic       = 0xA5;
inline constexpr uint8_t kTypeIdle    = 0x00;
inline constexpr uint8_t kFlagMore    = 0x01;
inline constexpr uint8_t kFlagReply   = 0x02;   /* answer to a request of the same type */

/* Frame types and payload layouts come from the firmware's own header, so the
 * two ends cannot disagree about the wire. It pulls in nothing but stdint. */
inline constexpr uint8_t kTypePing     = NB_CMD_PING;
inline constexpr uint8_t kTypePanel    = NB_CMD_PANEL_EVT;
inline constexpr uint8_t kTypeLeds     = NB_CMD_SET_LEDS;
inline constexpr uint8_t kTypeGetPanel = NB_CMD_GET_PANEL;
inline constexpr uint8_t kTypeGetTime  = NB_CMD_GET_TIME;
inline constexpr uint8_t kTypeSetTime  = NB_CMD_SET_TIME;
inline constexpr uint8_t kTypeGetInfo  = NB_CMD_GET_INFO;

struct Frame {
    uint8_t type  = kTypeIdle;
    uint8_t len   = 0;
    uint8_t flags = 0;
    std::array<uint8_t, kPayloadMax> payload{};
};

/* Decoded kTypePanel payload. */
struct PanelState {
    uint8_t  chip    = 0;
    uint8_t  pin     = 0;
    bool     active  = false;   /* true = pressed / closed */
    uint32_t buttons = 0;       /* 24 debounced button bits, 1 = pressed */
    uint8_t  toggles = 0;       /* 4 toggle bits, 1 = on */

    static bool decode(const Frame& f, PanelState& out);
};

struct Stats {
    uint64_t frames_tx    = 0;   /* frames carrying real payload we sent   */
    uint64_t frames_rx    = 0;   /* non-idle frames received               */
    uint64_t exchanges    = 0;   /* SPI transfers issued, idle included    */
    uint64_t blank_frames = 0;   /* all-zero reads: nothing driving MISO    */
    uint64_t crc_errors   = 0;   /* bad magic or CRC from the northbridge  */
    uint64_t spi_errors   = 0;   /* ioctl failures                         */
    uint64_t irq_events   = 0;   /* IRQ assertions observed                */
    uint64_t tx_dropped   = 0;   /* outbound frames dropped, queue full    */
    uint64_t request_retries  = 0; /* requests answered only after a resend  */
    uint64_t request_timeouts = 0; /* requests that gave up entirely         */
};

struct Config {
    std::string spi_device    = "/dev/spidev1.0";  /* SPI1 CE0 = GPIO 18 */
    uint32_t    spi_speed_hz  = 8'000'000;         /* keep <= ~10 MHz    */
    /* Mode 0, paired with cs_per_byte below.
     *
     * The RP2040's PL022 slave only sustains a multi-byte burst under one chip
     * select when CPHA=1, and it emits exactly one byte per CS otherwise. But
     * the Pi's auxiliary SPI1 rejects CPHA=1 outright -- SPI_IOC_WR_MODE
     * returns EINVAL -- so mode 1 is not available on this bus. Pulsing CS per
     * byte at mode 0 gives the slave the framing it needs and keeps this link
     * off SPI0, which carries audio.
     *
     * On SPI0 (a full controller) mode 1 with cs_per_byte = false is faster. */
    uint8_t     spi_mode      = 0;

    /* Sends the frame as one 1-byte transfer per byte with cs_change set, so
     * the chip select pulses between bytes and satisfies a CPHA=0 PL022 slave.
     * Required on the auxiliary SPI1, which cannot do CPHA=1.
     *
     * Pair it with spi_mode = 0 and DSPI_CPHA = SPI_CPHA_0 in the firmware:
     * the two ends must agree, and these settings are opposites. */
    bool        cs_per_byte   = true;

    /* Bytes per ioctl in cs_per_byte mode. spidev rounds every transfer's
     * bounce-buffer slot up to ARCH_KMALLOC_MINALIGN (64-128 bytes on arm64),
     * so a whole 64-byte frame in one message wants 4-8 KB against a default
     * spidev bufsiz of 4096 and fails with EMSGSIZE. Chunking keeps each
     * message inside that budget.
     *
     * Splitting is safe: the peer only shifts while its own chip select is low,
     * so another device's message landing between chunks is invisible to it.
     * Raise this (or all of spidev's bufsiz via the module parameter
     * spidev.bufsiz=65536) to cut the number of ioctls per frame. */
    size_t      cs_chunk_bytes = 16;
    uint8_t     spi_bits      = 8;

    std::string gpio_chip     = "/dev/gpiochip0";
    unsigned    irq_line      = 24;                /* BCM GPIO 24 */
    bool        irq_active_low = true;

    /* Set false to ignore the IRQ line entirely and rely on polling. Useful
     * during bringup: a floating IRQ pin picks up noise from each transfer and
     * drives the loop by itself, which looks like real traffic. */
    bool        use_irq       = true;

    /* Exchange at least this often even with the IRQ line quiet. Bounds the
     * damage from a missed edge and carries our outbound frames when the
     * northbridge has nothing to say. 0 disables polling entirely. */
    unsigned    poll_interval_ms = 20;

    /* Outbound queue depth; sends beyond this drop and count in tx_dropped. */
    size_t      tx_queue_depth = 64;

    /* SCHED_FIFO priority for the link thread, 0 = leave scheduling alone. */
    int         thread_prio   = 0;
};

class Link {
public:
    using FrameCallback = std::function<void(const Frame&)>;
    using ErrorCallback = std::function<void(const std::string&)>;
    /* Every inbound frame, before validation. For bringup only. */
    using RawCallback   = std::function<void(const uint8_t* frame, size_t len)>;

    virtual ~Link() = default;

    static std::unique_ptr<Link> create(Config cfg = {});

    virtual bool start() = 0;
    virtual void stop()  = 0;
    virtual bool is_running() const = 0;

    /* Queues a frame for the next exchange. Thread-safe, never blocks.
     * Returns false if len is too large or the queue is full. */
    virtual bool send(uint8_t type, const void* data, size_t len) = 0;

    /* Sends a command and waits for the peer's reply of the same type.
     * Returns false once every attempt has timed out. Replies are matched by
     * type, so do not run two requests of one type concurrently. The reply
     * never reaches on_frame().
     *
     * `attempts` covers the case where a frame is lost to a corrupted transfer:
     * the commands here are idempotent reads and sets, so resending is safe.
     *
     *     northbridge::Frame reply;
     *     if (link->request(northbridge::kTypeGetTime, nullptr, 0, reply)) { ... } */
    virtual bool request(uint8_t type, const void* data, size_t len, Frame& reply,
                         unsigned timeout_ms = 250, unsigned attempts = 3) = 0;

    /* Runs on the link thread: keep it short, hand work off elsewhere. */
    virtual void on_frame(FrameCallback cb) = 0;
    virtual void on_error(ErrorCallback cb) = 0;
    virtual void on_raw(RawCallback cb) = 0;

    /* Forces one exchange now instead of waiting for an IRQ or the poll. */
    virtual void poke() = 0;

    virtual Stats stats() const = 0;
};

/* CRC-16/CCITT-FALSE, exposed for tests and for anyone hand-building frames. */
uint16_t crc16_ccitt(const uint8_t* data, size_t len);

/* Pulls a wire struct out of a reply, checking the length first. Use it on the
 * Frame that request() filled in:
 *
 *     NbTimeWire t;
 *     if (link->request(kTypeGetTime, nullptr, 0, reply) && payload_of(reply, t)) { ... } */
template <typename T>
inline bool payload_of(const Frame& f, T& out) {
    if (f.len < sizeof(T)) return false;
    std::memcpy(&out, f.payload.data(), sizeof(T));
    return true;
}

} // namespace northbridge
