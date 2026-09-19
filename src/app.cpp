#include "app.h"
#include <cstdio>
#include <cstring>
#include <pthread.h>

App::App()
#ifndef DESKTOP
    :   gfx("/dev/fb0"),
        i2c("/dev/i2c-1"),
        touch(i2c, 23, 4),  // int_pin=23, rst_pin=4
        buz(0, 0),
        frameReady(false),
        led1(6, true), // GPIO6
        led2(26, true),  // GPIO26
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
    led1.set(1);
    snprintf(statusMsg, sizeof(statusMsg), "USB: starting...");
    pthread_create(&usb_thread, nullptr, App::usbThreadFunc, this);

    // Outbound recorded-UI stream. Waits for a remote to attach on its own
    // channel; until then render() skips the recording pass entirely.
    drStart();

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
        {
            std::lock_guard<std::mutex> lock(touchQueueMutex);
            touchQueue.push(e);
        }
#ifndef DESKTOP
        // Detach buzzer so the GT911 poll thread is never blocked waiting for it.
        std::thread([this]() { buz.set(1); usleep(25000); buz.set(0); }).detach();
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
    led1.set(0);

    // Control panel and RTC on SPI1. Runs after the buzzer test so its status
    // message is the last thing left on screen.
    if (!initNorthbridge()) {
        printf("Failed to initialize northbridge\n");
    }
#endif
}

void App::inputHandle() {
#ifdef DESKTOP 
    if (!gfx.processEvents()) { // if stop
        stop();
    }
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

    // Drain actions posted from other threads.
    //
    // Take the whole queue under the lock and release it before running
    // anything: a lock_guard living to the end of process() would hold the
    // mutex across render() and the frame sleep, so every postAction() from a
    // producer thread would block for a whole frame. That stalls the USB and
    // northbridge threads and shows up as laggy input.
    //
    // Swapping also means a callback may itself postAction() without
    // deadlocking on a non-recursive mutex.
    std::queue<std::function<void()>> actions;
    {
        std::lock_guard<std::mutex> lock(_actionQueueMutex);
        actions.swap(_actionQueue);
    }
    while (!actions.empty()) {
        auto fn = actions.front();
        actions.pop();
        fn();   // runs on main thread — safe to call UI/GFX
    }

    uint32_t nowMs = (uint32_t)(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count() & 0xFFFFFFFF);

    // update every 1 second
    if (nowMs - lastUpdate1 >= 500) {
        linfo.update();
        RRFSYSINFO = true;
        lastUpdate1 = nowMs;
    }

#ifndef DESKTOP
    nbService();   // clock check; returns immediately unless it is due
#endif

#ifndef DESKTOP
    // Heartbeat LED is hardware-only. Without this guard DESKTOP_BUILD does
    // not compile, since led2 is declared under the same condition.
    if (cycleCount % APP_FPS == 0) {
        led2.set(1);
    } else if (cycleCount % APP_FPS == 5) {
        led2.set(0);
    }
#endif

    ui.update(nowMs);

    render();
    usleep(fps_us);
    cycleCount++;
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

#ifndef DESKTOP
    // Stop the link before tearing down the mutex its callbacks touch.
    if (nb) {
        nb->stop();
        nb.reset();
    }
#endif

    pthread_mutex_destroy(&frameMutex);
#ifndef DESKTOP
    led2.set(0);
    led1.set(1);
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

    // Wake the DrawReplay sender so it sees running == false instead of
    // sitting out its wait_for timeout.
    if (drThreadRunning) {
        drCv.notify_all();
        struct timespec dts;
        clock_gettime(CLOCK_REALTIME, &dts);
        dts.tv_sec += 2;
        pthread_timedjoin_np(dr_thread, nullptr, &dts);
        drThreadRunning = false;
    }
    usleep(100000); 
    system("clear > /dev/fb0");
#endif
    gfx.stop();
}