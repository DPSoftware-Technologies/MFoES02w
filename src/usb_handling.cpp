#include "app.h"

void* App::usbThreadFunc(void* arg) {
    static_cast<App*>(arg)->usbLoop();
    return nullptr;
}

void App::usbLoop() {
    // DTS tile buffer: header + one tile
    uint32_t tile_buf_size = sizeof(TileUpdateHeader) + DTS_TILE_SIZE;
    uint8_t *buf = new uint8_t[tile_buf_size];

    // if not connect in 15s = timeout
    int wait_elapsed = 0;          // seconds waited without connection
    bool timeoutShown = false;     // only show the dialog once per disconnect

    while (running) {
        if (!usbdc.open(USBD_CHAN_AUTO)) {
            snprintf(statusMsg, sizeof(statusMsg), "USB: waiting... (%ds)", wait_elapsed);
            RRFSYSMSG = true;
            sleep(1);
            wait_elapsed += 1;

            if (wait_elapsed >= 15 && !timeoutShown) {
                timeoutShown = true;
                postAction([this]() {
                    defer([this]() {
                        ui.quickFireDialog(SCREEN_W, SCREEN_H,
                            "USB Timeout",
                            "No USB device connected\nafter 15 seconds. Please restart or re-plug.",
                            uisys::DialogMode::Notice,
                            uisys::DialogIcon::Error);
                    });
                });
            }
            continue;
        }
        snprintf(statusMsg, sizeof(statusMsg), "USB: ch%d connected", usbdc.get_channel());
        RRFSYSMSG = true;

        while (usbdc.is_connected()) {
            int p = usbdc.poll(100);
            if (p <= 0) continue;

            ssize_t r = usbdc.recv(buf, tile_buf_size);
            if (r < 1) break;

            // Update general data
            if (buf[0] == MSG_UPDATE_VALUE) {
                snprintf(statusMsg, sizeof(statusMsg), "CV: Updating...");
                if (r >= sizeof(CValue)) {
                    memcpy(&cvdata, buf, sizeof(CValue));
                    snprintf(statusMsg, sizeof(statusMsg), "CV: Updated");
                } else {
                    snprintf(statusMsg, sizeof(statusMsg), "CV: Update Error");
                }
                RRFSYSMSG = true;
            }

            // update TILE
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
                snprintf(statusMsg, sizeof(statusMsg), "DTS streaming - tile (%d, %d) updated", tx, ty);
                RRFSYSMSG = true;
            }
        }

        // Clear on disconnect
        pthread_mutex_lock(&frameMutex);
        memset(frameBufA, 0, FRAME_SIZE);
        frameReady = false;
        pthread_mutex_unlock(&frameMutex);

        snprintf(statusMsg, sizeof(statusMsg), "USB: disconnected, retrying...");
        RRFSYSMSG = true;
        usbdc.close();
        sleep(1);
    }

    delete[] buf;
}
