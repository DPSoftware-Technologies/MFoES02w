#include "app.h"
#include <cstdio>
#include <cstring>
#include <pthread.h>

#define RECV_BUF_SIZE (1 + FRAME_SIZE)

App::App()
#ifdef GFX_USE_DRM_DUMB
    :   gfx("/dev/dri/card0"),
#else
    :   gfx("/dev/fb0"),
#endif
        i2c("/dev/i2c-1"),
        frameReady(false),
        touch(i2c, 17, 27),  // int_pin=17, rst_pin=27
        buz(4, true),
        ui(0, 400, 1280, 320, uisys::Font::Medium())
{
    pthread_mutex_init(&frameMutex, nullptr);
    memset(frameBufA, 0, sizeof(frameBufA));
    memset(frameBufB, 0, sizeof(frameBufB));
}

App::~App() {
    pthread_mutex_destroy(&frameMutex);
}

void App::init() {
#ifndef GFX_USE_DRM_DUMB
    gfx.enableMultiBuffer(2);
#endif
    gfx.fillScreen(0x0000);
    gfx.swapBuffers();

    // Start USB in background, don't block init
    snprintf(statusMsg, sizeof(statusMsg), "USB: starting...");
    pthread_create(&usb_thread, nullptr, App::usbThreadFunc, this);

    GTConfig* cfg = touch.readConfig();
    if (cfg) {
        cfg->xResolution = 1280;
        cfg->yResolution = 720;
        touch.writeConfig();  // writes checksum + triggers update
    }

    touch.setMoveThreshold(1);    // px — tune to your screen
    touch.setHoldDuration(500);   // ms

    touch.onPress([this](const TouchEventData& e) {
        pthread_mutex_lock(&frameMutex);
        snprintf(statusMsg, sizeof(statusMsg), "Touch: press at (%d, %d)", e.point.x, e.point.y);
        pthread_mutex_unlock(&frameMutex);

        std::lock_guard<std::mutex> lock(touchQueueMutex);
        touchQueue.push(e);  // ← queue for main thread to draw
    });

    touch.onMove([this](const TouchEventData& e) {
        std::lock_guard<std::mutex> lock(touchQueueMutex);
        touchQueue.push(e);
    });

    touch.onRelease([this](const TouchEventData& e) {
        pthread_mutex_lock(&frameMutex);
        snprintf(statusMsg, sizeof(statusMsg), "Touch: release at (%d, %d)", e.point.x, e.point.y);
        pthread_mutex_unlock(&frameMutex);

        std::lock_guard<std::mutex> lock(touchQueueMutex);
        touchQueue.push(e); 
    });

    touch.onHold([this](const TouchEventData& e) {
        pthread_mutex_lock(&frameMutex);
        snprintf(statusMsg, sizeof(statusMsg), "Touch: hold at (%d, %d)", e.point.x, e.point.y);
        pthread_mutex_unlock(&frameMutex);

        std::lock_guard<std::mutex> lock(touchQueueMutex);
        touchQueue.push(e); 
    });

    // init UI
    initSysUI();
    initSidebarBTNs();
    initDemoUI();
}

int App::run() {
    touch.startPolling();

    int sw = gfx.width();
    int sh = gfx.height();

    while (true) {
        // pull frame from USB thread
        pthread_mutex_lock(&frameMutex);
        if (frameReady) memcpy(frameBufB, frameBufA, FRAME_SIZE);
        bool hasFrame = frameReady;
        pthread_mutex_unlock(&frameMutex);

        if (hasFrame) {
            int ox = (sw - SCREEN_W) / 2;
            int oy = (sh - SCREEN_H) / 2;
            gfx.drawRGBBitmap(ox, oy, frameBufB, SCREEN_W, SCREEN_H);
        } else {
            gfx.fillScreen(0x0000);
        }

        // draw status
        gfx.setCursor(10, 10);
        gfx.setTextColor(
            gfx.color565(0xFF, 0xFF, 0xFF),
            gfx.color565(0x00, 0x00, 0x00)
        );
        gfx.setTextSize(1);
        pthread_mutex_lock(&frameMutex);
        gfx.writeText(statusMsg);
        pthread_mutex_unlock(&frameMutex);

        std::lock_guard<std::mutex> lock(touchQueueMutex);
        while (!touchQueue.empty()) {
            TouchEventData e = touchQueue.front();
            touchQueue.pop();

            switch (e.event) {
                case TouchEvent::PRESS:
                    gfx.drawCircle(e.point.x, e.point.y, 20, gfx.color565(0xFF, 0x00, 0x00));
                    break;
                case TouchEvent::MOVE:
                    gfx.fillCircle(e.point.x, e.point.y, 5, gfx.color565(0x00, 0xFF, 0x00));
                    break;
                case TouchEvent::HOLD:
                    gfx.drawCircle(e.point.x, e.point.y, 40, gfx.color565(0xFF, 0xFF, 0x00));
                    break;
                case TouchEvent::RELEASE:
                    break;
            }
            ui.handleEvent(e);
        }
        if (!hide_ui) {ui.draw(gfx);};

        if (show_about) {renderAbout(sw, sh);};

        gfx.swapBuffers();
        usleep(16000); // ~60 FPS
    }
    return 0;
}