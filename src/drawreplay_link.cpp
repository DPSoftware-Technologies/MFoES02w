#include "app.h"

#include <usbproto.h>
#include <uiinput.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// =============================================================================
// DrawReplay remote link
// =============================================================================
//
// Ships recorded UI draw commands off the board so a remote renderer can
// reproduce the screen from a few hundred bytes a frame instead of a 3.5 MB
// framebuffer.
//
// Two rules shape this file:
//
//   1. The render loop must never block on USB. A stalled or unplugged host
//      would otherwise stall the panel. Frames are handed to a sender thread
//      through a one-deep slot; if the sender is still busy, the older frame
//      is dropped. For a display stream the newest frame is the only one that
//      matters, so dropping is the correct behaviour, not a compromise.
//
//   2. Nothing is recorded unless someone is listening. drRemoteAttached()
//      gates the second draw pass in render().
//
// The stream carries whole DrawReplay blobs, which matters because usbd does
// not preserve message boundaries — see the note in libs/usbproto/usbproto.h.
// A blob is self-delimiting (its header carries 'DRP1' and a body length), so
// the receiver can resynchronise and cut records out of the byte stream.
//

// ----- desktop capture -------------------------------------------------------
//
// With no USB gadget on a desktop build, MFOES_DR_DUMP=<path> writes the same
// bytes the device would transmit, wrapped in the same MUBD frames, so the
// viewer can be developed and tested against a capture with no hardware.

namespace {

FILE *drDumpFile() {
    static FILE *f = nullptr;
    static bool  tried = false;
    if (!tried) {
        tried = true;
        const char *path = getenv("MFOES_DR_DUMP");
        if (path && *path) {
            f = fopen(path, "wb");
            if (!f) fprintf(stderr, "[dr] cannot open dump file %s\n", path);
            else    fprintf(stderr, "[dr] capturing draw stream to %s\n", path);
        }
    }
    return f;
}

/// Milliseconds from a monotonic clock. Used by drPublish, which is compiled
/// on every target, so this must live outside the device-only section.
uint32_t drNowMs() {
    return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace

bool App::drRemoteAttached() const {
#ifndef DESKTOP
    if (drRemoteUp) return true;
#endif
    return drDumpFile() != nullptr;
}

void App::drPublish(const std::vector<uint8_t>& blob) {
    if (blob.empty()) return;

    // Drop a frame identical to the one already sent. drLastSent is only ever
    // touched here, on the render thread, so it needs no lock; a reconnect
    // asks for the next frame unconditionally via drForceResend.
    const uint32_t nowMs = drNowMs();
    bool force = drForceResend.exchange(false);

    // Even with no request, resend periodically so a viewer can never be left
    // waiting on a screen that simply never changes.
    if (!force && drKeyframeMs && (nowMs - drLastSentMs) >= drKeyframeMs) {
        force = true;
    }

    if (force) {
        drLastSent.clear();
    } else if (blob == drLastSent) {
        ++drFramesSkipped;
        return;
    }
    drLastSent   = blob;
    drLastSentMs = nowMs;

    if (FILE *f = drDumpFile()) {
        std::vector<uint8_t> frame;
        usbproto::encodeFrame(frame, usbproto::CHAN_DRAWREPLAY, usbproto::FLAG_DATA,
                              blob.data(), blob.size());
        fwrite(frame.data(), 1, frame.size(), f);
        fflush(f);
    }

#ifndef DESKTOP
    {
        std::lock_guard<std::mutex> lk(drMutex);
        if (drHasPending) ++drFramesDropped;   // newest frame wins
        drPending = blob;
        drHasPending = true;
    }
    drCv.notify_one();
#endif
}

#ifndef DESKTOP

void* App::drThreadFunc(void* arg) {
    static_cast<App*>(arg)->drLoop();
    return nullptr;
}

bool App::drStart() {
    if (drThreadRunning) return true;
    drThreadRunning = true;
    if (pthread_create(&dr_thread, nullptr, App::drThreadFunc, this) != 0) {
        drThreadRunning = false;
        fprintf(stderr, "[dr] failed to start sender thread\n");
        return false;
    }
    return true;
}

void App::drHandleInput(const uint8_t* data, size_t len) {
    // One datagram may carry several records, and they are not all the same
    // size, so dispatch on the magic rather than striding blindly.
    size_t off = 0;
    while (off < len) {
        const uint8_t* p = data + off;
        const size_t   n = len - off;

        if (uiinput::isCtrl(p, n)) {
            uiinput::CtrlRecord c;
            if (!uiinput::decodeCtrl(p, n, c)) break;
            off += uiinput::CTRL_SIZE;

            if (c.cmd == uiinput::CTRL_REQUEST_FULL_FRAME) {
                // A viewer just attached with a blank window. Closing a viewer
                // never reaches us — the socket to usbd stays up — so without
                // this the dedup would keep skipping and leave it black.
                drForceResend = true;
            }
            continue;
        }

        if (!uiinput::isTouch(p, n)) break;   // unknown record; drop the rest

        uiinput::TouchRecord tr;
        if (!uiinput::decode(p, n, tr)) break;
        off += uiinput::RECORD_SIZE;

        TouchEventData e{};
        switch (tr.event) {
            case uiinput::EV_PRESS:   e.event = TouchEvent::PRESS;   break;
            case uiinput::EV_RELEASE: e.event = TouchEvent::RELEASE; break;
            case uiinput::EV_MOVE:    e.event = TouchEvent::MOVE;    break;
            case uiinput::EV_HOLD:    e.event = TouchEvent::HOLD;    break;
            default: continue;                 // decode() already range-checks
        }

        // The far end is untrusted input: clamp rather than let a bad
        // coordinate reach widget hit-testing.
        e.point.id     = tr.id;
        e.point.x      = tr.x < SCREEN_W ? tr.x : (uint16_t)(SCREEN_W - 1);
        e.point.y      = tr.y < SCREEN_H ? tr.y : (uint16_t)(SCREEN_H - 1);
        e.point.size   = tr.size;
        e.point.active = tr.active != 0;
        e.dx           = tr.dx;
        e.dy           = tr.dy;
        e.duration_ms  = tr.duration_ms;

        {
            std::lock_guard<std::mutex> lock(touchQueueMutex);
            touchQueue.push(e);
        }
        ++drTouchesIn;
        continue;
    }
}

void App::drLoop() {
    using namespace std::chrono_literals;

    // Touch records are tiny; this only has to hold one datagram's worth.
    std::vector<uint8_t> rx(1024);

    while (running) {
        // A channel of its own, so the draw stream can never interfere with
        // the inbound DTS video on channel 0 or with otad on channel 10.
        if (!usbdcDR.open(usbproto::CHAN_DRAWREPLAY)) {
            std::this_thread::sleep_for(1s);
            continue;
        }

        // The far end starts with a blank window, so the next frame must be
        // sent even if it matches what the previous session last saw.
        drForceResend = true;
        drRemoteUp    = true;
        fprintf(stderr, "[dr] remote attached on channel %d\n",
                usbproto::CHAN_DRAWREPLAY);

        bool linkDown = false;
        while (running && usbdcDR.is_connected() && !linkDown) {
            // Inbound first, and without blocking: a tap should reach the UI
            // on the next frame, not after the outbound wait expires.
            while (usbdcDR.poll(0) > 0) {
                ssize_t r = usbdcDR.recv(rx.data(), (uint32_t)rx.size());
                if (r < 0) { linkDown = true; break; }
                if (r > 0) drHandleInput(rx.data(), (size_t)r);
            }
            if (linkDown) break;

            // Short wait so the inbound poll above stays responsive; the loop
            // is idle-cheap because it blocks on the condition variable.
            std::vector<uint8_t> frame;
            {
                std::unique_lock<std::mutex> lk(drMutex);
                drCv.wait_for(lk, 20ms, [this]{ return drHasPending || !running; });
                if (drHasPending) {
                    frame.swap(drPending);        // take it, leave the slot empty
                    drHasPending = false;
                }
            }

            if (!frame.empty()) {
                if (usbdcDR.send(frame.data(), (uint32_t)frame.size()) < 0) break;
                ++drFramesSent;
            }
        }

        drRemoteUp = false;
        usbdcDR.close();
        fprintf(stderr,
                "[dr] remote detached (sent %u, unchanged-skipped %u, dropped %u, "
                "touches in %u)\n",
                drFramesSent, drFramesSkipped, drFramesDropped, drTouchesIn);

        if (running) std::this_thread::sleep_for(1s);
    }
}

#endif // !DESKTOP
