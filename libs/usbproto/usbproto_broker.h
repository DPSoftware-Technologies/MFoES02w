#ifndef USBPROTO_BROKER_H
#define USBPROTO_BROKER_H

#include "usbproto.h"

#include <memory>
#include <string>

// =============================================================================
// Broker — share one USB link between several local programs
// =============================================================================
//
// libusb claims the interface exclusively, so while the viewer holds the gadget
// nothing else can open it: usb_channel.py and every tool built on it are shut
// out. That is the problem this solves. One process owns the USB link and
// relays MUBD frames to local clients over TCP, so the draw stream, DTS video
// and OTA can all run at once.
//
// Clients speak the SAME framing as the USB pipe — the bytes on the socket are
// exactly the bytes on the wire. A client therefore needs no new protocol: it
// swaps its bulk read/write for a socket and everything above the transport
// keeps working unchanged.
//
//   Broker b;
//   b.start(7311);
//   ...
//   session.setFrameHandler([&](uint8_t ch, uint8_t fl, const uint8_t* p, size_t n) {
//       std::vector<uint8_t> f;
//       encodeFrame(f, ch, fl, p, n);
//       b.broadcast(f.data(), f.size());        // device -> clients
//   });
//   b.poll(0, [&](const uint8_t* f, size_t n) {
//       transport.send(f, n);                    // clients -> device
//   });
//
// ----- Why whole frames --------------------------------------------------------
//
// Client bytes are reassembled into complete frames before being handed on.
// Forwarding raw bytes would let two clients' partial writes interleave on the
// USB pipe and corrupt the stream; a frame is the smallest safely forwardable
// unit.
//
// ----- Backpressure ------------------------------------------------------------
//
// A client that stops reading must not stall the viewer, so sockets are
// non-blocking and each client has a bounded outbound queue. A client that
// exceeds it is dropped rather than allowed to block the render loop.
//
// Binds to loopback only — this is local IPC, not a network service.
//

namespace usbproto {

constexpr uint16_t DEFAULT_BROKER_PORT = 7311;

class Broker {
public:
    /// A complete MUBD frame received from a client, to be sent to the device.
    using ClientFrameFn = std::function<void(const uint8_t *frame, size_t len)>;

    Broker ();
    ~Broker ();

    /**
     * Listen on 127.0.0.1:@p port.
     * @return false on failure; lastError() says why.
     */
    bool start (uint16_t port = DEFAULT_BROKER_PORT);
    void stop ();
    bool isRunning () const;

    /**
     * Accept new clients, read what they sent, and flush pending output.
     * Call it from the same loop that pumps the Session.
     *
     * @param timeoutMs     How long select() may wait. 0 to poll.
     * @param onClientFrame Invoked once per complete frame from any client.
     */
    void poll (int timeoutMs, const ClientFrameFn &onClientFrame);

    /// Queue a complete frame for every connected client.
    void broadcast (const uint8_t *frame, size_t len);

    size_t clientCount   () const;
    size_t framesRelayed () const;   ///< device -> clients
    size_t framesInjected() const;   ///< clients -> device
    size_t clientsDropped() const;   ///< dropped for falling too far behind

    /// Bytes a single client may have queued before it is dropped.
    void   setClientQueueLimit (size_t bytes);

    const char *lastError () const;

private:
    struct Impl;
    std::unique_ptr<Impl> m;

    Broker (const Broker &)            = delete;
    Broker &operator= (const Broker &) = delete;
};

} // namespace usbproto

#endif // USBPROTO_BROKER_H
