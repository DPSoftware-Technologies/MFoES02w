/*  nb_link_test — smoke test for the northbridge SPI link on the Pi.
 *
 *  Prints every frame the northbridge sends, decodes panel changes, and
 *  mirrors the low four buttons back onto its LEDs. Prints a stats line every
 *  two seconds so a dead link is obvious.
 *
 *  Needs dtoverlay=spi1-3cs in /boot/firmware/config.txt (creates
 *  /dev/spidev1.0 on GPIO 18) and read access to /dev/gpiochip0.
 *
 *  Reading the stats line:
 *    exch climbing, rx stuck at 0  -> data lines crossed, or the northbridge
 *                                     is not running
 *    crc_err climbing              -> clock too fast or poor signal integrity
 *    irq stuck at 0 but rx moving  -> IRQ line not wired; polling is carrying it
 *
 *  Usage: nb_link_test [spidev] [speed_hz]                                  */

#include "northbridge.h"

#include <atomic>
#include <chrono>
#include <cinttypes>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

namespace {

std::atomic<bool> g_stop{false};

void on_signal(int) { g_stop = true; }

} // namespace

int main(int argc, char** argv) {
    northbridge::Config cfg;
    if (argc > 1) cfg.spi_device = argv[1];
    if (argc > 2) cfg.spi_speed_hz = static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 0));
    /* Third arg "noirq" ignores the IRQ line and polls instead. */
    if (argc > 3 && std::string(argv[3]) == "noirq") cfg.use_irq = false;
    /* Fourth arg: "csbyte" forces CS-per-byte at mode 0 (the default on the
     * aux SPI). A number instead selects that SPI mode and holds CS for the
     * whole frame, which needs a controller that supports CPHA=1. */
    if (argc > 4) {
        if (std::string(argv[4]) == "csbyte") {
            cfg.cs_per_byte = true;
            cfg.spi_mode = 0;
        } else {
            cfg.spi_mode = static_cast<uint8_t>(std::strtoul(argv[4], nullptr, 0));
            cfg.cs_per_byte = false;
        }
    }

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    auto link = northbridge::Link::create(cfg);

    link->on_error([](const std::string& msg) { std::fprintf(stderr, "link: %s\n", msg.c_str()); });

    /* Dump the first few raw reads so bringup shows real bytes, not a verdict.
     * all 00 = nothing drives MISO, all ff = line idle high and undriven,
     * shifted/garbage = clock or CS trouble. */
    static int dumps_left = 5;
    link->on_raw([](const uint8_t* frame, size_t len) {
        if (dumps_left-- <= 0) return;
        std::printf("raw:");
        for (size_t i = 0; i < len && i < 24; ++i) std::printf(" %02x", frame[i]);
        std::printf("\n");
    });

    link->on_frame([&link](const northbridge::Frame& f) {
        northbridge::PanelState panel;
        if (northbridge::PanelState::decode(f, panel)) {
            std::printf("panel chip%u p%02u %s buttons=%06" PRIx32 " toggles=%x\n", panel.chip, panel.pin,
                        panel.active ? "down" : "up", panel.buttons, panel.toggles);

            /* Echo the low four buttons onto the panel LEDs. */
            const uint8_t leds = static_cast<uint8_t>(panel.buttons & 0x0F);
            link->send(northbridge::kTypeLeds, &leds, 1);
            return;
        }

        northbridge::EncoderState enc;
        if (northbridge::EncoderState::decode(f, enc)) {
            if (enc.button) {
                std::printf("encoder button %s pos=%" PRId32 "\n", enc.pressed ? "down" : "up", enc.position);
            } else {
                std::printf("encoder %+d pos=%" PRId32 " btn=%s\n", enc.delta, enc.position,
                            enc.pressed ? "down" : "up");
            }
            return;
        }

        if (f.type == 0x01 && f.len >= 4) {
            uint32_t uptime = 0;
            std::memcpy(&uptime, f.payload.data(), sizeof(uptime));
            std::printf("ping from northbridge, its uptime %" PRIu32 " s\n", uptime);
            return;
        }

        std::printf("frame type=0x%02x len=%u:", f.type, f.len);
        for (uint8_t i = 0; i < f.len && i < 16; ++i) std::printf(" %02x", f.payload[i]);
        std::printf("\n");
    });

    if (!link->start()) {
        std::fprintf(stderr, "failed to open %s\n", cfg.spi_device.c_str());
        return 1;
    }

    std::printf("linked on %s @ %u Hz mode %u%s, IRQ on %s line %u\n", cfg.spi_device.c_str(), cfg.spi_speed_hz,
                cfg.spi_mode, cfg.cs_per_byte ? " cs-per-byte" : "", cfg.gpio_chip.c_str(), cfg.irq_line);

    /* Identify the peer before anything else, so the log says what it is
     * talking to and whether the protocol versions agree. */
    {
        northbridge::Frame reply;
        NbInfoWire info{};
        if (link->request(northbridge::kTypeGetInfo, nullptr, 0, reply) && northbridge::payload_of(reply, info)) {
            std::printf("northbridge fw %u.%u.%u, protocol %u (host speaks %u)\n", info.fw_major, info.fw_minor,
                        info.fw_patch, info.proto_version, NB_PROTO_VERSION);
            std::printf("  built %04u-%02u-%02u %02u:%02u:%02u, up %" PRIu32 " s\n", info.build_year, info.build_month,
                        info.build_day, info.build_hour, info.build_minute, info.build_second, info.uptime_s);
            std::printf("  board id %016" PRIx64 ", rtc=%s panel=%s encoder=%s selftest=%s\n", info.board_id,
                        (info.features & NB_FEAT_RTC) ? "yes" : "no", (info.features & NB_FEAT_PANEL) ? "yes" : "no",
                        (info.features & NB_FEAT_ENCODER) ? "yes" : "no",
                        (info.features & NB_FEAT_POST_OK) ? "pass" : "FAIL");

            if (info.proto_version != NB_PROTO_VERSION) {
                std::fprintf(stderr, "WARNING: protocol mismatch, payload layouts may differ\n");
            }
        } else {
            std::fprintf(stderr, "northbridge did not answer GET_INFO\n");
        }
    }

    uint32_t seq = 0;
    while (!g_stop) {
        std::this_thread::sleep_for(std::chrono::seconds(2));

        /* Request/reply: one blocking call, answered by a handler over there. */
        ++seq;
        northbridge::Frame reply;
        if (link->request(northbridge::kTypePing, &seq, sizeof(seq), reply)) {
            uint32_t echoed = 0;
            std::memcpy(&echoed, reply.payload.data(), sizeof(echoed));
            std::printf("ping round trip ok, echoed %" PRIu32 "\n", echoed);
        } else {
            std::printf("ping timed out\n");
        }

        NbPanelWire panel{};
        if (link->request(northbridge::kTypeGetPanel, nullptr, 0, reply) &&
            northbridge::payload_of(reply, panel)) {
            std::printf("panel: buttons=%06" PRIx32 " toggles=%x leds=%x\n", panel.buttons, panel.toggles, panel.leds);
        }

        NbEncoderWire encw{};
        if (link->request(northbridge::kTypeGetEnc, nullptr, 0, reply) && northbridge::payload_of(reply, encw)) {
            std::printf("encoder: pos=%" PRId32 " btn=%s\n", encw.position, encw.pressed ? "down" : "up");
        }

        NbTimeWire now{};
        if (link->request(northbridge::kTypeGetTime, nullptr, 0, reply) && northbridge::payload_of(reply, now)) {
            std::printf("time: %04u-%02u-%02u %02u:%02u:%02u dow=%u  %d.%02d C\n", now.year, now.month, now.day,
                        now.hour, now.minute, now.second, now.weekday, now.temp_centi / 100,
                        (now.temp_centi < 0 ? -now.temp_centi : now.temp_centi) % 100);
        }

        const northbridge::Stats s = link->stats();
        std::printf("rx=%" PRIu64 " tx=%" PRIu64 " exch=%" PRIu64 " irq=%" PRIu64 " blank=%" PRIu64
                    " crc_err=%" PRIu64 " spi_err=%" PRIu64 " tx_drop=%" PRIu64 "\n",
                    s.frames_rx, s.frames_tx, s.exchanges, s.irq_events, s.blank_frames, s.crc_errors,
                    s.spi_errors, s.tx_dropped);
    }

    link->stop();
    std::printf("stopped\n");
    return 0;
}
