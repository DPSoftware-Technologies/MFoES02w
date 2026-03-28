/*
 * southbridge.cpp – RPi02W implementation
 *
 * Threads:
 *   tx_thread_   – SCHED_FIFO, pulls audio frames from ring, SPI transfer
 *   cmd_thread_  – SCHED_FIFO, polls command channel (CS1)
 *
 * Audio path:
 *   send_audio() → sample FIFO → frame assembly → ring_buffer → tx_thread → SPI
 *
 * Overflow policy:
 *   If ring > overflow_drop_threshold %, push_overwrite (drop oldest).
 *   On Pico OVERFLOW status, increment counter; TX continues (Pico self-recovers).
 */

#include "southbridge.h"
#include "ring_buffer.h"
#include "spi_device.h"
#include "protocol.h"

#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <cstring>
#include <atomic>
#include <stdexcept>
#include <sched.h>
#include <pthread.h>
#include <unistd.h>

/* helper: set thread to SCHED_FIFO at given priority */
static void set_realtime(pthread_t thr, int prio, int cpu) {
    struct sched_param sp{};
    sp.sched_priority = prio;
    pthread_setschedparam(thr, SCHED_FIFO, &sp);

    if (cpu >= 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpu, &cpuset);
        pthread_setaffinity_np(thr, sizeof(cpuset), &cpuset);
    }
}

namespace southbridge {

/*  */
class SouthbridgeImpl final : public Southbridge {
public:
    SouthbridgeImpl(const std::string& audio_dev, Config cfg)
        : cfg_(cfg)
        , ring_(next_pow2(cfg.ring_capacity))
        , running_(false)
        , seq_(0)
    {
        /* Audio SPI */
        audio_spi_ = std::make_unique<SpiDevice>(
            audio_dev, cfg_.spi_speed_hz, cfg_.spi_mode, cfg_.spi_bits);

        /* Command SPI (optional – open only if device exists) */
        try {
            cmd_spi_ = std::make_unique<SpiDevice>(
                cfg_.cmd_device, 1'000'000, 0, 8);   /* cmd at 1 MHz */
        } catch (...) {
            /* cmd channel not wired – silently disabled */
        }

        /* pre-allocate tx/rx buffers */
        tx_buf_.resize(SB_FRAME_BYTES, 0);
        rx_buf_.resize(SB_FRAME_BYTES, 0xFF);
    }

    ~SouthbridgeImpl() override { stop(); }

    /*  lifecycle  */
    bool start() override {
        if (running_.load()) return true;
        running_.store(true);

        tx_thread_  = std::thread(&SouthbridgeImpl::tx_loop,  this);
        if (cmd_spi_)
            cmd_thread_ = std::thread(&SouthbridgeImpl::cmd_loop, this);

        /* elevate after threads start */
        set_realtime(tx_thread_.native_handle(),
                     cfg_.tx_thread_prio, cfg_.tx_thread_cpu);
        if (cmd_thread_.joinable())
            set_realtime(cmd_thread_.native_handle(),
                         cfg_.cmd_thread_prio, cfg_.cmd_thread_cpu);
        return true;
    }

    void stop() override {
        running_.store(false);
        ring_cv_.notify_all();
        if (tx_thread_.joinable())  tx_thread_.join();
        if (cmd_thread_.joinable()) cmd_thread_.join();
    }

    bool is_running() const override { return running_.load(); }

    /*  audio send  */
    size_t send_audio(const int16_t* pcm, size_t n_samples) override {
        if (!running_.load()) return 0;

        size_t enqueued = 0;
        const size_t frame_samples = SB_AUDIO_SAMPLES_PER_FRAME * SB_CHANNELS;

        while (enqueued + frame_samples <= n_samples) {
            sb_audio_frame_t frame{};
            frame.magic_hi   = SB_FRAME_MAGIC_HI;
            frame.magic_lo   = SB_FRAME_MAGIC_LO;
            frame.frame_type = SB_FTYPE_AUDIO;
            frame.status     = 0;
            frame.seq        = seq_.fetch_add(1, std::memory_order_relaxed);
            std::memcpy(frame.payload,
                        pcm + enqueued,
                        SB_AUDIO_PAYLOAD_BYTES);
            frame.crc16 = sb_crc16(
                reinterpret_cast<const uint8_t*>(&frame),
                offsetof(sb_audio_frame_t, crc16));

            bool ok;
            if (ring_.fill_pct() >= cfg_.overflow_drop_threshold) {
                ring_.push_overwrite(frame);
                stats_.frames_dropped.fetch_add(1, std::memory_order_relaxed);
                ok = true;
            } else {
                ok = ring_.push(frame);
                if (!ok) {
                    stats_.frames_dropped.fetch_add(1, std::memory_order_relaxed);
                }
            }

            if (ok) {
                ring_cv_.notify_one();
                enqueued += frame_samples;
            } else {
                break;
            }
        }
        return enqueued;
    }

    /*  command send  */
    bool send_command(const std::string& json) override {
        if (!cmd_spi_) return false;
        if (json.size() > SB_CMD_FRAME_BYTES - 4) return false;

        sb_cmd_frame_t frame{};
        frame.magic_hi = SB_CMD_MAGIC_HI;
        frame.magic_lo = SB_CMD_MAGIC_LO;
        frame.len      = static_cast<uint8_t>(json.size());
        std::memcpy(frame.payload, json.data(), json.size());

        std::vector<uint8_t> tx(SB_CMD_FRAME_BYTES, 0);
        std::vector<uint8_t> rx(SB_CMD_FRAME_BYTES, 0);
        std::memcpy(tx.data(), &frame, sizeof(frame));

        std::lock_guard<std::mutex> lk(cmd_mutex_);
        cmd_spi_->transfer(tx.data(), rx.data(), SB_CMD_FRAME_BYTES);
        return true;
    }

    void on_command(CommandCallback cb) override {
        std::lock_guard<std::mutex> lk(cb_mutex_);
        cmd_cb_ = std::move(cb);
    }

    void on_error(ErrorCallback cb) override {
        std::lock_guard<std::mutex> lk(cb_mutex_);
        err_cb_ = std::move(cb);
    }

    /*  stats  */
    Stats stats() const override {
        Stats s;
        s.frames_sent    = stats_.frames_sent.load();
        s.frames_dropped = stats_.frames_dropped.load();
        s.pico_overflows = stats_.pico_overflows.load();
        s.pico_underruns = stats_.pico_underruns.load();
        s.crc_errors     = stats_.crc_errors.load();
        s.seq_gaps       = stats_.seq_gaps.load();
        s.ring_fill_pct  = ring_.fill_pct();
        return s;
    }

    bool flush(uint32_t wait_ms) override {
        auto deadline = std::chrono::steady_clock::now()
                      + std::chrono::milliseconds(wait_ms);
        while (!ring_.empty()) {
            if (std::chrono::steady_clock::now() > deadline) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return true;
    }

    void reset() override {
        seq_.store(0);
        sb_audio_frame_t dummy;
        while (ring_.pop(dummy)) {}
        /* send reset frame to Pico */
        sb_audio_frame_t rf{};
        rf.magic_hi   = SB_FRAME_MAGIC_HI;
        rf.magic_lo   = SB_FRAME_MAGIC_LO;
        rf.frame_type = SB_FTYPE_RESET;
        std::memset(tx_buf_.data(), 0, SB_FRAME_BYTES);
        std::memcpy(tx_buf_.data(), &rf, SB_FRAME_HEADER_BYTES);
        std::lock_guard<std::mutex> lk(spi_mutex_);
        audio_spi_->transfer(tx_buf_.data(), rx_buf_.data(), SB_FRAME_BYTES);
    }

private:
    /*  internal stats  */
    struct AtomicStats {
        std::atomic<uint64_t> frames_sent{0};
        std::atomic<uint64_t> frames_dropped{0};
        std::atomic<uint64_t> pico_overflows{0};
        std::atomic<uint64_t> pico_underruns{0};
        std::atomic<uint64_t> crc_errors{0};
        std::atomic<uint64_t> seq_gaps{0};
    };

    Config                           cfg_;
    RingBuffer<sb_audio_frame_t>     ring_;
    std::unique_ptr<SpiDevice>       audio_spi_;
    std::unique_ptr<SpiDevice>       cmd_spi_;

    std::vector<uint8_t>             tx_buf_;
    std::vector<uint8_t>             rx_buf_;

    std::atomic<bool>                running_;
    std::atomic<uint32_t>            seq_;

    std::thread                      tx_thread_;
    std::thread                      cmd_thread_;

    std::mutex                       spi_mutex_;
    std::mutex                       cmd_mutex_;
    std::mutex                       cb_mutex_;
    std::mutex                       ring_wait_mutex_;
    std::condition_variable          ring_cv_;

    CommandCallback                  cmd_cb_;
    ErrorCallback                    err_cb_;
    AtomicStats                      stats_;

    uint32_t                         last_pico_seq_ = 0;

    /*  TX thread loop  */
    void tx_loop() {
        sb_audio_frame_t frame;
        while (running_.load()) {
            /* wait for a frame to be available */
            {
                std::unique_lock<std::mutex> lk(ring_wait_mutex_);
                ring_cv_.wait_for(lk, std::chrono::milliseconds(10),
                    [this]{ return !ring_.empty() || !running_.load(); });
            }

            if (!running_.load()) break;

            bool got = ring_.pop(frame);

            if (!got) {
                /* ring empty: send silence or NOP */
                if (cfg_.send_silence_on_underrun) {
                    std::memset(tx_buf_.data(), 0, SB_FRAME_BYTES);
                    tx_buf_[0] = SB_FRAME_MAGIC_HI;
                    tx_buf_[1] = SB_FRAME_MAGIC_LO;
                    tx_buf_[2] = SB_FTYPE_SILENCE;
                    uint32_t s = seq_.fetch_add(1, std::memory_order_relaxed);
                    std::memcpy(tx_buf_.data() + 4, &s, 4);
                }
                /* else just spin-wait */
                continue;
            }

            /* pack frame into flat buffer (handles alignment padding) */
            std::memset(tx_buf_.data(), 0, SB_FRAME_BYTES);
            std::memcpy(tx_buf_.data(), &frame,
                        std::min(sizeof(frame), (size_t)SB_FRAME_BYTES));

            /* transfer */
            {
                std::lock_guard<std::mutex> lk(spi_mutex_);
                audio_spi_->transfer(tx_buf_.data(), rx_buf_.data(), SB_FRAME_BYTES);
            }

            stats_.frames_sent.fetch_add(1, std::memory_order_relaxed);

            /* parse MISO status byte (offset 3 of returned frame) */
            uint8_t status = rx_buf_[3];
            if (status & SB_STATUS_OVERFLOW)
                stats_.pico_overflows.fetch_add(1, std::memory_order_relaxed);
            if (status & SB_STATUS_UNDERRUN)
                stats_.pico_underruns.fetch_add(1, std::memory_order_relaxed);
            if (status & SB_STATUS_CRC_ERR)
                stats_.crc_errors.fetch_add(1, std::memory_order_relaxed);

            if (status & SB_STATUS_BUSY) {
                /* Pico not ready, back-off and re-queue frame */
                ring_.push_overwrite(frame);
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            }

            if (cfg_.inter_frame_us > 0)
                ::usleep(cfg_.inter_frame_us);
        }
    }

    /*  CMD thread loop  */
    void cmd_loop() {
        std::vector<uint8_t> tx(SB_CMD_FRAME_BYTES, 0xFF);
        std::vector<uint8_t> rx(SB_CMD_FRAME_BYTES, 0);

        while (running_.load()) {
            /* poll every 20 ms */
            std::this_thread::sleep_for(std::chrono::milliseconds(20));

            {
                std::lock_guard<std::mutex> lk(cmd_mutex_);
                cmd_spi_->transfer(tx.data(), rx.data(), SB_CMD_FRAME_BYTES);
            }

            /* check for valid frame from Pico */
            if (rx[0] == SB_CMD_MAGIC_HI && rx[1] == SB_CMD_MAGIC_LO && rx[2] > 0) {
                uint8_t len = rx[2];
                if (len <= SB_CMD_FRAME_BYTES - 4) {
                    std::string json(reinterpret_cast<const char*>(&rx[4]), len);
                    std::lock_guard<std::mutex> lk(cb_mutex_);
                    if (cmd_cb_) cmd_cb_(json);
                }
            }
        }
    }

    /*  helpers  */
    static size_t next_pow2(size_t v) {
        if (v == 0) return 1;
        --v;
        for (size_t s = 1; s < sizeof(v)*8; s <<= 1) v |= v >> s;
        return ++v;
    }
};

/*  factory  */
std::unique_ptr<Southbridge> Southbridge::create(
    const std::string& audio_device, Config cfg)
{
    cfg.audio_device = audio_device;
    return std::make_unique<SouthbridgeImpl>(audio_device, cfg);
}

} // namespace southbridge
