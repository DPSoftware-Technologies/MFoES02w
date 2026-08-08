#include "northbridge.h"

#include "spi_dev.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include <stdexcept>

#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/gpio.h>

namespace northbridge {

namespace {

constexpr size_t kOffMagic   = 0;
constexpr size_t kOffType    = 1;
constexpr size_t kOffLen     = 2;
constexpr size_t kOffFlags   = 3;
constexpr size_t kOffPayload = 4;
constexpr size_t kOffCrc     = kFrameSize - 2;

/*  IRQ line via the GPIO character device.
 *
 *  The sysfs interface would work too, but it is deprecated and racy on export.
 *  This uses the v2 ioctls directly — no libgpiod dependency, which keeps the
 *  static musl build self-contained.                                        */
class IrqLine {
public:
    ~IrqLine() { close(); }

    bool open(const std::string& chip, unsigned line, bool active_low) {
        chip_fd_ = ::open(chip.c_str(), O_RDONLY | O_CLOEXEC);
        if (chip_fd_ < 0) return false;

        gpio_v2_line_request req{};
        req.offsets[0]     = line;
        req.num_lines      = 1;
        req.config.flags   = GPIO_V2_LINE_FLAG_INPUT;
        std::snprintf(req.consumer, sizeof(req.consumer), "northbridge-irq");

        if (active_low) {
            /* Assertion is a falling edge; bias keeps the line released while
             * the northbridge is still in reset. */
            req.config.flags |= GPIO_V2_LINE_FLAG_EDGE_FALLING |
                                GPIO_V2_LINE_FLAG_ACTIVE_LOW |
                                GPIO_V2_LINE_FLAG_BIAS_PULL_UP;
        } else {
            req.config.flags |= GPIO_V2_LINE_FLAG_EDGE_RISING |
                                GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN;
        }

        if (::ioctl(chip_fd_, GPIO_V2_GET_LINE_IOCTL, &req) < 0 || req.fd < 0) {
            close();
            return false;
        }
        line_fd_ = req.fd;

        /* The kernel hands back a BLOCKING fd. Draining queued edges would
         * then park forever on the read after the last one. */
        const int flags = ::fcntl(line_fd_, F_GETFL, 0);
        if (flags < 0 || ::fcntl(line_fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
            close();
            return false;
        }
        return true;
    }

    /* True if an edge arrived within timeout_ms (negative = wait forever). */
    bool wait(int timeout_ms) {
        if (line_fd_ < 0) {
            if (timeout_ms > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
            }
            return false;
        }

        pollfd pfd{line_fd_, POLLIN, 0};
        const int rc = ::poll(&pfd, 1, timeout_ms);
        if (rc <= 0) return false;

        /* Drain every queued edge: one exchange settles them all. The fd is
         * non-blocking, so this ends on EAGAIN once the queue is empty. */
        bool seen = false;
        gpio_v2_line_event ev{};
        while (::read(line_fd_, &ev, sizeof(ev)) == static_cast<ssize_t>(sizeof(ev))) {
            seen = true;
        }
        return seen;
    }

    /* Current logical level: true = asserted (ACTIVE_LOW already applied). */
    bool asserted() const {
        if (line_fd_ < 0) return false;
        gpio_v2_line_values values{};
        values.mask = 1;
        if (::ioctl(line_fd_, GPIO_V2_LINE_GET_VALUES_IOCTL, &values) < 0) return false;
        return (values.bits & 1) != 0;
    }

    bool valid() const { return line_fd_ >= 0; }

    void close() {
        if (line_fd_ >= 0) { ::close(line_fd_); line_fd_ = -1; }
        if (chip_fd_ >= 0) { ::close(chip_fd_); chip_fd_ = -1; }
    }

private:
    int chip_fd_ = -1;
    int line_fd_ = -1;
};

class LinkImpl final : public Link {
public:
    explicit LinkImpl(Config cfg) : cfg_(std::move(cfg)) {}

    ~LinkImpl() override { stop(); }

    bool start() override {
        if (running_) return true;

        try {
            spi_ = std::make_unique<southbridge::SpiDevice>(cfg_.spi_device, cfg_.spi_speed_hz,
                                                            cfg_.spi_mode, cfg_.spi_bits);
        } catch (const std::exception& e) {
            report(e.what());
            return false;
        }

        if (!cfg_.use_irq) {
            report("IRQ line disabled by config, polling only");
        } else if (!irq_.open(cfg_.gpio_chip, cfg_.irq_line, cfg_.irq_active_low)) {
            /* Not fatal: polling alone still moves data, just with more
             * latency and more idle traffic. */
            report("IRQ line unavailable on " + cfg_.gpio_chip + ", falling back to polling");
        } else {
            /* A line already asserted while the peer should be idle usually
             * means it is floating rather than driven. */
            report(std::string("IRQ line at startup: ") + (irq_.asserted() ? "ASSERTED" : "released"));
        }

        build_frame(idle_frame_.data(), kTypeIdle, nullptr, 0, false);

        running_ = true;
        thread_  = std::thread(&LinkImpl::run, this);
        return true;
    }

    void stop() override {
        if (!running_.exchange(false)) return;
        wake_.notify_all();
        reply_cv_.notify_all(); /* release anyone blocked in request() */
        if (thread_.joinable()) thread_.join();
        irq_.close();
        spi_.reset();
    }

    bool is_running() const override { return running_; }

    bool send(uint8_t type, const void* data, size_t len) override {
        if (len > kPayloadMax) return false;

        Frame f;
        f.type = type;
        f.len  = static_cast<uint8_t>(len);
        if (data != nullptr && len > 0) std::memcpy(f.payload.data(), data, len);

        {
            std::lock_guard<std::mutex> lock(tx_mutex_);
            if (tx_queue_.size() >= cfg_.tx_queue_depth) {
                ++stats_.tx_dropped;
                return false;
            }
            tx_queue_.push_back(f);
        }
        wake_.notify_one();
        return true;
    }

    bool request(uint8_t type, const void* data, size_t len, Frame& reply, unsigned timeout_ms,
                 unsigned attempts) override {
        for (unsigned attempt = 0; attempt < std::max(1u, attempts) && running_; ++attempt) {
            {
                std::lock_guard<std::mutex> lock(reply_mutex_);
                pending_type_ = type;
                have_reply_   = false;
                awaiting_     = true;
            }

            if (!send(type, data, len)) {
                std::lock_guard<std::mutex> lock(reply_mutex_);
                awaiting_ = false;
                return false; /* queue full: retrying will not help */
            }

            std::unique_lock<std::mutex> lock(reply_mutex_);
            const bool got = reply_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                                [this] { return have_reply_ || !running_; });
            awaiting_ = false;

            if (got && have_reply_) {
                reply = pending_reply_;
                if (attempt > 0) {
                    std::lock_guard<std::mutex> slock(stats_mutex_);
                    ++stats_.request_retries;
                }
                return true;
            }
        }

        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            ++stats_.request_timeouts;
        }
        return false;
    }

    void on_frame(FrameCallback cb) override { on_frame_ = std::move(cb); }
    void on_error(ErrorCallback cb) override { on_error_ = std::move(cb); }
    void on_raw(RawCallback cb) override { on_raw_ = std::move(cb); }

    void poke() override { wake_.notify_one(); }

    Stats stats() const override {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        return stats_;
    }

private:
    void report(const std::string& msg) {
        if (on_error_) on_error_(msg);
    }

    static void build_frame(uint8_t* out, uint8_t type, const uint8_t* payload, uint8_t len, bool more) {
        std::memset(out, 0, kFrameSize);
        out[kOffMagic] = kMagic;
        out[kOffType]  = type;
        out[kOffLen]   = len;
        out[kOffFlags] = more ? kFlagMore : 0;
        if (payload != nullptr && len > 0) std::memcpy(out + kOffPayload, payload, len);

        const uint16_t crc = crc16_ccitt(out, kOffCrc);
        out[kOffCrc]     = static_cast<uint8_t>(crc & 0xFF);
        out[kOffCrc + 1] = static_cast<uint8_t>(crc >> 8);
    }

    bool pop_tx(Frame& out, bool& more) {
        std::lock_guard<std::mutex> lock(tx_mutex_);
        if (tx_queue_.empty()) return false;
        out = tx_queue_.front();
        tx_queue_.pop_front();
        more = !tx_queue_.empty();
        return true;
    }

    bool tx_waiting() const {
        std::lock_guard<std::mutex> lock(tx_mutex_);
        return !tx_queue_.empty();
    }

    /* Clocks the frame as one 1-byte transfer per byte, pulsing CS between
     * each. A CPHA=0 PL022 slave frames every byte off that edge, so without
     * this it accepts the first byte of a burst and ignores the rest.
     *
     * Issued in chunks: see Config::cs_chunk_bytes for why one ioctl per frame
     * does not fit. CS deasserts at the end of each message anyway, which is
     * the pulse the slave wants, so chunk boundaries need no special care. */
    bool transfer_cs_per_byte(const uint8_t* tx, uint8_t* rx) {
        const size_t chunk = cfg_.cs_chunk_bytes > 0 ? cfg_.cs_chunk_bytes : kFrameSize;

        for (size_t base = 0; base < kFrameSize; base += chunk) {
            const size_t count = std::min(chunk, kFrameSize - base);
            std::vector<spi_ioc_transfer> tr(count);

            for (size_t i = 0; i < count; ++i) {
                tr[i].tx_buf        = reinterpret_cast<uintptr_t>(&tx[base + i]);
                tr[i].rx_buf        = reinterpret_cast<uintptr_t>(&rx[base + i]);
                tr[i].len           = 1;
                tr[i].speed_hz      = cfg_.spi_speed_hz;
                tr[i].bits_per_word = cfg_.spi_bits;
                /* Pulse CS between bytes. On the final transfer of a message,
                 * cs_change would instead HOLD it asserted, so leave it clear
                 * and let the message end release the line. */
                tr[i].cs_change = (i + 1 < count) ? 1 : 0;
            }

            /* SPI_IOC_MESSAGE(N) needs a compile-time N -- with a variable it
             * expands to a VLA. Build the same request number by hand. */
            const unsigned msg_bytes = static_cast<unsigned>(count * sizeof(spi_ioc_transfer));
            if (msg_bytes >= (1u << _IOC_SIZEBITS)) {
                errno = EMSGSIZE;
                return false;
            }
            const unsigned long request = _IOC(_IOC_WRITE, SPI_IOC_MAGIC, 0, msg_bytes);

            if (::ioctl(spi_->fd(), request, tr.data()) < 0) {
                return false;
            }
        }
        return true;
    }

    /* One full-duplex frame. Returns true if the peer says it has more. */
    bool exchange(bool& peer_more) {
        std::array<uint8_t, kFrameSize> tx{};
        std::array<uint8_t, kFrameSize> rx{};

        Frame outgoing;
        bool  more_queued = false;
        const bool sending = pop_tx(outgoing, more_queued);

        if (sending) {
            build_frame(tx.data(), outgoing.type, outgoing.payload.data(), outgoing.len, more_queued);
        } else {
            tx = idle_frame_;
        }

        try {
            if (cfg_.cs_per_byte) {
                if (!transfer_cs_per_byte(tx.data(), rx.data())) {
                    throw std::runtime_error(std::string("SPI cs-per-byte transfer failed: ") + strerror(errno));
                }
            } else {
                spi_->transfer(tx.data(), rx.data(), kFrameSize);
            }
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            ++stats_.spi_errors;
            report(e.what());
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            ++stats_.exchanges;
            if (sending) ++stats_.frames_tx;
        }

        peer_more = false;
        return parse(rx.data(), peer_more);
    }

    bool parse(const uint8_t* frame, bool& peer_more) {
        if (on_raw_) on_raw_(frame, kFrameSize);

        if (frame[kOffMagic] != kMagic || frame[kOffLen] > kPayloadMax) {
            /* An all-zero frame means nothing is driving MISO: either the slave
             * has not armed its TX DMA, or the line is not connected. Counted
             * apart from corruption, which points somewhere else entirely. */
            bool blank = true;
            for (size_t i = 0; i < kFrameSize; ++i) {
                if (frame[i] != 0x00) { blank = false; break; }
            }
            std::lock_guard<std::mutex> lock(stats_mutex_);
            if (blank) {
                ++stats_.blank_frames;
            } else {
                ++stats_.crc_errors;
            }
            return false;
        }

        const uint16_t want = static_cast<uint16_t>(frame[kOffCrc] |
                                                    (static_cast<uint16_t>(frame[kOffCrc + 1]) << 8));
        if (want != crc16_ccitt(frame, kOffCrc)) {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            ++stats_.crc_errors;
            return false;
        }

        peer_more = (frame[kOffFlags] & kFlagMore) != 0;
        if (frame[kOffType] == kTypeIdle) return true;

        Frame f;
        f.type  = frame[kOffType];
        f.len   = frame[kOffLen];
        f.flags = frame[kOffFlags];
        std::memcpy(f.payload.data(), frame + kOffPayload, kPayloadMax);

        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            ++stats_.frames_rx;
        }

        /* Hand a reply to whoever is blocked in request() rather than to the
         * general callback, so a command's answer cannot be mistaken for an
         * unsolicited event. */
        if (f.flags & kFlagReply) {
            std::lock_guard<std::mutex> lock(reply_mutex_);
            if (awaiting_ && pending_type_ == f.type) {
                pending_reply_ = f;
                have_reply_    = true;
                reply_cv_.notify_all();
                return true;
            }
        }

        if (on_frame_) on_frame_(f);
        return true;
    }

    void apply_thread_prio() {
        if (cfg_.thread_prio <= 0) return;
        sched_param param{};
        param.sched_priority = cfg_.thread_prio;
        if (::pthread_setschedparam(::pthread_self(), SCHED_FIFO, &param) != 0) {
            report("SCHED_FIFO not granted; running at default priority");
        }
    }

    void run() {
        apply_thread_prio();

        /* Clear anything the northbridge queued before we attached. */
        bool peer_more = false;
        while (exchange(peer_more) && peer_more) {}

        while (running_) {
            const int timeout = cfg_.poll_interval_ms > 0
                                    ? static_cast<int>(cfg_.poll_interval_ms)
                                    : -1;

            if (irq_.valid()) {
                if (irq_.wait(timeout)) {
                    std::lock_guard<std::mutex> lock(stats_mutex_);
                    ++stats_.irq_events;
                }
            } else {
                std::unique_lock<std::mutex> lock(tx_mutex_);
                wake_.wait_for(lock, std::chrono::milliseconds(timeout > 0 ? timeout : 20),
                               [this] { return !tx_queue_.empty() || !running_; });
            }

            if (!running_) break;

            /* Keep exchanging while either side still has something: the IRQ
             * line stays asserted, or the peer set the MORE flag. */
            do {
                peer_more = false;
                if (!exchange(peer_more)) break;
            } while (running_ && (peer_more || irq_.asserted() || tx_waiting()));
        }
    }

    Config cfg_;
    std::unique_ptr<southbridge::SpiDevice> spi_;
    IrqLine irq_;

    std::array<uint8_t, kFrameSize> idle_frame_{};

    std::thread             thread_;
    std::atomic<bool>       running_{false};
    std::condition_variable wake_;

    mutable std::mutex tx_mutex_;
    std::deque<Frame>  tx_queue_;

    mutable std::mutex stats_mutex_;
    Stats              stats_;

    /* Rendezvous for request(). */
    std::mutex              reply_mutex_;
    std::condition_variable reply_cv_;
    Frame                   pending_reply_;
    uint8_t                 pending_type_ = kTypeIdle;
    bool                    awaiting_     = false;
    bool                    have_reply_   = false;

    FrameCallback on_frame_;
    ErrorCallback on_error_;
    RawCallback   on_raw_;
};

} // namespace

uint16_t crc16_ccitt(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc = static_cast<uint16_t>(crc ^ (static_cast<uint16_t>(data[i]) << 8));
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                                 : static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

bool PanelState::decode(const Frame& f, PanelState& out) {
    if (f.type != kTypePanel || f.len < 7) return false;

    out.chip    = f.payload[0];
    out.pin     = f.payload[1];
    out.active  = f.payload[2] != 0;
    out.buttons = static_cast<uint32_t>(f.payload[3]) |
                  (static_cast<uint32_t>(f.payload[4]) << 8) |
                  (static_cast<uint32_t>(f.payload[5]) << 16);
    out.toggles = f.payload[6];
    return true;
}

std::unique_ptr<Link> Link::create(Config cfg) {
    return std::make_unique<LinkImpl>(std::move(cfg));
}

} // namespace northbridge
