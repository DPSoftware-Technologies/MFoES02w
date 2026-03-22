#pragma once
#include "i2c_dev.h"
#include "gpio_sysfs.h"
#include <cstdint>
#include <cstring>
#include <thread>
#include <atomic>
#include <functional>
#include <mutex>
#include <chrono>
#include <map>
#include <vector>

//  GT911 register map 

#define GT911_I2C_ADDR_28   0x14   // INT high during reset
#define GT911_I2C_ADDR_BA   0x5D   // INT low during reset  (default)
#define GT911_MAX_CONTACTS  5

#define GT911_REG_CFG        0x8047
#define GT911_REG_CHECKSUM   0x80FF
#define GT911_REG_DATA       0x8140  // product ID / info base
#define GT911_REG_ID         0x8140
#define GT911_REG_COORD_ADDR 0x814E  // status register
#define GT911_REG_COORD_BASE 0x814F  // first touch point

//  Packed structs (match Arduino reference exactly) 

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

//  Gesture event types 

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

//  Internal per-finger tracking 

struct TrackState {
    TouchPoint point;
    std::chrono::steady_clock::time_point pressTime;
    std::chrono::steady_clock::time_point releaseTime;  // when this ID last released
    uint16_t startX;
    uint16_t startY;
    bool holdFired;
    bool debouncing;  // true = released but within debounce window, suppress re-press
};

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
    uint32_t debounceMs    = 75;

    std::map<uint8_t, TrackState>                              tracked;
    std::map<uint8_t, std::chrono::steady_clock::time_point>   lastRelease;

    GTInfo   _info;
    GTConfig _config;
    bool     _configLoaded = false;

    void    i2cSetReg(uint16_t reg);
    bool    writeReg(uint16_t reg, uint8_t data);
    uint8_t readReg(uint16_t reg);
    bool    writeBytes(uint16_t reg, uint8_t* data, uint16_t size);
    bool    readBytes(uint16_t reg, uint8_t* data, uint16_t size);

    uint8_t calcChecksum(uint8_t* buf, uint8_t len);
    uint8_t readChecksum();

    void hardReset();
    void setupEdgeInterrupt(int gpio_abs);

    int  readRawTouches(GTPoint* points);
    void fireEvent(const TouchEventData& ev);
    void processTouches(GTPoint* gtPoints, int count);
    bool waitForInterrupt(int timeout_ms);
    void pollLoop();

public:
    GT911(I2CBus& bus, int int_pin, int rst_pin, uint8_t address = GT911_I2C_ADDR_BA);

    GTInfo*   readInfo();
    GTConfig* readConfig();
    bool      writeConfig();
    void      setResolution(uint16_t w, uint16_t h);

    void setMoveThreshold(uint16_t px);
    void setHoldDuration(uint32_t ms);
    void setDebounceMs(uint32_t ms);

    void onAny    (EventCallback cb);
    void onPress  (EventCallback cb);
    void onRelease(EventCallback cb);
    void onMove   (EventCallback cb);
    void onHold   (EventCallback cb);

    void startPolling();
    void stopPolling();

    ~GT911();
};
