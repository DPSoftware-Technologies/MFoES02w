#include "app.h"

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

            // Update general data
            if (buf[0] == MSG_UPDATE_VALUE && r >= 17) {
                memcpy(&cvdata, buf, sizeof(CValue));
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
