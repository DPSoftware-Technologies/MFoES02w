#ifndef USBPROTO_SESSION_H
#define USBPROTO_SESSION_H

#include "usbproto.h"

#include <memory>

// =============================================================================
// Session — channels on top of a Transport
// =============================================================================
//
// Owns the receive-side state machine so host tools do not each rewrite it:
// read bytes from the transport, cut them into MUBD frames, route FLAG_DATA by
// channel, and optionally reassemble a channel's byte stream back into records.
//
//   usbproto::LibusbTransport t;
//   t.open();
//   usbproto::Session s(&t);
//   s.openChannel(usbproto::CHAN_DRAWREPLAY);
//   s.setChannelReassembler(usbproto::CHAN_DRAWREPLAY, drSizer, magic, 4,
//                           [](const uint8_t* rec, size_t n) { render(rec, n); });
//   while (running) s.pump(100);
//
// Not thread-safe: drive one Session from one thread, or serialise externally.
//

namespace usbproto {

class Session {
public:
    using FrameFn   = std::function<void(uint8_t channel, uint8_t flags,
                                         const uint8_t *payload, size_t len)>;
    using RecordFn  = std::function<void(const uint8_t *data, size_t len)>;
    using ChannelFn = std::function<void(uint8_t channel)>;

    /// @param transport Borrowed, not owned; must outlive the Session.
    explicit Session (Transport *transport);
    ~Session ();

    // ----- outbound ----------------------------------------------------------

    /// Ask the device to open @p channel. usbd replies with FLAG_ACK.
    bool openChannel  (uint8_t channel);
    bool closeChannel (uint8_t channel);

    /**
     * Send @p len bytes on @p channel, split into as many FLAG_DATA frames as
     * needed. Note that usbd does not preserve these boundaries on the way to
     * the app, so the payload should be self-delimiting.
     */
    bool sendData (uint8_t channel, const uint8_t *data, size_t len);

    // ----- inbound -----------------------------------------------------------

    /**
     * Read once from the transport and dispatch whatever frames complete.
     * @return bytes read (>0), 0 on timeout, -1 on transport error.
     */
    int pump (int timeoutMs);

    /// Raw frame hook — called for every frame, before channel routing.
    void setFrameHandler (FrameFn fn) { m_onFrame = std::move(fn); }

    /// Called for FLAG_DATA on a channel with no reassembler registered.
    void setDataHandler (uint8_t channel, RecordFn fn);

    /**
     * Route FLAG_DATA on @p channel through a StreamAssembler, so the handler
     * sees whole records instead of arbitrary byte runs.
     */
    void setChannelReassembler (uint8_t channel,
                                StreamAssembler::SizerFn sizer,
                                const uint8_t *syncMagic, size_t syncLen,
                                RecordFn onRecord,
                                size_t maxRecord = MAX_FRAME_SIZE);

    void clearChannel (uint8_t channel);

    void setChannelOpenedHandler (ChannelFn fn) { m_onOpened = std::move(fn); }
    void setChannelClosedHandler (ChannelFn fn) { m_onClosed = std::move(fn); }

    // ----- stats -------------------------------------------------------------

    size_t framesReceived () const { return m_frames; }
    size_t bytesReceived  () const { return m_bytes; }
    size_t resyncCount    () const { return m_parser.resyncCount(); }

    /// Size of the buffer pump() reads into. Default USB_BUF_SIZE.
    void setReadBufferSize (size_t bytes);

private:
    struct ChannelState {
        RecordFn                         onRecord;
        std::unique_ptr<StreamAssembler> assembler;
    };

    Transport            *m_transport;
    FrameParser           m_parser;
    std::vector<uint8_t>  m_readBuf;
    ChannelState          m_channels[MAX_CHANNELS];

    FrameFn   m_onFrame;
    ChannelFn m_onOpened;
    ChannelFn m_onClosed;

    size_t m_frames = 0;
    size_t m_bytes  = 0;

    Session (const Session &)            = delete;
    Session &operator= (const Session &) = delete;
};

} // namespace usbproto

#endif // USBPROTO_SESSION_H
