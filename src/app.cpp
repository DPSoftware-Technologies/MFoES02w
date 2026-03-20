#include "app.h"
#include <iostream>
#include <unistd.h>
#include <cmath>
#include <pthread.h>


App::App()
#ifdef GFX_USE_OPENGL_ES
    :   gfx("/dev/dri/card0"),
#else
    :   gfx("/dev/fb0"),
#endif
        i2c("/dev/i2c-1"),
        adc(i2c, 0x48)
{
}

App::~App() {} // Destructor (if needed)

void* App::usbThreadFunc(void* arg) {
    static_cast<App*>(arg)->usbLoop();
    return nullptr;
}

void App::usbLoop() {
    while (true) {
        if (!usbdc.open(USBD_CHAN_AUTO)) {
            snprintf(statusMsg, sizeof(statusMsg), "USB: waiting for usbd...");
            sleep(2);
            continue;
        }
        snprintf(statusMsg, sizeof(statusMsg), "USB: ch%d connected", usbdc.get_channel());

        uint8_t buf[512];
        while (usbdc.is_connected()) {
            int p = usbdc.poll(100);
            if (p <= 0) continue;
            ssize_t r = usbdc.recv(buf, sizeof(buf));
            if (r < 0) break;

            // Update display
            snprintf(statusMsg, sizeof(statusMsg), "USB rx: %.*s", (int)r, buf);

            // Echo back so PC gets a reply ← ADD THIS
            usbdc.send(buf, (uint32_t)r);
        }

        snprintf(statusMsg, sizeof(statusMsg), "USB: disconnected, retrying...");
        usbdc.close();
        sleep(1);
    }
}

void App::init() {
// Multi-buffering is only supported in OpenGL ES back-end
#ifndef GFX_USE_OPENGL_ES
    gfx.enableMultiBuffer(2);
#endif
    // Clear screen to black
    gfx.fillScreen(0x0000); 
    gfx.swapBuffers();

    // Start USB in background, don't block init
    snprintf(statusMsg, sizeof(statusMsg), "USB: starting...");
    pthread_create(&usb_thread, nullptr, App::usbThreadFunc, this);
}

void App::drawGauge(int x, int y, int r, float value, float minVal, float maxVal) {
    // 1. Draw the scale arc (semi-circle)
    for (int i = 0; i <= 180; i += 5) {
        float rad = (180 + i) * M_PI / 180.0;
        int x1 = x + (r - 5) * cos(rad);
        int y1 = y + (r - 5) * sin(rad);
        int x2 = x + r * cos(rad);
        int y2 = y + r * sin(rad);
        gfx.drawLine(x1, y1, x2, y2, 0xFFFF);
    }

    // 2. Map value to angle (180 degrees to 0 degrees)
    float angle = ((value - minVal) / (maxVal - minVal) * 180.0);
    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;
    
    float rad = (180 + angle) * M_PI / 180.0;
    int nx = x + (r - 10) * cos(rad);
    int ny = y + (r - 10) * sin(rad);

    // 3. Draw Needle
    gfx.drawLine(x, y, nx, ny, 0xF800); // Red needle
}

int App::run() {
    while (true) {
        float ch0 = adc.readVoltage(ADS1115::AIN0_GND, ADS1115::PGA_6V144);

        gfx.fillScreen(0x0000); 

        // Draw gauge at center of screen, radius 100
        drawGauge(240, 200, 100, ch0, 0.0, 4.096);

        char buf[32];
        snprintf(buf, sizeof(buf), "%.3f V", ch0);
        gfx.setCursor(210, 210);
        gfx.writeText(buf);

        // show last status message from usbd client
        gfx.setCursor(10, 10);
        gfx.writeText(statusMsg);

        gfx.swapBuffers();
        usleep(50000); // 50ms for smoother updates
    }
    return 0;
}