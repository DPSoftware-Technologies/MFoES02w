#include "app.h"
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <pthread.h>

#define RECV_BUF_SIZE (1 + FRAME_SIZE)

App::App()
#ifdef GFX_USE_OPENGL_ES
    :   gfx("/dev/dri/card0"),
#else
    :   gfx("/dev/fb0"),
#endif
        i2c("/dev/i2c-1"),
        adc(i2c, 0x48),
        frameReady(false)
{
    pthread_mutex_init(&frameMutex, nullptr);
    memset(frameBufA, 0, sizeof(frameBufA));
    memset(frameBufB, 0, sizeof(frameBufB));
}

App::~App() {
    pthread_mutex_destroy(&frameMutex);
}

void* App::usbThreadFunc(void* arg) {
    static_cast<App*>(arg)->usbLoop();
    return nullptr;
}

void App::usbLoop() {
    // DTS tile buffer: header + one tile
    uint32_t tile_buf_size = sizeof(TileUpdateHeader) + DTS_TILE_SIZE;
    uint8_t *buf = new uint8_t[tile_buf_size];

    while (true) {
        if (!usbdc.open(USBD_CHAN_AUTO)) {
            snprintf(statusMsg, sizeof(statusMsg), "USB: waiting...");
            sleep(2);
            continue;
        }
        snprintf(statusMsg, sizeof(statusMsg),
                 "USB: ch%d connected", usbdc.get_channel());

        while (usbdc.is_connected()) {
            int p = usbdc.poll(100);
            if (p <= 0) continue;

            ssize_t r = usbdc.recv(buf, tile_buf_size);
            if (r < 1) break;

            if (buf[0] == MSG_TILE_UPDATE) {
                TileUpdateHeader *hdr = (TileUpdateHeader*)buf;
                uint8_t  tx = hdr->tx;
                uint8_t  ty = hdr->ty;
                uint16_t tw = hdr->tw;
                uint16_t th = hdr->th;

                if (tx >= DTS_TILES_X || ty >= DTS_TILES_Y) continue;
                uint32_t expected = sizeof(TileUpdateHeader) + tw * th * 2;
                if ((uint32_t)r != expected) continue;

                uint16_t *pixels = (uint16_t*)(buf + sizeof(TileUpdateHeader));
                int x0 = tx * tw;
                int y0 = ty * th;

                pthread_mutex_lock(&frameMutex);
                for (int row = 0; row < th; row++) {
                    memcpy(
                        &frameBufA[(y0 + row) * SCREEN_W + x0],
                        &pixels[row * tw],
                        tw * 2
                    );
                }
                frameReady = true;
                pthread_mutex_unlock(&frameMutex);
                snprintf(statusMsg, sizeof(statusMsg), "DTS streaming");
            }
        }

        // Clear on disconnect
        pthread_mutex_lock(&frameMutex);
        memset(frameBufA, 0, FRAME_SIZE);
        frameReady = false;
        pthread_mutex_unlock(&frameMutex);

        snprintf(statusMsg, sizeof(statusMsg), "USB: disconnected, retrying...");
        usbdc.close();
        sleep(1);
    }

    delete[] buf;
}

void App::init() {
#ifndef GFX_USE_OPENGL_ES
    gfx.enableMultiBuffer(2);
#endif
    gfx.fillScreen(0x0000);
    gfx.swapBuffers();

    // Start USB in background, don't block init
    snprintf(statusMsg, sizeof(statusMsg), "USB: starting...");
    pthread_create(&usb_thread, nullptr, App::usbThreadFunc, this);
}

int App::run() {
    int sw = gfx.width();
    int sh = gfx.height();

    while (true) {
        pthread_mutex_lock(&frameMutex);
        if (frameReady) {
            memcpy(frameBufB, frameBufA, FRAME_SIZE);
        }
        bool hasFrame = frameReady;
        pthread_mutex_unlock(&frameMutex);

        if (hasFrame) {
            int ox = (sw - SCREEN_W) / 2;
            int oy = (sh - SCREEN_H) / 2;
            gfx.drawRGBBitmap(ox, oy, frameBufB, SCREEN_W, SCREEN_H);
        } else {
            gfx.fillScreen(0x0000);
            gfx.setCursor(10, 10);
            gfx.setTextColor(gfx.color565(0xFF, 0xFF, 0xFF), gfx.color565(0x00, 0x00, 0x00));
            gfx.writeText(statusMsg);
        }

        gfx.swapBuffers();
        usleep(16000);
    }
    return 0;
}