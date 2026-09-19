#include "usbproto_session.h"

namespace usbproto {

Session::Session (Transport *transport)
    : m_transport(transport),
      m_readBuf(USB_BUF_SIZE)
{}

Session::~Session () = default;

void Session::setReadBufferSize (size_t bytes) {
    if (bytes < HEADER_SIZE) bytes = HEADER_SIZE;
    m_readBuf.resize(bytes);
}

// =============================================================================
// Outbound
// =============================================================================

bool Session::openChannel (uint8_t channel) {
    if (!m_transport || channel >= MAX_CHANNELS) return false;
    uint8_t hdr[HEADER_SIZE];
    encodeHeader(hdr, channel, FLAG_OPEN, 0);
    return m_transport->send(hdr, sizeof(hdr));
}

bool Session::closeChannel (uint8_t channel) {
    if (!m_transport || channel >= MAX_CHANNELS) return false;
    uint8_t hdr[HEADER_SIZE];
    encodeHeader(hdr, channel, FLAG_CLOSE, 0);
    return m_transport->send(hdr, sizeof(hdr));
}

bool Session::sendData (uint8_t channel, const uint8_t *data, size_t len) {
    if (!m_transport || channel >= MAX_CHANNELS) return false;
    if (!data || len == 0) return false;

    std::vector<uint8_t> frame;
    size_t sent = 0;
    while (sent < len) {
        const size_t chunk = (len - sent > USB_MAX_PAYLOAD) ? USB_MAX_PAYLOAD
                                                            : (len - sent);
        frame.clear();
        frame.reserve(HEADER_SIZE + chunk);
        encodeFrame(frame, channel, FLAG_DATA, data + sent, chunk);
        if (!m_transport->send(frame.data(), frame.size())) return false;
        sent += chunk;
    }
    return true;
}

// =============================================================================
// Inbound
// =============================================================================

void Session::setDataHandler (uint8_t channel, RecordFn fn) {
    if (channel >= MAX_CHANNELS) return;
    m_channels[channel].onRecord = std::move(fn);
    m_channels[channel].assembler.reset();
}

void Session::setChannelReassembler (uint8_t channel,
                                     StreamAssembler::SizerFn sizer,
                                     const uint8_t *syncMagic, size_t syncLen,
                                     RecordFn onRecord,
                                     size_t maxRecord) {
    if (channel >= MAX_CHANNELS) return;
    m_channels[channel].onRecord  = std::move(onRecord);
    m_channels[channel].assembler.reset(
        new StreamAssembler(std::move(sizer), syncMagic, syncLen, maxRecord));
}

void Session::clearChannel (uint8_t channel) {
    if (channel >= MAX_CHANNELS) return;
    m_channels[channel].onRecord = nullptr;
    m_channels[channel].assembler.reset();
}

int Session::pump (int timeoutMs) {
    if (!m_transport || !m_transport->isOpen()) return -1;

    const int n = m_transport->recv(m_readBuf.data(), m_readBuf.size(), timeoutMs);
    if (n <= 0) return n;                    // 0 = timeout, -1 = error

    m_bytes += (size_t)n;

    m_parser.feed(m_readBuf.data(), (size_t)n,
                  [this](uint8_t ch, uint8_t flags, const uint8_t *payload, size_t len) {
        ++m_frames;

        if (m_onFrame) m_onFrame(ch, flags, payload, len);

        if (ch >= MAX_CHANNELS) return;

        switch (flags) {
        case FLAG_OPEN:
            if (m_onOpened) m_onOpened(ch);
            break;

        case FLAG_CLOSE:
            // The far end is gone; drop any half-built record so a later
            // reconnect does not start mid-stream.
            if (m_channels[ch].assembler) m_channels[ch].assembler->reset();
            if (m_onClosed) m_onClosed(ch);
            break;

        case FLAG_DATA: {
            ChannelState &cs = m_channels[ch];
            if (!cs.onRecord || len == 0) break;
            if (cs.assembler) {
                cs.assembler->feed(payload, len, cs.onRecord);
            } else {
                cs.onRecord(payload, len);
            }
            break;
        }

        default:
            break;   // ACK / ERROR carry no payload we act on here
        }
    });

    return n;
}

} // namespace usbproto
