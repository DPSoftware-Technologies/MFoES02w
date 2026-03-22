#include "gt911.h"
#include <cmath>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <stdexcept>
#include <fstream>

void GT911::i2cSetReg(uint16_t reg) {
    uint8_t buf[2] = {(uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF)};
    ::write(i2c.getFd(), buf, 2);
}

bool GT911::writeReg(uint16_t reg, uint8_t data) {
    uint8_t buf[3] = {(uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), data};
    return ::write(i2c.getFd(), buf, 3) == 3;
}

uint8_t GT911::readReg(uint16_t reg) {
    i2cSetReg(reg);
    uint8_t val = 0;
    ::read(i2c.getFd(), &val, 1);
    return val;
}

bool GT911::writeBytes(uint16_t reg, uint8_t* data, uint16_t size) {
    std::vector<uint8_t> buf(2 + size);
    buf[0] = reg >> 8;
    buf[1] = reg & 0xFF;
    memcpy(buf.data() + 2, data, size);
    return ::write(i2c.getFd(), buf.data(), 2 + size) == (ssize_t)(2 + size);
}

bool GT911::readBytes(uint16_t reg, uint8_t* data, uint16_t size) {
    i2cSetReg(reg);
    ssize_t r = ::read(i2c.getFd(), data, size);
    return r == (ssize_t)size;
}

uint8_t GT911::calcChecksum(uint8_t* buf, uint8_t len) {
    uint8_t sum = 0;
    for (uint8_t i = 0; i < len; i++) sum += buf[i];
    return (~sum) + 1;
}

uint8_t GT911::readChecksum() {
    return readReg(GT911_REG_CHECKSUM);
}

void GT911::hardReset() {
    intPin.set(0);
    rstPin.set(0);
    usleep(11000);

    intPin.set(addr == GT911_I2C_ADDR_28 ? 1 : 0);
    usleep(110);

    rstPin.set(1);
    usleep(6000);

    intPin.set(0);
    usleep(51000);
}

void GT911::setupEdgeInterrupt(int gpio_abs) {
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

int GT911::readRawTouches(GTPoint* points) {
    i2c.setAddr(addr);

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

    writeReg(GT911_REG_COORD_ADDR, 0);
    return count;
}

void GT911::fireEvent(const TouchEventData& ev) {
    std::lock_guard<std::mutex> lock(cbMutex);
    if (onAnyCallback) onAnyCallback(ev);
    switch (ev.event) {
        case TouchEvent::PRESS:   if (onPressCallback)   onPressCallback(ev);   break;
        case TouchEvent::RELEASE: if (onReleaseCallback) onReleaseCallback(ev); break;
        case TouchEvent::MOVE:    if (onMoveCallback)    onMoveCallback(ev);    break;
        case TouchEvent::HOLD:    if (onHoldCallback)    onHoldCallback(ev);    break;
    }
}

void GT911::processTouches(GTPoint* gtPoints, int count) {
    auto now = std::chrono::steady_clock::now();

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

    std::vector<uint8_t> released;
    for (auto& [id, state] : tracked)
        if (currentMap.find(id) == currentMap.end())
            released.push_back(id);

    for (uint8_t id : released) {
        auto& state = tracked[id];
        uint32_t dur = (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
            now - state.pressTime).count();

        lastRelease[id] = now;

        if (!state.debouncing) {
            TouchEventData ev{};
            ev.event       = TouchEvent::RELEASE;
            ev.point       = state.point;
            ev.dx          = 0; ev.dy = 0;
            ev.duration_ms = dur;
            fireEvent(ev);
        }
        tracked.erase(id);
    }

    for (auto& [id, tp] : currentMap) {
        if (tracked.find(id) == tracked.end()) {
            auto relIt = lastRelease.find(id);
            if (relIt != lastRelease.end() && debounceMs > 0) {
                uint32_t msSinceRelease = (uint32_t)std::chrono::duration_cast<
                    std::chrono::milliseconds>(now - relIt->second).count();
                if (msSinceRelease < debounceMs) {
                    TrackState state{};
                    state.point      = tp;
                    state.pressTime  = relIt->second;
                    state.releaseTime= relIt->second;
                    state.startX     = tp.x;
                    state.startY     = tp.y;
                    state.holdFired  = false;
                    state.debouncing = true;
                    tracked[id]      = state;
                    continue;
                }
            }

            TrackState state{};
            state.point      = tp;
            state.pressTime  = now;
            state.startX     = tp.x;
            state.startY     = tp.y;
            state.holdFired  = false;
            state.debouncing = false;
            tracked[id]      = state;

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

            if (dist >= (float)moveThreshold) {
                TouchEventData ev{};
                ev.event = TouchEvent::MOVE;
                ev.point = tp;
                ev.dx = dx; ev.dy = dy;
                ev.duration_ms = dur;
                fireEvent(ev);
                state.point = tp;
            }

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

bool GT911::waitForInterrupt(int timeout_ms) {
    struct epoll_event ev;
    int ret = epoll_wait(epollFd, &ev, 1, timeout_ms);
    if (ret <= 0) return false;
    char buf; lseek(intFd, 0, SEEK_SET); ::read(intFd, &buf, 1);
    return true;
}

void GT911::pollLoop() {
    GTPoint points[GT911_MAX_CONTACTS];

    while (running) {
        bool triggered = waitForInterrupt(20);
        int count = 0;
        if (triggered)
            count = readRawTouches(points);
        processTouches(points, count);
    }

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

GT911::GT911(I2CBus& bus, int int_pin, int rst_pin, uint8_t address)
    : i2c(bus), addr(address),
      intPin(int_pin, true),
      rstPin(rst_pin, true),
      epollFd(-1), intFd(-1)
{
    i2c.setAddr(addr);
    hardReset();

    {
        std::string intBase = "/sys/class/gpio/gpio" + std::to_string(512 + int_pin);
        std::ofstream dir(intBase + "/direction");
        dir << "in";
    }

    setupEdgeInterrupt(512 + int_pin);
}

GTInfo* GT911::readInfo() {
    i2c.setAddr(addr);
    readBytes(GT911_REG_DATA, (uint8_t*)&_info, sizeof(_info));
    return &_info;
}

GTConfig* GT911::readConfig() {
    i2c.setAddr(addr);
    readBytes(GT911_REG_CFG, (uint8_t*)&_config, sizeof(_config));
    if (readChecksum() == calcChecksum((uint8_t*)&_config, sizeof(_config))) {
        _configLoaded = true;
        return &_config;
    }
    return nullptr;
}

bool GT911::writeConfig() {
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

void GT911::setResolution(uint16_t w, uint16_t h) {
    i2c.setAddr(addr);
    uint8_t xbuf[2] = {(uint8_t)(w & 0xFF), (uint8_t)(w >> 8)};
    uint8_t ybuf[2] = {(uint8_t)(h & 0xFF), (uint8_t)(h >> 8)};
    writeBytes(0x8048, xbuf, 2);
    writeBytes(0x804A, ybuf, 2);
    writeReg(0x8040, 0x00);
}

void GT911::setMoveThreshold(uint16_t px) { moveThreshold = px; }
void GT911::setHoldDuration(uint32_t ms)  { holdMs = ms; }
void GT911::setDebounceMs(uint32_t ms)    { debounceMs = ms; }

void GT911::onAny    (EventCallback cb) { std::lock_guard<std::mutex> l(cbMutex); onAnyCallback     = cb; }
void GT911::onPress  (EventCallback cb) { std::lock_guard<std::mutex> l(cbMutex); onPressCallback   = cb; }
void GT911::onRelease(EventCallback cb) { std::lock_guard<std::mutex> l(cbMutex); onReleaseCallback = cb; }
void GT911::onMove   (EventCallback cb) { std::lock_guard<std::mutex> l(cbMutex); onMoveCallback    = cb; }
void GT911::onHold   (EventCallback cb) { std::lock_guard<std::mutex> l(cbMutex); onHoldCallback    = cb; }

void GT911::startPolling() {
    running    = true;
    pollThread = std::thread(&GT911::pollLoop, this);
}

void GT911::stopPolling() {
    running = false;
    if (pollThread.joinable()) pollThread.join();
}

GT911::~GT911() {
    stopPolling();
    if (intFd >= 0)   close(intFd);
    if (epollFd >= 0) close(epollFd);
}
