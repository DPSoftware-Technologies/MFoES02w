#include "app.h"
#include <cstdio>
#include <cstring>
#include <pthread.h>

#define RECV_BUF_SIZE (1 + FRAME_SIZE)

App::App()
    :   gfx("/dev/fb0"),
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
    gfx.enableMultiBuffer(2);
    gfx.fillScreen(GFX_BLACK);
    gfx.swapBuffers();

    // Start USB in background, don't block init
    snprintf(statusMsg, sizeof(statusMsg), "USB: starting...");
    pthread_create(&usb_thread, nullptr, App::usbThreadFunc, this);

    GTConfig* cfg = touch.readConfig();
    if (cfg) {
        cfg->xResolution = 1280;
        cfg->yResolution = 720;
        touch.writeConfig();
    }

    touch.setMoveThreshold(1);  
    touch.setHoldDuration(500); 

    touch.onPress([this](const TouchEventData& e) {
        pthread_mutex_lock(&frameMutex);
        snprintf(statusMsg, sizeof(statusMsg), "Touch: press at (%d, %d)", e.point.x, e.point.y);
        pthread_mutex_unlock(&frameMutex);
        std::lock_guard<std::mutex> lock(touchQueueMutex);
        touchQueue.push(e);  
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

    touch.startPolling();

    // init UI
    initSysUI();
    initSidebarBTNs();
    initDemoUI();

    // Register pump for quickFireDialog
    ui.setPumpFn([this]() {
        process();
    });
}

void App::inputHandle() {
    std::lock_guard<std::mutex> lock(touchQueueMutex);
    while (!touchQueue.empty()) {
        TouchEventData e = touchQueue.front();
        touchQueue.pop();
        switch (e.event) {
            case TouchEvent::PRESS: break;
            case TouchEvent::MOVE: break;
            case TouchEvent::HOLD: break;
            case TouchEvent::RELEASE: break;
        }
        if (!hide_ui) ui.handleEvent(e);
    }
}

void App::process() {
    pthread_mutex_lock(&frameMutex);
    if (frameReady) {
        memcpy(frameBufB, frameBufA, FRAME_SIZE); 
        hasFrame = frameReady;
        RRFDTS = true;
    }
    pthread_mutex_unlock(&frameMutex);

    inputHandle();

    // Drain actions posted from other threads
    std::lock_guard<std::mutex> lock(_actionQueueMutex);
    while (!_actionQueue.empty()) {
        auto fn = _actionQueue.front();
        _actionQueue.pop();
        fn();   // runs on main thread — safe to call UI/GFX
    }

    render();
    usleep(fps_us);
}

int App::run() {
    while (running) {
        process();

        if (_pendingAction) {
            auto action = _pendingAction;
            _pendingAction = nullptr;
            action();   // quickFireDialog spins process() via setPumpFn
        }
    }

    printf("Exiting...");
    return 0;
}

void App::stop() {
    pthread_join(usb_thread, nullptr);
    running = false;
}