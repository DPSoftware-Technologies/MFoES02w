// =============================================================================
// remote_viewer — render an MFoES screen from recorded draw commands
// =============================================================================
//
// The board records its UI as DrawReplay commands and ships them over a usbd
// channel. This program reads that stream and replays it into a window, so the
// screen is reproduced from a few hundred bytes a frame instead of a 3.5 MB
// framebuffer.
//
//   remote_viewer                      # attach over USB
//   remote_viewer --probe              # dump what the gadget exposes, then exit
//   remote_viewer --file frames.bin    # replay a capture, no board needed
//   remote_viewer --file frames.bin --loop --chunk 64
//
// A capture comes from a desktop build of the firmware:
//   MFOES_DR_DUMP=frames.bin ./mfoes02w
//
// Nothing here knows the command encoding: DrawReplay::DRRender does the
// decoding and DrawReplay::DRRecordSize says where one blob ends, so the wire
// format lives in exactly one place.

#include <GFX.h>
#include <DrawReplay.h>

#include <usbproto.h>
#include <usbproto_session.h>
#include <usbproto_file.h>
#include <uiinput.h>

#ifdef USBPROTO_WITH_LIBUSB
#include <usbproto_libusb.h>
#endif

#ifdef USBPROTO_WITH_BROKER
#include <usbproto_broker.h>
#endif

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

namespace {

struct Options {
    const char *file    = nullptr;
    bool        loop    = false;
    size_t      chunk   = 0;
    uint16_t    vid     = usbproto::DEFAULT_VID;
    uint16_t    pid     = usbproto::DEFAULT_PID;
    uint8_t     channel = usbproto::CHAN_DRAWREPLAY;
    int         width   = 1280;
    int         height  = 720;
    bool        stats   = false;
    bool        hold    = false;
    bool        verbose = false;
    bool        probe   = false;
    bool        noInput = false;
    const char *record  = nullptr;
    // Defaults mirror the board: setHoldDuration(500) and a moveThreshold of 1,
    // which GT911 multiplies by 3 to decide a finger has stayed put.
    uint32_t    holdMs        = 500;
    int         holdTolerance = 3;
    bool        broker        = false;
    uint16_t    brokerPort    = 7311;
};

void usage (const char *argv0) {
    printf(
        "Usage: %s [options]\n"
        "\n"
        "  --file <path>   Replay a captured frame dump instead of opening USB\n"
        "  --loop          Restart the capture at end of file\n"
        "  --chunk <n>     Feed the capture n bytes at a time (exercises reassembly)\n"
        "  --probe         Print the gadget's interfaces and endpoints, then exit\n"
        "  --vid <hex>     USB vendor id  (default %04x)\n"
        "  --pid <hex>     USB product id (default %04x)\n"
        "  --channel <n>   usbd channel to listen on (default %d)\n"
        "  --size <w>x<h>  Window size (default 1280x720)\n"
        "  --stats         Print a line per frame\n"
        "  --hold          Keep the window open after a capture ends\n"
        "  --verbose       Turn on libusb's own logging\n"
        "  --no-input      Watch only; do not send mouse clicks back as touch\n"
        "  --record <path> Save the incoming frames; replay later with --file\n"
        "  --hold-ms <n>   Hold threshold for press-and-hold, 0 disables (default 500)\n"
        "  --broker        Share the USB link: relay channels to local clients\n"
        "  --broker-port <n>  Broker port on 127.0.0.1 (default 7311)\n"
        "  --help\n",
        argv0, usbproto::DEFAULT_VID, usbproto::DEFAULT_PID,
        (int)usbproto::CHAN_DRAWREPLAY);
}

bool parseArgs (int argc, char **argv, Options &o) {
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        auto next = [&](const char *what) -> const char * {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s needs a value\n", what);
                return nullptr;
            }
            return argv[++i];
        };

        if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(argv[0]); return false; }
        else if (!strcmp(a, "--file"))    { const char *v = next(a); if (!v) return false; o.file = v; }
        else if (!strcmp(a, "--loop"))    { o.loop = true; }
        else if (!strcmp(a, "--stats"))   { o.stats = true; }
        else if (!strcmp(a, "--hold"))    { o.hold = true; }
        else if (!strcmp(a, "--verbose")) { o.verbose = true; }
        else if (!strcmp(a, "--probe"))   { o.probe = true; }
        else if (!strcmp(a, "--no-input")) { o.noInput = true; }
        else if (!strcmp(a, "--record"))  { const char *v = next(a); if (!v) return false; o.record = v; }
        else if (!strcmp(a, "--hold-ms")) { const char *v = next(a); if (!v) return false; o.holdMs = (uint32_t)strtoul(v, nullptr, 0); }
        else if (!strcmp(a, "--broker"))  { o.broker = true; }
        else if (!strcmp(a, "--broker-port")) { const char *v = next(a); if (!v) return false; o.brokerPort = (uint16_t)strtoul(v, nullptr, 0); o.broker = true; }
        else if (!strcmp(a, "--chunk"))   { const char *v = next(a); if (!v) return false; o.chunk = (size_t)strtoul(v, nullptr, 0); }
        else if (!strcmp(a, "--vid"))     { const char *v = next(a); if (!v) return false; o.vid = (uint16_t)strtoul(v, nullptr, 16); }
        else if (!strcmp(a, "--pid"))     { const char *v = next(a); if (!v) return false; o.pid = (uint16_t)strtoul(v, nullptr, 16); }
        else if (!strcmp(a, "--channel")) { const char *v = next(a); if (!v) return false; o.channel = (uint8_t)strtoul(v, nullptr, 0); }
        else if (!strcmp(a, "--size")) {
            const char *v = next(a); if (!v) return false;
            if (sscanf(v, "%dx%d", &o.width, &o.height) != 2) {
                fprintf(stderr, "--size wants WxH, e.g. 1280x720\n");
                return false;
            }
        }
        else { fprintf(stderr, "unknown option: %s\n", a); usage(argv[0]); return false; }
    }
    return true;
}

void printUsbHints (const Options &o) {
    fprintf(stderr,
        "\nThe device was opened, so it is plugged in and a driver is bound.\n"
        "A read failing straight away usually means one of:\n"
        "  1. usbd is not running on the Pi — it owns the USB gadget, and\n"
        "     without it the endpoints exist but nothing ever answers.\n"
        "  2. Nothing has attached to channel %u on the board. mfoes02w only\n"
        "     opens it when the app is running.\n"
        "  3. Windows has a vendor driver bound instead of WinUSB. libusb\n"
        "     cannot transfer without WinUSB/libusbK — use Zadig to rebind.\n"
        "  4. Another program holds the device (usb_channel.py, a second viewer).\n"
        "\nRun --probe to see the endpoints the gadget actually exposes,\n"
        "and --verbose for libusb's own log.\n",
        (unsigned)o.channel);
}

} // namespace

int main (int argc, char **argv) {
#ifdef _WIN32
    // The Windows console defaults to a legacy code page, which renders this
    // program's punctuation as mojibake. Ask for UTF-8 once, rather than
    // flattening every string in the file.
    SetConsoleOutputCP(CP_UTF8);
#endif

    Options opt;
    if (!parseArgs(argc, argv, opt)) return 1;

    // ----- probe -------------------------------------------------------------
    if (opt.probe) {
#ifdef USBPROTO_WITH_LIBUSB
        std::string report;
        const bool found = usbproto::describeDevice(opt.vid, opt.pid, report);
        fputs(report.c_str(), stdout);
        return found ? 0 : 1;
#else
        fprintf(stderr, "Built without libusb support — --probe unavailable.\n");
        return 1;
#endif
    }

    // ----- transport ---------------------------------------------------------
    std::unique_ptr<usbproto::Transport> transport;

    if (opt.file) {
        auto ft = std::unique_ptr<usbproto::FileTransport>(new usbproto::FileTransport());
        if (!ft->open(opt.file, opt.loop)) {
            fprintf(stderr, "%s\n", ft->lastError());
            return 1;
        }
        if (opt.chunk) ft->setChunkSize(opt.chunk);
        printf("replaying capture %s%s\n", opt.file, opt.loop ? " (looping)" : "");
        transport = std::move(ft);
    } else {
#ifdef USBPROTO_WITH_LIBUSB
        auto lt = std::unique_ptr<usbproto::LibusbTransport>(new usbproto::LibusbTransport());
        lt->setDebug(opt.verbose);                 // must precede open()
        if (!lt->open(opt.vid, opt.pid)) {
            fprintf(stderr, "USB: %s\n", lt->lastError());
            return 1;
        }
        printf("attached to %04x:%04x  (bulk OUT 0x%02x, IN 0x%02x)\n",
               opt.vid, opt.pid, lt->endpointOut(), lt->endpointIn());
        transport = std::move(lt);
#else
        fprintf(stderr,
                "Built without libusb support — use --file <capture>,\n"
                "or rebuild with -DUSBPROTO_WITH_LIBUSB=ON.\n");
        return 1;
#endif
    }
    fflush(stdout);

    // Frames are saved in the same shape they arrive in — one MUBD frame per
    // blob — so a recording replays through --file with no special handling.
    FILE *recFile = nullptr;
    if (opt.record) {
        recFile = fopen(opt.record, "wb");
        if (!recFile) {
            fprintf(stderr, "cannot write %s\n", opt.record);
            return 1;
        }
        printf("recording frames to %s\n", opt.record);
    }

    // ----- window ------------------------------------------------------------
    LinuxGFX gfx("MFoES remote viewer", (uint16_t)opt.width, (uint16_t)opt.height);
    gfx.fillScreen(GFX_BLACK);
    gfx.swapBuffers();

    // ----- session -----------------------------------------------------------
    usbproto::Session session(transport.get());

    bool     dirty       = false;
    bool     sizeChecked = false;
    uint64_t frames      = 0;
    uint64_t totalBytes  = 0;
    uint64_t touchesSent = 0;

    // Panel size, learned from the first blob's header. Mouse coordinates are
    // mapped through this, so the window does not have to match the panel.
    int deviceW = opt.width;
    int deviceH = opt.height;

    session.setChannelReassembler(
        opt.channel,
        &DrawReplay::DRRecordSize,
        DrawReplay::DRSyncMagic(),
        DrawReplay::DR_SYNC_LEN,
        [&](const uint8_t *blob, size_t len) {
            // Warn once if the board is drawing at a different size than the
            // window, rather than silently cropping every frame.
            if (!sizeChecked) {
                sizeChecked = true;
                int16_t  w = 0, h = 0;
                uint32_t cmds = 0, body = 0;
                if (DrawReplay::DRPeek(blob, len, w, h, cmds, body) == DRError::Ok &&
                    w > 0 && h > 0) {
                    deviceW = w;
                    deviceH = h;
                    if (w != opt.width || h != opt.height) {
                        fprintf(stderr,
                                "note: board draws %dx%d but the window is %dx%d — "
                                "pass --size %dx%d (touch is mapped either way)\n",
                                w, h, opt.width, opt.height, w, h);
                    }
                }
            }

            // Start every frame black, matching what the board's own
            // swapBuffers() autoclear does to its draw buffer. A blob only
            // describes what that frame paints, so without this the frames
            // accumulate on top of each other. Older firmware does not put the
            // clear in the stream, so do not rely on the blob carrying one.
            gfx.fillScreen(GFX_BLACK);

            const DRError err = DrawReplay::DRRender(blob, len, gfx);
            if (err != DRError::Ok) {
                fprintf(stderr, "frame rejected: %s\n", DRErrorString(err));
                return;
            }

            ++frames;
            totalBytes += len;
            dirty = true;

            if (recFile) {
                std::vector<uint8_t> frame;
                usbproto::encodeFrame(frame, opt.channel, usbproto::FLAG_DATA,
                                      blob, len);
                fwrite(frame.data(), 1, frame.size(), recFile);
                // A recording is normally ended with Ctrl-C, which never runs
                // the fclose below. Flush each frame so an interrupted session
                // still leaves a complete, replayable file.
                fflush(recFile);
            }

            if (opt.stats) {
                const double raw = (double)opt.width * opt.height * 4.0;
                printf("frame %llu  %zu bytes  (%.0fx smaller than a raw frame)\n",
                       (unsigned long long)frames, len, raw / (double)len);
            }
        });

    session.setChannelOpenedHandler([](uint8_t ch) {
        printf("channel %u opened by device\n", (unsigned)ch);
    });
    session.setChannelClosedHandler([](uint8_t ch) {
        printf("channel %u closed\n", (unsigned)ch);
    });

    // ----- touch emulation ---------------------------------------------------
    //
    // Mouse in the window becomes touch on the board. A panel has no hover, so
    // motion is only sent while the button is down — otherwise the board would
    // track a finger that never landed.
    bool     pointerDown = false;
    int      lastX = 0, lastY = 0;
    int      pressX = 0, pressY = 0;
    uint32_t pressMs  = 0;
    bool     holdSent = false;

    // Announce ourselves. The board cannot see a viewer attach — usbd answers
    // the channel's FLAG_OPEN itself — so without this it keeps skipping
    // unchanged frames and we stay blank. Sent even with --no-input: a watcher
    // needs the first frame just as much.
    auto requestFullFrame = [&]() {
        uint8_t rec[uiinput::CTRL_SIZE];
        uiinput::CtrlRecord c;
        c.cmd = uiinput::CTRL_REQUEST_FULL_FRAME;
        uiinput::encodeCtrl(rec, c);
        session.sendData(opt.channel, rec, sizeof(rec));
    };

    auto sendTouch = [&](const uiinput::TouchRecord& r) {
        uint8_t rec[uiinput::RECORD_SIZE];
        uiinput::encode(rec, r);
        if (!session.sendData(opt.channel, rec, sizeof(rec))) {
            fprintf(stderr, "touch send failed: %s\n", transport->lastError());
            return;
        }
        ++touchesSent;
    };

    if (!opt.noInput) {
        gfx.setEventCallback([&](const GFXInputEvent& ev) {
            const uint32_t nowMs = (uint32_t)std::chrono::duration_cast<
                std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();

            const int wW = opt.width  > 0 ? opt.width  : 1;
            const int wH = opt.height > 0 ? opt.height : 1;

            int px = (int)ev.x * deviceW / wW;
            int py = (int)ev.y * deviceH / wH;
            if (px < 0) px = 0;
            if (py < 0) py = 0;
            if (px >= deviceW) px = deviceW - 1;
            if (py >= deviceH) py = deviceH - 1;

            uiinput::TouchRecord r;
            r.id   = 0;
            r.x    = (uint16_t)px;
            r.y    = (uint16_t)py;
            r.size = 1;

            switch (ev.type) {
            case GFXEventType::MOUSE_BUTTON_DOWN:
                pointerDown  = true;
                pressMs      = nowMs;
                pressX       = px;      // hold is judged against the press point
                pressY       = py;
                holdSent     = false;
                r.event      = uiinput::EV_PRESS;
                r.active     = 1;
                break;

            case GFXEventType::MOUSE_BUTTON_UP:
                if (!pointerDown) return;          // release without our press
                pointerDown  = false;
                holdSent     = false;
                r.event      = uiinput::EV_RELEASE;
                r.active     = 0;
                r.duration_ms = nowMs - pressMs;
                break;

            case GFXEventType::MOUSE_MOVE:
                if (!pointerDown) return;          // no hover on a touch panel
                if (px == lastX && py == lastY) return;   // nothing actually moved
                r.event      = uiinput::EV_MOVE;
                r.active     = 1;
                r.duration_ms = nowMs - pressMs;
                r.dx         = (int16_t)(px - lastX);
                r.dy         = (int16_t)(py - lastY);
                break;

            default:
                return;
            }

            lastX = px;
            lastY = py;

            sendTouch(r);
        });
    }

    // Ask the board to open the channel. Harmless on a capture, where send()
    // is discarded.
    session.openChannel(opt.channel);
    requestFullFrame();

    uint32_t lastRequestMs = (uint32_t)std::chrono::duration_cast<
        std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

    // ----- broker ------------------------------------------------------------
    //
    // libusb claims the interface exclusively, so while this process holds the
    // gadget, usb_channel.py and everything built on it are locked out. With
    // --broker this process owns the link and relays frames to local clients,
    // letting the draw stream, DTS video and OTA run at the same time.
#ifdef USBPROTO_WITH_BROKER
    usbproto::Broker broker;
    const bool brokerOn = opt.broker && !opt.file;

    if (brokerOn) {
        if (!broker.start(opt.brokerPort)) {
            fprintf(stderr, "broker: %s\n", broker.lastError());
            return 1;
        }
        printf("broker on 127.0.0.1:%u — other tools can attach there\n",
               (unsigned)opt.brokerPort);

        // Everything arriving from the device goes to every client, which
        // filters by channel. This runs before the channel routing below, so
        // the viewer still renders channel 11 itself.
        session.setFrameHandler([&](uint8_t ch, uint8_t flags,
                                    const uint8_t *payload, size_t len) {
            std::vector<uint8_t> frame;
            usbproto::encodeFrame(frame, ch, flags, payload, len);
            broker.broadcast(frame.data(), frame.size());
        });
    }
#endif

    printf("listening on channel %u — close the window to quit\n",
           (unsigned)opt.channel);
    fflush(stdout);

    // ----- pump --------------------------------------------------------------
    bool running = true;
    while (running) {
        const int n = session.pump(50);
        if (n < 0) {
            if (opt.file) {
                printf("end of capture\n");
                break;
            }
            const char *why = transport->lastError();
            fprintf(stderr, "transport error — disconnected%s%s\n",
                    (why && *why) ? ": " : "",
                    (why && *why) ? why  : "");
            printUsbHints(opt);
            break;
        }

#ifdef USBPROTO_WITH_BROKER
        // Accept clients and push whatever they sent onto the USB pipe. The
        // broker hands over whole frames only, so two clients writing at once
        // cannot interleave and corrupt the stream.
        if (brokerOn) {
            broker.poll(0, [&](const uint8_t *frame, size_t len) {
                transport->send(frame, len);
            });
        }
#endif

        // Keep asking until the first frame lands. The board may not have been
        // listening when we first announced ourselves — it only reads between
        // outbound frames — and one lost request should not mean a blank window.
        if (frames == 0 && !opt.file) {
            const uint32_t nowMs = (uint32_t)std::chrono::duration_cast<
                std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
            if (nowMs - lastRequestMs >= 1000) {
                requestFullFrame();
                lastRequestMs = nowMs;
            }
        }

        // GT911 raises HOLD from a timer while the finger sits still, so there
        // is no touch event carrying it — and a motionless mouse produces no
        // SDL events either. Synthesise it here on the driver's own rules:
        // once per press, and only while the pointer has barely moved.
        if (!opt.noInput && pointerDown && !holdSent && opt.holdMs > 0) {
            const uint32_t nowMs = (uint32_t)std::chrono::duration_cast<
                std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
            const int ddx = lastX - pressX;
            const int ddy = lastY - pressY;
            if (nowMs - pressMs >= opt.holdMs &&
                (ddx * ddx + ddy * ddy) <= opt.holdTolerance * opt.holdTolerance) {
                uiinput::TouchRecord r;
                r.event       = uiinput::EV_HOLD;
                r.id          = 0;
                r.x           = (uint16_t)lastX;
                r.y           = (uint16_t)lastY;
                r.size        = 1;
                r.active      = 1;
                r.duration_ms = nowMs - pressMs;
                sendTouch(r);
                holdSent = true;
            }
        }

        if (dirty) {
            gfx.swapBuffers();
            dirty = false;
        }

        if (!gfx.processEvents()) running = false;
    }

    printf("\n%llu frames, %llu bytes total",
           (unsigned long long)frames, (unsigned long long)totalBytes);
    if (frames) {
        const double raw = (double)opt.width * opt.height * 4.0;
        printf(", %.0f bytes/frame average (%.0fx smaller than raw)",
               (double)totalBytes / (double)frames,
               raw / ((double)totalBytes / (double)frames));
    }
    printf("\n");
    if (recFile) {
        fclose(recFile);
        printf("recording written to %s\n", opt.record);
    }
#ifdef USBPROTO_WITH_BROKER
    if (brokerOn) {
        printf("broker: %zu frames relayed out, %zu injected in, %zu clients dropped\n",
               broker.framesRelayed(), broker.framesInjected(),
               broker.clientsDropped());
    }
#endif
    if (touchesSent) {
        printf("%llu touch events sent\n", (unsigned long long)touchesSent);
    }
    if (session.resyncCount()) {
        printf("%zu stream resynchronisations\n", session.resyncCount());
    }

    // A capture ends on its own, so exit when it does — otherwise the viewer
    // could never be run from a script. --hold keeps the last frame up for
    // someone watching.
    if (opt.hold && frames) {
        while (gfx.processEvents()) { /* idle */ }
    }

    return 0;
}
