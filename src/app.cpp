#include "app.h"
#include <cstdio>
#include <cstring>
#include <pthread.h>

App::App()
#ifndef DESKTOP
    :   gfx("/dev/fb0"),
        i2c("/dev/i2c-1"),
        touch(i2c, 17, 27),  // int_pin=17, rst_pin=27
        buz(0, 0),
        frameReady(false),
#else
    :   gfx("MFoES02w Demo", 1280, 720),
#endif
        ui(0, 400, 1280, 320, uisys::Font::Medium())
{
    pthread_mutex_init(&frameMutex, nullptr);
#ifndef DESKTOP
    memset(frameBufA, 0, sizeof(frameBufA));
    memset(frameBufB, 0, sizeof(frameBufB));
#endif    
}

App::~App() {
    if (running) {
        stop();
    }
}

void App::init() {
    gfx.enableMultiBuffer(2);
    gfx.fillScreen(GFX_BLACK);
    gfx.swapBuffers();

    // Start USB in background, don't block init
#ifndef DESKTOP 
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
#ifndef DESKTOP
        buz.set(1); 
        usleep(25000); 
        buz.set(0); 
#endif
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
#else
    gfx.setEventCallback([this](const GFXInputEvent& event) {
        TouchEventData te;
        te.point.id = 0;
        te.point.x = (uint16_t)event.x;
        te.point.y = (uint16_t)event.y;
        te.point.active = true;

        bool shouldPush = true;
        const char* typeStr = nullptr;

        switch (event.type) {
            case GFXEventType::MOUSE_BUTTON_DOWN:
                te.event = TouchEvent::PRESS;
                typeStr = "Press";
                break;
            case GFXEventType::MOUSE_MOVE:
                te.event = TouchEvent::MOVE;
                // Don't update statusMsg for move to avoid flooding the mutex
                break;
            case GFXEventType::MOUSE_BUTTON_UP:
                te.event = TouchEvent::RELEASE;
                te.point.active = false;
                typeStr = "Release";
                break;
            default:
                return;
        }

        // Keep statusMsg logic consistent with Embedded build
        if (typeStr) {
            pthread_mutex_lock(&frameMutex);
            snprintf(statusMsg, sizeof(statusMsg), "Mouse: %s at (%d, %d)", typeStr, te.point.x, te.point.y);
            pthread_mutex_unlock(&frameMutex);
        }

        std::lock_guard<std::mutex> lock(touchQueueMutex);
        touchQueue.push(te);
    });
#endif
    // init UI
    initSysUI();
    initSidebarBTNs();
    initDemoUI();

    // Register pump for quickFireDialog
    ui.setPumpFn([this]() {
        process();
    });

#ifndef DESKTOP 
    // buzzer test
    buz.enable();
    for (float n : {750.0f, 1000.0f}) {
        buz.disable();
        buz.setFrequencyHz(n, 50.0f);
        buz.enable();
        usleep(100000);
    }
    buz.disable();

    buz.setFrequencyHz(1000.0f, 50.0f); 
#endif
}

void App::inputHandle() {
#ifdef DESKTOP 
    gfx.processEvents();
#endif
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
#ifndef DESKTOP 
    pthread_mutex_lock(&frameMutex);
    if (frameReady) {
        memcpy(frameBufB, frameBufA, FRAME_SIZE); 
        hasFrame = frameReady;
        RRFDTS = true;
    }
    pthread_mutex_unlock(&frameMutex);
#endif
    inputHandle();

    // Drain actions posted from other threads
    std::lock_guard<std::mutex> lock(_actionQueueMutex);
    while (!_actionQueue.empty()) {
        auto fn = _actionQueue.front();
        _actionQueue.pop();
        fn();   // runs on main thread — safe to call UI/GFX
    }

    uint32_t nowMs = (uint32_t)(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count() & 0xFFFFFFFF);
    ui.update(nowMs);

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

void App::ostop(bool restart) {
    stop();
#ifndef DESKTOP 
    buz.setFrequencyHz(450.0f, 50.0f);

    if (restart) {
        buz.enable();
        usleep(200000); 
        buz.disable();

        system("reboot");
    } else {
        for (int i = 0; i < 2; i++) {
            buz.enable();
            usleep(100000); 
            buz.disable();
            usleep(25000); 
        }

        system("halt");
    }
#endif
}

void App::stop() {
    running = false; 

    pthread_mutex_destroy(&frameMutex);
#ifndef DESKTOP 
    buz.setFrequencyHz(500.0f, 50.0f);
    buz.enable();
    usleep(50000); 
    buz.disable();

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 2; // Set a 2-second timeout

    int s = pthread_timedjoin_np(usb_thread, nullptr, &ts);
    if (s == ETIMEDOUT) {
        // Thread didn't stop in time, move on anyway
    }
    usleep(100000); 
    system("clear > /dev/fb0");
#endif
    gfx.stop();
}