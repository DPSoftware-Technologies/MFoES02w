#pragma once
#include "i2c_dev.h"
#include "gpio_sysfs.h"
#include <cstdint>
#include <cstring>
#include <cmath>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <thread>
#include <atomic>
#include <functional>
#include <mutex>
#include <chrono>
#include <stdexcept>
#include <map>
#include <vector>

// ─── GT911 register map ───────────────────────────────────────────────────────

#define GT911_I2C_ADDR_28   0x14   // INT high during reset
#define GT911_I2C_ADDR_BA   0x5D   // INT low during reset  (default)
#define GT911_MAX_CONTACTS  5

#define GT911_REG_CFG        0x8047
#define GT911_REG_CHECKSUM   0x80FF
#define GT911_REG_DATA       0x8140  // product ID / info base
#define GT911_REG_ID         0x8140
#define GT911_REG_COORD_ADDR 0x814E  // status register
#define GT911_REG_COORD_BASE 0x814F  // first touch point

// ─── Packed structs (match Arduino reference exactly) ────────────────────────

struct __attribute__((packed)) GTInfo {
    char     productId[4];   // 0x8140-0x8143
    uint16_t fwId;           // 0x8144-0x8145
    uint16_t xResolution;    // 0x8146-0x8147
    uint16_t yResolution;    // 0x8148-0x8149
    uint8_t  vendorId;       // 0x814A
};

struct __attribute__((packed)) GTPoint {
    // 8 bytes per point: 0x814F-0x8156, ..., 0x816F-0x8176 (5 points)
    uint8_t  trackId;
    uint16_t x;
    uint16_t y;
    uint16_t area;
    uint8_t  reserved;
};

struct __attribute__((packed)) GTConfig {
    uint8_t  configVersion;        // 0x8047
    uint16_t xResolution;          // 0x8048-0x8049
    uint16_t yResolution;          // 0x804A-0x804B
    uint8_t  touchNumber;          // 0x804C
    uint8_t  moduleSwitch1;        // 0x804D
    uint8_t  moduleSwitch2;        // 0x804E
    uint8_t  shakeCount;           // 0x804F
    uint8_t  filter;               // 0x8050
    uint8_t  largeTouch;           // 0x8051
    uint8_t  noiseReduction;       // 0x8052
    uint8_t  screenTouchLevel;     // 0x8053
    uint8_t  screenLeaveLevel;     // 0x8054
    uint8_t  lowPowerControl;      // 0x8055
    uint8_t  refreshRate;          // 0x8056
    uint8_t  xThreshold;           // 0x8057
    uint8_t  yThreshold;           // 0x8058
    uint8_t  xSpeedLimit;          // 0x8059
    uint8_t  ySpeedLimit;          // 0x805A
    uint8_t  vSpace;               // 0x805B
    uint8_t  hSpace;               // 0x805C
    uint8_t  miniFilter;           // 0x805D
    uint8_t  stretchR0;            // 0x805E
    uint8_t  stretchR1;            // 0x805F
    uint8_t  stretchR2;            // 0x8060
    uint8_t  stretchRM;            // 0x8061
    uint8_t  drvGroupANum;         // 0x8062
    uint8_t  drvGroupBNum;         // 0x8063
    uint8_t  sensorNum;            // 0x8064
    uint8_t  freqAFactor;          // 0x8065
    uint8_t  freqBFactor;          // 0x8066
    uint16_t pannelBitFreq;        // 0x8067-0x8068
    uint16_t pannelSensorTime;     // 0x8069-0x806A
    uint8_t  pannelTxGain;         // 0x806B
    uint8_t  pannelRxGain;         // 0x806C
    uint8_t  pannelDumpShift;      // 0x806D
    uint8_t  drvFrameControl;      // 0x806E
    uint8_t  chargingLevelUp;      // 0x806F
    uint8_t  moduleSwitch3;        // 0x8070
    uint8_t  gestureDis;           // 0x8071
    uint8_t  gestureLongPressTime; // 0x8072
    uint8_t  xySlopeAdjust;        // 0x8073
    uint8_t  gestureControl;       // 0x8074
    uint8_t  gestureSwitch1;       // 0x8075
    uint8_t  gestureSwitch2;       // 0x8076
    uint8_t  gestureRefreshRate;   // 0x8077
    uint8_t  gestureTouchLevel;    // 0x8078
    uint8_t  newGreenWakeUpLevel;  // 0x8079
    uint8_t  freqHoppingStart;     // 0x807A
    uint8_t  freqHoppingEnd;       // 0x807B
    uint8_t  noiseDetectTimes;     // 0x807C
    uint8_t  hoppingFlag;          // 0x807D
    uint8_t  hoppingThreshold;     // 0x807E
    uint8_t  noiseThreshold;       // 0x807F
    uint8_t  noiseMinThreshold;    // 0x8080
    uint8_t  NC_1;                 // 0x8081
    uint8_t  hoppingSensorGroup;   // 0x8082
    uint8_t  hoppingSeg1Normalize; // 0x8083
    uint8_t  hoppingSeg1Factor;    // 0x8084
    uint8_t  mainClockAjdust;      // 0x8085
    uint8_t  hoppingSeg2Normalize; // 0x8086
    uint8_t  hoppingSeg2Factor;    // 0x8087
    uint8_t  NC_2;                 // 0x8088
    uint8_t  hoppingSeg3Normalize; // 0x8089
    uint8_t  hoppingSeg3Factor;    // 0x808A
    uint8_t  NC_3;                 // 0x808B
    uint8_t  hoppingSeg4Normalize; // 0x808C
    uint8_t  hoppingSeg4Factor;    // 0x808D
    uint8_t  NC_4;                 // 0x808E
    uint8_t  hoppingSeg5Normalize; // 0x808F
    uint8_t  hoppingSeg5Factor;    // 0x8090
    uint8_t  NC_5;                 // 0x8091
    uint8_t  hoppingSeg6Normalize; // 0x8092
    uint8_t  key[4];               // 0x8093-0x8096
    uint8_t  keyArea;              // 0x8097
    uint8_t  keyTouchLevel;        // 0x8098
    uint8_t  keyLeaveLevel;        // 0x8099
    uint8_t  keySens[2];           // 0x809A-0x809B
    uint8_t  keyRestrain;          // 0x809C
    uint8_t  keyRestrainTime;      // 0x809D
    uint8_t  gestureLargeTouch;    // 0x809E
    uint8_t  NC_6[2];              // 0x809F-0x80A0
    uint8_t  hotknotNoiseMap;      // 0x80A1
    uint8_t  linkThreshold;        // 0x80A2
    uint8_t  pxyThreshold;         // 0x80A3
    uint8_t  gHotDumpShift;        // 0x80A4
    uint8_t  gHotRxGain;           // 0x80A5
    uint8_t  freqGain[4];          // 0x80A6-0x80A9
    uint8_t  NC_7[9];              // 0x80AA-0x80B2
    uint8_t  combineDis;           // 0x80B3
    uint8_t  splitSet;             // 0x80B4
    uint8_t  NC_8[2];              // 0x80B5-0x80B6
    uint8_t  sensorCH[14];         // 0x80B7-0x80C4
    uint8_t  NC_9[16];             // 0x80C5-0x80D4
    uint8_t  driverCH[26];         // 0x80D5-0x80EE
    uint8_t  NC_10[16];            // 0x80EF-0x80FE
};

// ─── Gesture event types ──────────────────────────────────────────────────────

enum class TouchEvent {
    PRESS,
    RELEASE,
    MOVE,
    HOLD,
};

struct TouchPoint {
    uint8_t  id;
    uint16_t x;
    uint16_t y;
    uint16_t size;
    bool     active;
};

struct TouchEventData {
    TouchEvent event;
    TouchPoint point;
    int16_t    dx;
    int16_t    dy;
    uint32_t   duration_ms;
};

// ─── Internal per-finger tracking ────────────────────────────────────────────

struct TrackState {
    TouchPoint point;
    std::chrono::steady_clock::time_point pressTime;
    uint16_t startX;
    uint16_t startY;
    bool holdFired;
};

// ─── GT911 Linux driver ───────────────────────────────────────────────────────

class GT911 {
public:
    static constexpr int MAX_TOUCHES = GT911_MAX_CONTACTS;
    using EventCallback = std::function<void(const TouchEventData&)>;

private:
    I2CBus&  i2c;
    uint8_t  addr;
    GpioPin  intPin;
    GpioPin  rstPin;
    int      epollFd;
    int      intFd;

    std::thread       pollThread;
    std::atomic<bool> running{false};

    EventCallback onPressCallback;
    EventCallback onReleaseCallback;
    EventCallback onMoveCallback;
    EventCallback onHoldCallback;
    EventCallback onAnyCallback;
    std::mutex    cbMutex;

    uint16_t moveThreshold = 5;
    uint32_t holdMs        = 500;

    std::map<uint8_t, TrackState> tracked;

    GTInfo   _info;
    GTConfig _config;
    bool     _configLoaded = false;

    // ── Low-level I2C (matches Arduino reference pattern) ────────────────────

    void i2cSetReg(uint16_t reg) {
        uint8_t buf[2] = {(uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF)};
        ::write(i2c.getFd(), buf, 2);
    }

    bool writeReg(uint16_t reg, uint8_t data) {
        uint8_t buf[3] = {(uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), data};
        return ::write(i2c.getFd(), buf, 3) == 3;
    }

    uint8_t readReg(uint16_t reg) {
        i2cSetReg(reg);
        uint8_t val = 0;
        ::read(i2c.getFd(), &val, 1);
        return val;
    }

    bool writeBytes(uint16_t reg, uint8_t* data, uint16_t size) {
        std::vector<uint8_t> buf(2 + size);
        buf[0] = reg >> 8;
        buf[1] = reg & 0xFF;
        memcpy(buf.data() + 2, data, size);
        return ::write(i2c.getFd(), buf.data(), 2 + size) == (ssize_t)(2 + size);
    }

    bool readBytes(uint16_t reg, uint8_t* data, uint16_t size) {
        i2cSetReg(reg);
        ssize_t r = ::read(i2c.getFd(), data, size);
        return r == (ssize_t)size;
    }

    // ── Checksum (from Arduino reference) ────────────────────────────────────

    uint8_t calcChecksum(uint8_t* buf, uint8_t len) {
        uint8_t sum = 0;
        for (uint8_t i = 0; i < len; i++) sum += buf[i];
        return (~sum) + 1;
    }

    uint8_t readChecksum() { return readReg(GT911_REG_CHECKSUM); }

    // ── Reset sequence (from Arduino reference) ───────────────────────────────
    // INT low during reset = 0x5D, INT high = 0x14

    void hardReset() {
        intPin.set(0);
        rstPin.set(0);
        usleep(11000);  // >10ms

        // Set INT level to select address
        intPin.set(addr == GT911_I2C_ADDR_28 ? 1 : 0);
        usleep(110);    // >100us

        rstPin.set(1);  // release reset (input in Arduino, we just set high)
        usleep(6000);   // >5ms

        intPin.set(0);
        usleep(51000);  // >50ms settle
    }

    // ── Epoll edge interrupt ──────────────────────────────────────────────────

    void setupEdgeInterrupt(int gpio_abs) {
        std::string base = "/sys/class/gpio/gpio" + std::to_string(gpio_abs);

        std::ofstream edge(base + "/edge");
        if (!edge) throw std::runtime_error("Cannot set edge on INT pin");
        edge << "falling";
        edge.flush();

        intFd = open((base + "/value").c_str(), O_RDONLY | O_NONBLOCK);
        if (intFd < 0) throw std::runtime_error("Cannot open INT value fd");

        char buf; lseek(intFd, 0, SEEK_SET); ::read(intFd, &buf, 1);

        epollFd = epoll_create1(0);
        if (epollFd < 0) throw std::runtime_error("Cannot create epoll");

        struct epoll_event ev{};
        ev.events  = EPOLLPRI | EPOLLERR;
        ev.data.fd = intFd;
        epoll_ctl(epollFd, EPOLL_CTL_ADD, intFd, &ev);
    }

    // ── Raw touch read (matches Arduino readTouches + readTouchPoints) ────────

    int readRawTouches(GTPoint* points) {
        i2c.setAddr(addr);

        // Poll status register — bit7=buffer ready, bits0-3=count
        uint8_t flag = readReg(GT911_REG_COORD_ADDR);
        if (!(flag & 0x80)) return 0;

        int count = flag & 0x0F;
        if (count < 0 || count > GT911_MAX_CONTACTS) {
            writeReg(GT911_REG_COORD_ADDR, 0);
            return 0;
        }

        if (count > 0) {
            readBytes(GT911_REG_COORD_BASE,
                      (uint8_t*)points,
                      sizeof(GTPoint) * count);
        }

        // Clear status register (critical — must write 0 after read)
        writeReg(GT911_REG_COORD_ADDR, 0);
        return count;
    }

    // ── Fire event ────────────────────────────────────────────────────────────

    void fireEvent(const TouchEventData& ev) {
        std::lock_guard<std::mutex> lock(cbMutex);
        if (onAnyCallback) onAnyCallback(ev);
        switch (ev.event) {
            case TouchEvent::PRESS:   if (onPressCallback)   onPressCallback(ev);   break;
            case TouchEvent::RELEASE: if (onReleaseCallback) onReleaseCallback(ev); break;
            case TouchEvent::MOVE:    if (onMoveCallback)    onMoveCallback(ev);    break;
            case TouchEvent::HOLD:    if (onHoldCallback)    onHoldCallback(ev);    break;
        }
    }

    // ── Gesture processing ────────────────────────────────────────────────────

    void processTouches(GTPoint* gtPoints, int count) {
        auto now = std::chrono::steady_clock::now();

        // Build current active map from GTPoint array
        std::map<uint8_t, TouchPoint> currentMap;
        for (int i = 0; i < count; i++) {
            TouchPoint tp;
            tp.id     = gtPoints[i].trackId;
            tp.x      = gtPoints[i].x;
            tp.y      = gtPoints[i].y;
            tp.size   = gtPoints[i].area;
            tp.active = true;
            currentMap[tp.id] = tp;
        }

        // ── RELEASE ───────────────────────────────────────────────────────────
        std::vector<uint8_t> released;
        for (auto& [id, state] : tracked)
            if (currentMap.find(id) == currentMap.end())
                released.push_back(id);

        for (uint8_t id : released) {
            auto& state = tracked[id];
            uint32_t dur = (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                now - state.pressTime).count();
            TouchEventData ev{};
            ev.event       = TouchEvent::RELEASE;
            ev.point       = state.point;
            ev.dx          = 0; ev.dy = 0;
            ev.duration_ms = dur;
            fireEvent(ev);
            tracked.erase(id);
        }

        // ── PRESS / MOVE / HOLD ───────────────────────────────────────────────
        for (auto& [id, tp] : currentMap) {
            if (tracked.find(id) == tracked.end()) {
                // PRESS
                TrackState state{};
                state.point     = tp;
                state.pressTime = now;
                state.startX    = tp.x;
                state.startY    = tp.y;
                state.holdFired = false;
                tracked[id]     = state;

                TouchEventData ev{};
                ev.event = TouchEvent::PRESS;
                ev.point = tp;
                ev.dx = 0; ev.dy = 0; ev.duration_ms = 0;
                fireEvent(ev);

            } else {
                auto& state = tracked[id];
                int16_t  dx   = (int16_t)tp.x - (int16_t)state.point.x;
                int16_t  dy   = (int16_t)tp.y - (int16_t)state.point.y;
                float    dist = std::sqrt((float)(dx*dx + dy*dy));
                uint32_t dur  = (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - state.pressTime).count();

                // MOVE
                if (dist >= (float)moveThreshold) {
                    TouchEventData ev{};
                    ev.event = TouchEvent::MOVE;
                    ev.point = tp;
                    ev.dx = dx; ev.dy = dy;
                    ev.duration_ms = dur;
                    fireEvent(ev);
                    state.point = tp;
                }

                // HOLD — once, only if not wandered
                if (!state.holdFired && dur >= holdMs) {
                    int16_t tdx   = (int16_t)tp.x - (int16_t)state.startX;
                    int16_t tdy   = (int16_t)tp.y - (int16_t)state.startY;
                    float   tdist = std::sqrt((float)(tdx*tdx + tdy*tdy));
                    if (tdist < (float)(moveThreshold * 3)) {
                        state.holdFired = true;
                        TouchEventData ev{};
                        ev.event = TouchEvent::HOLD;
                        ev.point = tp;
                        ev.dx = 0; ev.dy = 0;
                        ev.duration_ms = dur;
                        fireEvent(ev);
                    }
                }
            }
        }
    }

    // ── Interrupt wait ────────────────────────────────────────────────────────

    bool waitForInterrupt(int timeout_ms) {
        struct epoll_event ev;
        int ret = epoll_wait(epollFd, &ev, 1, timeout_ms);
        if (ret <= 0) return false;
        char buf; lseek(intFd, 0, SEEK_SET); ::read(intFd, &buf, 1);
        return true;
    }

    // ── Poll loop ─────────────────────────────────────────────────────────────

    void pollLoop() {
        GTPoint points[GT911_MAX_CONTACTS];

        while (running) {
            bool triggered = waitForInterrupt(20);
            int count = 0;
            if (triggered)
                count = readRawTouches(points);
            processTouches(points, count);
        }

        // Flush remaining tracked fingers as releases
        auto now = std::chrono::steady_clock::now();
        for (auto& [id, state] : tracked) {
            uint32_t dur = (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                now - state.pressTime).count();
            TouchEventData ev{};
            ev.event = TouchEvent::RELEASE;
            ev.point = state.point;
            ev.dx = 0; ev.dy = 0;
            ev.duration_ms = dur;
            fireEvent(ev);
        }
        tracked.clear();
    }

public:

    // int_pin, rst_pin = BCM numbers, chip offset 512 handled internally
    GT911(I2CBus& bus, int int_pin, int rst_pin, uint8_t address = GT911_I2C_ADDR_BA)
        : i2c(bus), addr(address),
          intPin(int_pin, true),   // output for reset sequence
          rstPin(rst_pin, true),
          epollFd(-1), intFd(-1)
    {
        i2c.setAddr(addr);
        hardReset();

        // Switch INT to input after reset for interrupt detection
        {
            std::string intBase = "/sys/class/gpio/gpio" + std::to_string(512 + int_pin);
            std::ofstream dir(intBase + "/direction");
            dir << "in";
        }

        setupEdgeInterrupt(512 + int_pin);
    }

    // ── Info / config ─────────────────────────────────────────────────────────

    GTInfo* readInfo() {
        i2c.setAddr(addr);
        readBytes(GT911_REG_DATA, (uint8_t*)&_info, sizeof(_info));
        return &_info;
    }

    GTConfig* readConfig() {
        i2c.setAddr(addr);
        readBytes(GT911_REG_CFG, (uint8_t*)&_config, sizeof(_config));
        if (readChecksum() == calcChecksum((uint8_t*)&_config, sizeof(_config))) {
            _configLoaded = true;
            return &_config;
        }
        return nullptr;
    }

    bool writeConfig() {
        if (!_configLoaded) return false;
        i2c.setAddr(addr);
        uint8_t checksum = calcChecksum((uint8_t*)&_config, sizeof(_config));
        if (readChecksum() != checksum) {
            writeBytes(GT911_REG_CFG, (uint8_t*)&_config, sizeof(_config));
            uint8_t buf[2] = {checksum, 1};
            writeBytes(GT911_REG_CHECKSUM, buf, 2);
            return true;
        }
        return false;
    }

    // Set display resolution — writes to GT911 config registers
    void setResolution(uint16_t w, uint16_t h) {
        i2c.setAddr(addr);
        // Write X/Y resolution directly to config registers
        uint8_t xbuf[2] = {(uint8_t)(w & 0xFF), (uint8_t)(w >> 8)};
        uint8_t ybuf[2] = {(uint8_t)(h & 0xFF), (uint8_t)(h >> 8)};
        writeBytes(0x8048, xbuf, 2);
        writeBytes(0x804A, ybuf, 2);
        // Trigger soft reset to apply
        writeReg(0x8040, 0x00);
    }

    void setMoveThreshold(uint16_t px) { moveThreshold = px; }
    void setHoldDuration(uint32_t ms)  { holdMs = ms; }

    // ── Callbacks ─────────────────────────────────────────────────────────────

    void onAny    (EventCallback cb) { std::lock_guard<std::mutex> l(cbMutex); onAnyCallback     = cb; }
    void onPress  (EventCallback cb) { std::lock_guard<std::mutex> l(cbMutex); onPressCallback   = cb; }
    void onRelease(EventCallback cb) { std::lock_guard<std::mutex> l(cbMutex); onReleaseCallback = cb; }
    void onMove   (EventCallback cb) { std::lock_guard<std::mutex> l(cbMutex); onMoveCallback    = cb; }
    void onHold   (EventCallback cb) { std::lock_guard<std::mutex> l(cbMutex); onHoldCallback    = cb; }

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    void startPolling() {
        running    = true;
        pollThread = std::thread(&GT911::pollLoop, this);
    }

    void stopPolling() {
        running = false;
        if (pollThread.joinable()) pollThread.join();
    }

    ~GT911() {
        stopPolling();
        if (intFd >= 0)   close(intFd);
        if (epollFd >= 0) close(epollFd);
    }
};