#pragma once
#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <functional>
#include <atomic>

namespace southbridge {

/*  stats snapshot  */
struct Stats {
    uint64_t frames_sent;
    uint64_t frames_dropped;   /* ring full on RPi side              */
    uint64_t pico_overflows;   /* Pico reported overflow             */
    uint64_t pico_underruns;   /* Pico reported underrun             */
    uint64_t crc_errors;       /* CRC mismatch detected by RPi       */
    uint64_t seq_gaps;         /* discontinuity in sequence numbers  */
    double   ring_fill_pct;    /* 0–100, current audio ring usage    */
};

/*  event callback types  */
using CommandCallback = std::function<void(const std::string& json)>;
using ErrorCallback   = std::function<void(const std::string& msg)>;

/*  configuration  */
struct Config {
    /* SPI */
    uint32_t    spi_speed_hz     = 25'000'000;   /* 25 MHz               */
    uint8_t     spi_mode         = 0;            /* CPOL=0, CPHA=0       */
    uint8_t     spi_bits         = 8;

    /* CS lines (as /dev/spidev0.X) */
    std::string audio_device     = "/dev/spidev0.0";   /* SPI0 CE0 = GPIO 8  */
    std::string cmd_device       = "/dev/spidev1.0";   /* SPI1 CE2 = GPIO 16 */

    /* realtime thread priorities (SCHED_FIFO) */
    int         tx_thread_prio   = 85;
    int         cmd_thread_prio  = 60;

    /* CPU affinity (core index, -1 = don't pin) */
    int         tx_thread_cpu    = 3;   /* last core, avoid core 0       */
    int         cmd_thread_cpu   = 2;

    /* audio ring buffer size in frames */
    uint32_t    ring_capacity    = 64;  /* ~512 ms at 16 kHz/128 smp/frm */

    /* if ring is this full (%), drop oldest instead of newest */
    float       overflow_drop_threshold = 90.0f;

    /* silence-fill when ring is empty (vs stalling) */
    bool        send_silence_on_underrun = true;

    /* inter-frame delay (µs) – 0 = max throughput */
    uint32_t    inter_frame_us   = 0;
};

/*  main class  */
class Southbridge {
public:
    virtual ~Southbridge() = default;

    /* factory */
    static std::unique_ptr<Southbridge> create(
        const std::string& audio_device = "/dev/spidev0.0",
        Config cfg = {});

    /* lifecycle */
    virtual bool start() = 0;
    virtual void stop()  = 0;
    virtual bool is_running() const = 0;

    /*
     * send_audio() – enqueue PCM samples (16-bit signed, native endian).
     * n_samples = number of int16_t values (per channel).
     * Thread-safe, non-blocking.
     * Returns number of samples actually enqueued (< n if ring is full).
     */
    virtual size_t send_audio(const int16_t* pcm, size_t n_samples) = 0;

    /*
     * send_command() – send a JSON command to the Pico (CS1 channel).
     * json must be ≤ 60 bytes.  Blocks max ~1 ms.
     * Returns false if Pico is busy or json too large.
     */
    virtual bool send_command(const std::string& json) = 0;

    /* register callback for commands/events arriving from the Pico */
    virtual void on_command(CommandCallback cb) = 0;
    virtual void on_error  (ErrorCallback   cb) = 0;

    /* stats (thread-safe snapshot) */
    virtual Stats stats() const = 0;

    /* drain the ring and wait for Pico to consume everything (max wait_ms) */
    virtual bool flush(uint32_t wait_ms = 500) = 0;

    /* reset sequence counter and clear ring */
    virtual void reset() = 0;
};

} // namespace southbridge