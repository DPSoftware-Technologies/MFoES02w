#pragma once

#include <GFX.h>
#include <DrawReplay.h>
#include "hwinterface/gt911.h"
#include <linfo.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <queue>
#include <memory>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <thread>
#include "uisys/manager.h"
#include <unistd.h>
#include <UIwidget.h>

#define APP_FPS 30
#define SCREEN_W 1280
#define SCREEN_H 720

#ifndef DESKTOP
#include "hwinterface/i2c_dev.h"
#include "hwinterface/usbd_client.h"
#include "hwinterface/pwm.h"
#include "hwinterface/gpio_sysfs.h"
#include "hwinterface/northbridge.h"

#include "dts.h"

#endif

#pragma pack(push, 1)

struct CValue { // Custom Value
    uint8_t  type;
    uint16_t v[8];
}; // 17 bytes

#pragma pack(pop)

static_assert(sizeof(CValue) == 17, "Size mismatch!");

class App {
    public:
        App();
        ~App();
        void init();
        int  run();
        void stop();
        void ostop(bool restart=false);

        void defer(std::function<void()> fn) { _pendingAction = fn; }
        void postAction(std::function<void()> fn) {
            std::lock_guard<std::mutex> lock(_actionQueueMutex);
            _actionQueue.push(fn);
        }
    private:
        // Hardware init
        LinuxGFX gfx;

        // ===== Remote draw stream ============================================
        //
        // Every frame that is drawn locally is drawn a second time into `dr`,
        // which rasterises nothing and instead records the draw calls as a few
        // hundred bytes of commands. Those bytes go out over USB so a remote
        // renderer can reproduce the screen without anyone shipping a 3.5 MB
        // framebuffer.
        //
        // The second pass only runs while a remote is actually attached, so
        // the panel costs nothing extra when nobody is listening.
        //
        // RGB565 bitmaps are masked out of the recording on purpose: the only
        // one is the incoming DTS video frame, at 1.8 MB per frame, and the
        // host is the side that sent it in the first place.
        DrawReplay           dr{SCREEN_W, SCREEN_H};
        std::vector<uint8_t> drBlob;

        // An unchanged screen produces a byte-identical blob, and render()
        // publishes on every loop iteration whether or not anything moved.
        // Sending those repeats is pure wasted bandwidth, so the last blob is
        // kept and identical ones are dropped. Touched only by the render
        // thread; drForceResend is how the sender thread asks for a full frame
        // after a reconnect, when the far end has nothing on screen.
        std::vector<uint8_t> drLastSent;
        uint32_t             drFramesSkipped = 0;
        std::atomic<bool>    drForceResend{true};

        // Safety net for the dedup above. A viewer that attaches, misses the
        // announcement, or drops a frame would otherwise sit on a stale or
        // blank window for as long as the screen stays still. Resending an
        // unchanged frame this often costs about 1 KB and bounds that wait.
        uint32_t             drLastSentMs = 0;
        uint32_t             drKeyframeMs = 2000;   // 0 disables

        /// What a single frame needs to paint. Computed once per frame so the
        /// local pass and the recording pass render identical content — the
        /// redraw flags are latched here and cleared once, not consumed by
        /// whichever pass happens to run first.
        struct RenderPlan {
            bool clear    = false;
            bool dts      = false;
            bool dataIn   = false;
            bool about    = false;
            bool sysInfo  = false;
            bool widgets  = false;
            bool status   = false;
            bool any      = false;
            bool askRedraw = false;
        };

        RenderPlan planFrame(bool forceRender);

        /// Hand a finished command blob to the sender. Never blocks the render
        /// loop: the newest frame wins and older ones are dropped.
        void drPublish(const std::vector<uint8_t>& blob);
        bool drRemoteAttached() const;

#ifndef DESKTOP
        // DrawReplay sender. Its own usbd channel, so the outbound draw stream
        // can never interfere with inbound DTS video (channel 0) or otad
        // (channel 10). drPending is a one-deep slot: if the sender is still
        // busy when a new frame arrives the older one is dropped, because for
        // a display stream only the newest frame is worth sending.
        UsbdClient              usbdcDR;
        pthread_t               dr_thread{};
        bool                    drThreadRunning = false;
        std::atomic<bool>       drRemoteUp{false};   // set by the sender thread
        std::vector<uint8_t>    drPending;
        bool                    drHasPending    = false;
        std::mutex              drMutex;
        std::condition_variable drCv;
        uint32_t                drFramesSent    = 0;
        uint32_t                drFramesDropped = 0;  // guarded by drMutex

        static void* drThreadFunc(void* arg);
        void drLoop();
        bool drStart();

        /// Decode touch records arriving from the viewer and queue them as if
        /// they had come from the GT911. May contain several records.
        void drHandleInput(const uint8_t* data, size_t len);
        uint32_t drTouchesIn = 0;

        I2CBus i2c;
        UsbdClient usbdc;
        GT911 touch;
        PWM buz;

        GpioPin led1;
        GpioPin led2;

        // USB
        pthread_t usb_thread;
        static void* usbThreadFunc(void* arg);
        void usbLoop();

        // Northbridge (Pico) control panel and RTC, over SPI1
        std::unique_ptr<northbridge::Link> nb;
        NbInfoWire  nbInfo{};
        NbPanelWire nbPanel{};
        northbridge::EncoderState nbEnc{};    // last encoder report
        uint8_t     nbLeds       = 0;         // panel LEDs we last asked for, bit 0 = LED 1
        bool        nbOnline     = false;
        bool        nbClockFromRtc = false;  // we set the system clock, so do not push it back
        uint32_t    nbLastClockCheck = 0;

        bool initNorthbridge();
        void nbService();                    // called from process(), cheap when idle
        void nbOnPanelEvent(const northbridge::PanelState& panel);
        void nbOnEncoderEvent(const northbridge::EncoderState& enc);

        /* Panel LEDs. The northbridge lights them for its power-on test and
         * then leaves them alone, so after boot they show only what we set. */
        bool nbSetLeds(uint8_t mask);
        uint8_t nbGetLeds() const { return nbLeds; }
        bool nbReadTime(NbTimeWire& out, unsigned timeout_ms = 250, unsigned attempts = 3);
        bool nbAdoptRtcTime();               // RTC -> system clock
        bool nbPushSystemTime();             // system clock -> RTC

        // DTS
        bool hasFrame = false;
        uint16_t frameBufA[FRAME_PIXELS];  // USB thread writes
        uint16_t frameBufB[FRAME_PIXELS];  // render thread reads
        bool frameReady;
#endif
        pthread_mutex_t frameMutex;
        // touch
        std::queue<TouchEventData> touchQueue;
        std::mutex touchQueueMutex;
        // system msg
        char statusMsg[128];

        // UI
        uisys::Manager ui;
        std::function<void()> _pendingAction; 
        std::queue<std::function<void()>> _actionQueue;
        std::mutex _actionQueueMutex;

        Linfo linfo;

        // app
        int sw = gfx.width();
        int sh = gfx.height();
        int fps_us = 1000000 / APP_FPS;
        bool running = true;
        
        // app init
        void initSysUI();
        void initDemoUI();
        void initSidebarBTNs();

        // render
        //
        // Templated on the target so one body serves both the real display and
        // the DrawReplay recorder. Binding to the concrete type matters: it
        // reaches DrawReplay's own overloads, which record one compact command
        // per call instead of decomposing into spans. Defined in ui.cpp, which
        // is the only translation unit that instantiates them.
        template<typename G> void renderAbout(G& g);
        template<typename G> void renderDataInInfo(G& g);
        template<typename G> void renderInfo(G& g);
        template<typename G> void drawFrame(G& g, const RenderPlan& plan);
        void render(bool forceRender=false);
        
        void process();
        void inputHandle();

        void setView(std::string view);

        // Data input handler
        CValue cvdata = {};
        
        bool show_about = true;
        bool hide_ui = false;
        bool show_data_in = false;
        bool show_info = false;

        // Request Render For ...
        bool RRFDTS = false;
        bool RRFSYSMSG = false;
        bool RRFSYSINFO = false;
        bool RRFF = false; // Force render
        
        uint32_t lastUpdate1 = 0;

        int cycleCount = 0;
};

static std::vector<ColorThreshold> InfoBarThresholdsColors {
    {0.0f,  0xFF00FF00}, // Green for 0 and up
    {70.0f, 0xFFFFFF00}, // Yellow for 70 and up
    {90.0f, 0xFFFF0000}  // Red for 90 and up
};

// for check Adafruit GFX assets compatible
static const unsigned char adaf_logo_bmp[] = {
	0b00000000,0b00000000,0b01100000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,
	0b00000000,0b00000000,0b11100000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,
	0b00000000,0b00000001,0b11100000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,
	0b00000000,0b00000001,0b11110000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,
	0b00000000,0b00000011,0b11110000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,
	0b00000000,0b00000111,0b11110000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,
	0b00000000,0b00000111,0b11111000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,
	0b00000000,0b00001111,0b11111000,0b00000000,0b00000000,0b00000000,0b00000011,0b11000000,0b00000000,0b01111110,0b00000000,0b00000000,0b00000001,0b11100000,0b00000000,
	0b01111111,0b00001111,0b11111000,0b00000000,0b00000000,0b00000000,0b00000011,0b11000000,0b00000000,0b11111110,0b00000000,0b00000000,0b00000001,0b11100000,0b00000000,
	0b11111111,0b11101111,0b11111000,0b00000000,0b00000000,0b00000000,0b00000011,0b11000000,0b00000000,0b11111110,0b00000000,0b00000000,0b00000001,0b11100000,0b00000000,
	0b11111111,0b11111111,0b11111000,0b00000000,0b00000000,0b00000000,0b00000011,0b11000000,0b00000000,0b11110000,0b00000000,0b00000000,0b00000000,0b00001111,0b00000000,
	0b01111111,0b11111110,0b01111111,0b11000000,0b00000000,0b00000000,0b00000011,0b11000000,0b00000000,0b11110000,0b00000000,0b00000000,0b00000000,0b00001111,0b00000000,
	0b00111111,0b11111110,0b01111111,0b11111000,0b00111111,0b11110001,0b11111011,0b11001111,0b11111100,0b11111110,0b11110011,0b10111100,0b00111101,0b11101111,0b11100000,
	0b00011111,0b11111110,0b01111111,0b11111111,0b01111111,0b11111011,0b11111111,0b11011111,0b11111110,0b11111110,0b11111111,0b10111100,0b00111101,0b11101111,0b11100000,
	0b00011111,0b11000110,0b11111111,0b11111111,0b01111111,0b11111011,0b11111111,0b11011111,0b11111110,0b11111110,0b11111111,0b10111100,0b00111101,0b11101111,0b11100000,
	0b00001111,0b11100011,0b11000111,0b11111110,0b01111000,0b01111011,0b11000011,0b11011110,0b00011110,0b11110000,0b11111111,0b10111100,0b00111101,0b11101111,0b00000000,
	0b00000111,0b11111111,0b10000111,0b11111100,0b01111000,0b01111011,0b11000011,0b11011110,0b00011110,0b11110000,0b11111000,0b00111100,0b00111101,0b11101111,0b00000000,
	0b00000001,0b11111111,0b11111111,0b11110000,0b00000000,0b01111011,0b11000011,0b11000000,0b00011110,0b11110000,0b11110000,0b00111100,0b00111101,0b11101111,0b00000000,
	0b00000001,0b11110011,0b01111111,0b11100000,0b00111111,0b11111011,0b11000011,0b11001111,0b11111110,0b11110000,0b11110000,0b00111100,0b00111101,0b11101111,0b00000000,
	0b00000011,0b11100011,0b00111111,0b10000000,0b01111111,0b11111011,0b11000011,0b11011111,0b11111110,0b11110000,0b11110000,0b00111100,0b00111101,0b11101111,0b00000000,
	0b00000111,0b11100111,0b00111100,0b00000000,0b01111000,0b01111011,0b11000011,0b11011110,0b00011110,0b11110000,0b11110000,0b00111100,0b00111101,0b11101111,0b00000000,
	0b00000111,0b11111111,0b10111110,0b00000000,0b01111000,0b01111011,0b11000011,0b11011110,0b00011110,0b11110000,0b11110000,0b00111100,0b00111101,0b11101111,0b00000000,
	0b00000111,0b11111111,0b11111110,0b00000000,0b01111000,0b01111011,0b11000011,0b11011110,0b00011110,0b11110000,0b11110000,0b00111100,0b00111101,0b11101111,0b00000000,
	0b00001111,0b11111111,0b11111110,0b00000000,0b01111111,0b11111011,0b11111111,0b11011111,0b11111110,0b11110000,0b11110000,0b00111111,0b11111101,0b11101111,0b11100000,
	0b00001111,0b11111111,0b11111111,0b00000000,0b01111111,0b11111011,0b11111111,0b11011111,0b11111110,0b11110000,0b11110000,0b00111111,0b11111101,0b11101111,0b11100000,
	0b00001111,0b11111001,0b11111111,0b00000000,0b00111110,0b01111001,0b11111001,0b11001111,0b10011110,0b11110000,0b11110000,0b00011111,0b00111101,0b11100111,0b11100000,
	0b00011111,0b11110001,0b11111111,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000,
	0b00011111,0b10000000,0b11111111,0b00000000,0b01111111,0b11111111,0b11111111,0b11111111,0b11111111,0b11111111,0b11111111,0b11111111,0b11111111,0b11111111,0b11100000,
	0b00011100,0b00000000,0b01111111,0b00000000,0b01111111,0b11111111,0b11111111,0b11111111,0b11111110,0b10110100,0b01101101,0b10001000,0b10001101,0b00011000,0b11100000,
	0b00000000,0b00000000,0b00011111,0b00000000,0b01111111,0b11111111,0b11111111,0b11111111,0b11111110,0b10010101,0b10101101,0b01111101,0b10110101,0b01110111,0b11100000,
	0b00000000,0b00000000,0b00001111,0b00000000,0b01111111,0b11111111,0b11111111,0b11111111,0b11111110,0b10100101,0b10101101,0b10011101,0b10001101,0b00011001,0b11100000,
	0b00000000,0b00000000,0b00000110,0b00000000,0b01111111,0b11111111,0b11111111,0b11111111,0b11111110,0b10110101,0b10101101,0b11101101,0b10110101,0b01111110,0b11100000,
};