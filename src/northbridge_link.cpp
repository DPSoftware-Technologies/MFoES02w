/*  App side of the northbridge link.
 *
 *  The northbridge is a Pico on SPI1 carrying the control panel (24 buttons,
 *  4 toggles, 4 LEDs) and a DS3231. This file owns the connection, mirrors
 *  panel state into the app, and keeps the clocks in step.
 *
 *  Clock policy, and why it matters here: the Pi Zero 2W has no RTC of its own,
 *  so at boot its clock is whatever the filesystem last recorded -- hence the
 *  "clock skew detected" on startup. The DS3231 on the northbridge is the only
 *  real time source until NTP lands, so:
 *
 *    boot   RTC -> system clock, if the RTC looks sane and the system does not
 *    later  system clock -> RTC, but ONLY if something else (NTP) moved it,
 *           so a drifting system clock can never corrupt the RTC
 *
 *  Times on the wire are UTC. The RTC holds UTC too, so a timezone change never
 *  rewrites the chip.                                                        */

#include "app.h"

#ifndef DESKTOP

#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/time.h>

namespace {

/* Below this year a clock is assumed unset rather than merely wrong. */
constexpr int kSaneYear = 2025;

/* How far the two clocks may drift before we correct the RTC. */
constexpr long kClockSkewToleranceSec = 30;

/* How often to compare the clocks once running. */
constexpr uint32_t kClockCheckIntervalMs = 60u * 60u * 1000u;

bool wire_to_tm(const NbTimeWire& w, struct tm& out) {
    if (w.year < 1970 || w.month < 1 || w.month > 12 || w.day < 1 || w.day > 31) return false;
    if (w.hour > 23 || w.minute > 59 || w.second > 59) return false;

    std::memset(&out, 0, sizeof(out));
    out.tm_year = w.year - 1900;
    out.tm_mon  = w.month - 1;
    out.tm_mday = w.day;
    out.tm_hour = w.hour;
    out.tm_min  = w.minute;
    out.tm_sec  = w.second;
    return true;
}

void tm_to_wire(const struct tm& t, NbTimeWire& out) {
    std::memset(&out, 0, sizeof(out));
    out.year    = static_cast<uint16_t>(t.tm_year + 1900);
    out.month   = static_cast<uint8_t>(t.tm_mon + 1);
    out.day     = static_cast<uint8_t>(t.tm_mday);
    out.hour    = static_cast<uint8_t>(t.tm_hour);
    out.minute  = static_cast<uint8_t>(t.tm_min);
    out.second  = static_cast<uint8_t>(t.tm_sec);
    out.weekday = static_cast<uint8_t>(t.tm_wday + 1); /* firmware recomputes it anyway */
}

} // namespace

bool App::initNorthbridge() {
    northbridge::Config cfg;
    /* Defaults already match this board: SPI1 CE0 at mode 0 with CS pulsed per
     * byte, which is what the Pico's PL022 needs and what the Pi's auxiliary
     * SPI can actually do. */
    nb = northbridge::Link::create(cfg);

    nb->on_error([this](const std::string& msg) {
        pthread_mutex_lock(&frameMutex);
        snprintf(statusMsg, sizeof(statusMsg), "NB: %s", msg.c_str());
        pthread_mutex_unlock(&frameMutex);
        RRFSYSMSG = true;
    });

    /* Runs on the link thread. Hand everything to the main thread rather than
     * touching UI or GFX from here. */
    nb->on_frame([this](const northbridge::Frame& f) {
        if (f.type == northbridge::kTypePanel) {
            northbridge::PanelState panel;
            if (!northbridge::PanelState::decode(f, panel)) return;
            postAction([this, panel]() { nbOnPanelEvent(panel); });
            return;
        }

        if (f.type == northbridge::kTypeEncEvt) {
            northbridge::EncoderState enc;
            if (!northbridge::EncoderState::decode(f, enc)) return;
            postAction([this, enc]() { nbOnEncoderEvent(enc); });
        }
    });

    if (!nb->start()) {
        snprintf(statusMsg, sizeof(statusMsg), "NB: link failed on %s", cfg.spi_device.c_str());
        RRFSYSMSG = true;
        nb.reset();
        return false;
    }

    /* Identify the peer. A protocol mismatch means the payload layouts may
     * disagree, so say so loudly rather than decoding garbage. */
    northbridge::Frame reply;
    if (nb->request(northbridge::kTypeGetInfo, nullptr, 0, reply) && northbridge::payload_of(reply, nbInfo)) {
        nbOnline = true;
        printf("northbridge fw %u.%u.%u proto %u, board %016llx, rtc=%d panel=%d enc=%d post=%s\n", nbInfo.fw_major,
               nbInfo.fw_minor, nbInfo.fw_patch, nbInfo.proto_version, (unsigned long long)nbInfo.board_id,
               (nbInfo.features & NB_FEAT_RTC) ? 1 : 0, (nbInfo.features & NB_FEAT_PANEL) ? 1 : 0,
               (nbInfo.features & NB_FEAT_ENCODER) ? 1 : 0, (nbInfo.features & NB_FEAT_POST_OK) ? "pass" : "FAIL");

        snprintf(statusMsg, sizeof(statusMsg), "NB fw %u.%u.%u online", nbInfo.fw_major, nbInfo.fw_minor,
                 nbInfo.fw_patch);

        if (nbInfo.proto_version != NB_PROTO_VERSION) {
            printf("northbridge protocol %u, host speaks %u -- payloads may not match\n", nbInfo.proto_version,
                   NB_PROTO_VERSION);
        }
    } else {
        snprintf(statusMsg, sizeof(statusMsg), "NB: no answer to GET_INFO");
        printf("northbridge did not answer GET_INFO\n");
    }
    RRFSYSMSG = true;

    if (nbOnline && (nbInfo.features & NB_FEAT_RTC)) {
        nbAdoptRtcTime();
    }

    /* Seed the cached panel state so the UI has something before the first
     * button is touched. */
    if (nbOnline && nb->request(northbridge::kTypeGetPanel, nullptr, 0, reply)) {
        northbridge::payload_of(reply, nbPanel);
    }

    /* Same for the encoder, so the first turn is measured against a known
     * position rather than against zero. */
    if (nbOnline && nb->request(northbridge::kTypeGetEnc, nullptr, 0, reply)) {
        NbEncoderWire wire{};
        if (northbridge::payload_of(reply, wire)) {
            nbEnc.position = wire.position;
            nbEnc.pressed  = wire.pressed != 0;
        }
    }

    /* The firmware's power-on test has already lit the LEDs and left them dark
     * again. From here they are ours: state them explicitly so what the panel
     * shows and what nbLeds says cannot disagree. */
    if (nbOnline) nbSetLeds(0);

    return true;
}

/* Panel LEDs, bit 0 = first LED. Fire and forget: the firmware sends no reply
 * to SET_LEDS, so a lost frame is corrected by the next call. */
bool App::nbSetLeds(uint8_t mask) {
    if (!nb || !nbOnline) return false;

    mask &= 0x0f;
    if (!nb->send(northbridge::kTypeLeds, &mask, sizeof(mask))) return false;

    nbLeds = mask;
    return true;
}

void App::nbOnPanelEvent(const northbridge::PanelState& panel) {
    nbPanel.buttons = panel.buttons;
    nbPanel.toggles = panel.toggles;

    const char* kind = (panel.chip == 1 && panel.pin >= 8) ? "toggle" : "button";
    pthread_mutex_lock(&frameMutex);
    snprintf(statusMsg, sizeof(statusMsg), "%s %u.%02u %s  (buttons %06lx toggles %x)", kind, panel.chip, panel.pin,
             panel.active ? "down" : "up", (unsigned long)panel.buttons, panel.toggles);
    pthread_mutex_unlock(&frameMutex);
    RRFSYSMSG = true;

    /* Short blip on press, same feedback the touchscreen gives. */
    if (panel.active) {
        std::thread([this]() { buz.set(1); usleep(15000); buz.set(0); }).detach();
    }

    /* The northbridge no longer mirrors buttons onto the LEDs by itself, so the
     * panel lights up only because we drive it. Replace this with whatever the
     * LEDs should really mean. */
    nbSetLeds(static_cast<uint8_t>(panel.buttons & 0x0f));
}

void App::nbOnEncoderEvent(const northbridge::EncoderState& enc) {
    nbEnc = enc;

    pthread_mutex_lock(&frameMutex);
    if (enc.button) {
        snprintf(statusMsg, sizeof(statusMsg), "encoder button %s  (pos %ld)", enc.pressed ? "down" : "up",
                 (long)enc.position);
    } else {
        snprintf(statusMsg, sizeof(statusMsg), "encoder %+d  (pos %ld)", enc.delta, (long)enc.position);
    }
    pthread_mutex_unlock(&frameMutex);
    RRFSYSMSG = true;

    if (enc.button && enc.pressed) {
        std::thread([this]() { buz.set(1); usleep(15000); buz.set(0); }).detach();
    }
}

bool App::nbReadTime(NbTimeWire& out, unsigned timeout_ms, unsigned attempts) {
    if (!nb || !nbOnline) return false;

    northbridge::Frame reply;
    if (!nb->request(northbridge::kTypeGetTime, nullptr, 0, reply, timeout_ms, attempts)) return false;
    return northbridge::payload_of(reply, out);
}

bool App::nbAdoptRtcTime() {
    NbTimeWire rtc{};
    if (!nbReadTime(rtc)) {
        printf("northbridge: RTC unreadable, leaving system clock alone\n");
        return false;
    }

    if (rtc.year < kSaneYear) {
        printf("northbridge: RTC reads %04u, treating as unset\n", rtc.year);
        return false;
    }

    const time_t now = time(nullptr);
    struct tm sys{};
    gmtime_r(&now, &sys);

    /* A system clock that already looks plausible is left alone: it may have
     * come from NTP, which beats the RTC. */
    if (sys.tm_year + 1900 >= kSaneYear) {
        printf("northbridge: system clock already set, keeping it\n");
        return false;
    }

    struct tm t{};
    if (!wire_to_tm(rtc, t)) return false;

    const time_t epoch = timegm(&t);
    if (epoch <= 0) return false;

    const struct timeval tv{epoch, 0};
    if (settimeofday(&tv, nullptr) != 0) {
        printf("northbridge: settimeofday failed (%s), need root\n", strerror(errno));
        return false;
    }

    nbClockFromRtc = true;
    printf("northbridge: system clock set from RTC, %04u-%02u-%02u %02u:%02u:%02u UTC\n", rtc.year, rtc.month, rtc.day,
           rtc.hour, rtc.minute, rtc.second);
    return true;
}

bool App::nbPushSystemTime() {
    if (!nb || !nbOnline) return false;

    const time_t now = time(nullptr);
    struct tm sys{};
    gmtime_r(&now, &sys);
    if (sys.tm_year + 1900 < kSaneYear) return false;

    NbTimeWire wire{};
    tm_to_wire(sys, wire);

    northbridge::Frame reply;
    NbStatusWire status{};
    if (!nb->request(northbridge::kTypeSetTime, &wire, sizeof(wire), reply)) return false;
    if (!northbridge::payload_of(reply, status) || status.code != NB_OK) {
        printf("northbridge: SET_TIME refused, code %u\n", status.code);
        return false;
    }

    printf("northbridge: RTC set to %04u-%02u-%02u %02u:%02u:%02u UTC\n", wire.year, wire.month, wire.day, wire.hour,
           wire.minute, wire.second);
    return true;
}

void App::nbService() {
    if (!nb || !nbOnline || !(nbInfo.features & NB_FEAT_RTC)) return;

    const uint32_t nowMs = (uint32_t)(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::high_resolution_clock::now().time_since_epoch())
                                          .count() &
                                      0xFFFFFFFF);

    if (nbLastClockCheck != 0 && (nowMs - nbLastClockCheck) < kClockCheckIntervalMs) return;
    nbLastClockCheck = nowMs;

    /* Only correct the RTC when the system clock moved on its own, i.e. NTP
     * fixed it. If we were the ones who set the system clock from the RTC,
     * writing it back would just feed the RTC its own drift. */
    if (nbClockFromRtc) return;

    /* One quick attempt: this runs on the render thread, and the hourly clock
     * check is not worth a multi-attempt stall in front of the user. If it
     * misses, the next hour picks it up. */
    NbTimeWire rtc{};
    if (!nbReadTime(rtc, 120, 1)) return;

    struct tm t{};
    if (!wire_to_tm(rtc, t)) return;

    const time_t rtcEpoch = timegm(&t);
    const time_t sysEpoch = time(nullptr);
    if (rtcEpoch <= 0 || sysEpoch <= 0) return;

    const long skew = (long)(sysEpoch > rtcEpoch ? sysEpoch - rtcEpoch : rtcEpoch - sysEpoch);
    if (skew > kClockSkewToleranceSec) {
        printf("northbridge: RTC off by %ld s, correcting it\n", skew);
        nbPushSystemTime();
    }
}

#endif // !DESKTOP
