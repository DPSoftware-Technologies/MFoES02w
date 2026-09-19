#ifndef USBPROTO_H
#define USBPROTO_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

// =============================================================================
// usbproto — the MFoES USB channel wire format, in one place
// =============================================================================
//
// The Pi multiplexes several logical channels over a single USB bulk pipe.
// usbd (services/usbd.cpp) owns the device end; this library is the definition
// of the format both ends speak, so the host tooling and the firmware cannot
// drift apart.
//
// ----- Layers -----------------------------------------------------------------
//
//   1. MUBD frame     12-byte header + payload, carried on the bulk pipe.
//                     FrameParser decodes a byte stream into frames and
//                     resynchronises on the magic after corruption.
//
//   2. Channel        One byte of the header. Each channel is an independent
//                     byte stream between a host program and one app on the Pi.
//
//   3. Records        Whatever the two ends agree on inside a channel.
//                     StreamAssembler cuts a channel's byte stream back into
//                     self-delimiting records.
//
// ----- Why records matter -----------------------------------------------------
//
// A local app writes length-prefixed messages to usbd over its unix socket, but
// usbd does NOT preserve those boundaries: app_reader_thread strips the length
// and pushes raw bytes into a ring buffer, and channel_tx_thread later drains
// whatever happens to be queued, chunked at USB_MAX_PAYLOAD. One app message can
// therefore arrive as several FLAG_DATA frames, and several small app messages
// can arrive coalesced inside one.
//
// So a channel is a byte stream, not a message stream. Anything sent over it
// must be self-delimiting — carry its own magic and length — and the receiver
// must reassemble. StreamAssembler does that, driven by a caller-supplied sizer
// so this library stays independent of any particular payload format.
//

namespace usbproto {

// ===== WIRE CONSTANTS ========================================================
// These mirror services/usbd.cpp and tools/usb_channel.py exactly.

/// "MUBD", stored little-endian on the wire (bytes 44 42 55 4D).
constexpr uint32_t FRAME_MAGIC = 0x4D554244u;

constexpr uint8_t FLAG_DATA  = 0x00;
constexpr uint8_t FLAG_OPEN  = 0x01;
constexpr uint8_t FLAG_CLOSE = 0x02;
constexpr uint8_t FLAG_ACK   = 0x03;
constexpr uint8_t FLAG_ERROR = 0x04;

constexpr size_t HEADER_SIZE     = 12;
constexpr size_t USB_BUF_SIZE    = 512u * 1024u;            ///< max header+payload
constexpr size_t USB_MAX_PAYLOAD = USB_BUF_SIZE - HEADER_SIZE;
constexpr size_t MAX_FRAME_SIZE  = 4u * 1024u * 1024u;      ///< sanity cap
constexpr uint8_t MAX_CHANNELS   = 32;
constexpr uint8_t CHAN_AUTO      = 0xFF;                    ///< "assign me one"

/// USB identity of the gadget (matches the descriptors in usbd.cpp).
constexpr uint16_t DEFAULT_VID = 0x750C;
constexpr uint16_t DEFAULT_PID = 0x0544;
// Fallback endpoint addresses, used only when discovery finds nothing.
//
// Do not trust the endpoint numbers in usbd.cpp's FunctionFS descriptors: those
// are a request, and the UDC assigns the real addresses. The gadget asks for
// IN ep2, but dwc2 hands out 0x81 — IN and OUT are separate address spaces, so
// both sit on endpoint 1. LibusbTransport::open() reads the actual addresses
// off the device; hardcoding these is how a bulk read ends up failing with no
// visible cause.
constexpr uint8_t  EP_BULK_OUT = 0x01;   ///< host -> device
constexpr uint8_t  EP_BULK_IN  = 0x81;   ///< device -> host

// ----- Well-known channel assignments ----------------------------------------
// Keep these distinct; usbd refuses a channel that is already active.
constexpr uint8_t CHAN_DTS        = 0;   ///< inbound video tiles (DTS_videotransfer.py)
constexpr uint8_t CHAN_OTA        = 10;  ///< otad firmware upload
constexpr uint8_t CHAN_DRAWREPLAY = 11;  ///< outbound recorded UI draw commands

// ===== FRAME HEADER ==========================================================

struct FrameHeader {
    uint32_t magic    = 0;
    uint8_t  channel  = 0;
    uint8_t  flags    = 0;
    uint16_t reserved = 0;
    uint32_t length   = 0;   ///< payload bytes following the header
};

/**
 * Write a header into @p out, which must have room for HEADER_SIZE bytes.
 * Fields are written as explicit little-endian, matching usbd's htole32 — never
 * as a struct copy, so the format does not depend on this compiler's packing.
 * @return HEADER_SIZE
 */
size_t encodeHeader (uint8_t *out, uint8_t channel, uint8_t flags, uint32_t length);

/// Decode a header from @p in. Returns false if @p len < HEADER_SIZE.
/// A successful decode does NOT imply a valid magic — check `out.magic`.
bool decodeHeader (const uint8_t *in, size_t len, FrameHeader &out);

/// Build a complete frame (header + payload) into @p out, appending.
void encodeFrame (std::vector<uint8_t> &out, uint8_t channel, uint8_t flags,
                  const uint8_t *payload = nullptr, size_t payloadLen = 0);

// ===== FrameParser ===========================================================
/**
 * Decodes a bulk-pipe byte stream into MUBD frames.
 *
 * feed() may be called with arbitrary chunk boundaries — a frame split across
 * several reads, or several frames in one read, are both handled. On a bad
 * magic or an impossible length the parser scans forward for the next magic
 * rather than giving up on the stream, and counts the event in resyncCount().
 */
class FrameParser {
public:
    /// payload points into internal storage and is only valid for the call.
    using FrameFn = std::function<void(uint8_t channel, uint8_t flags,
                                       const uint8_t *payload, size_t len)>;

    void feed (const uint8_t *data, size_t len, const FrameFn &onFrame);
    void reset ();

    size_t resyncCount () const { return m_resyncs; }
    size_t buffered    () const { return m_buf.size() - m_pos; }

private:
    /// Drop bytes up to the next plausible frame start. Returns false when the
    /// buffer holds no further magic and the parser should wait for more data.
    bool resync ();
    void compact ();

    std::vector<uint8_t> m_buf;
    size_t               m_pos     = 0;   ///< read offset into m_buf
    size_t               m_resyncs = 0;
};

// ===== StreamAssembler =======================================================
/**
 * Cuts a channel's byte stream back into self-delimiting records.
 *
 * The caller supplies a sizer that inspects the start of the buffer and reports
 * how long the record there is. Keeping the sizer external is what lets this
 * library stay ignorant of DrawReplay or any other payload format.
 */
class StreamAssembler {
public:
    /// Sizer return values.
    static constexpr size_t NEED_MORE  = 0;              ///< header incomplete
    static constexpr size_t BAD_RECORD = (size_t)-1;     ///< corrupt; resync

    /**
     * @param data,len  Start of the candidate record (len >= 1).
     * @return total record length in bytes, NEED_MORE, or BAD_RECORD.
     */
    using SizerFn  = std::function<size_t(const uint8_t *data, size_t len)>;
    using RecordFn = std::function<void(const uint8_t *data, size_t len)>;

    /**
     * @param sizer      Measures a record at the front of the buffer.
     * @param syncMagic  Byte pattern a record starts with, used to resynchronise.
     * @param syncLen    Length of syncMagic (1..8).
     * @param maxRecord  Records larger than this are treated as corruption.
     */
    StreamAssembler (SizerFn sizer, const uint8_t *syncMagic, size_t syncLen,
                     size_t maxRecord = MAX_FRAME_SIZE);

    void feed (const uint8_t *data, size_t len, const RecordFn &onRecord);
    void reset ();

    size_t resyncCount () const { return m_resyncs; }
    size_t buffered    () const { return m_buf.size() - m_pos; }

private:
    bool resync ();
    void compact ();

    SizerFn              m_sizer;
    uint8_t              m_magic[8];
    size_t               m_magicLen;
    size_t               m_maxRecord;
    std::vector<uint8_t> m_buf;
    size_t               m_pos     = 0;
    size_t               m_resyncs = 0;
};

// ===== Transport =============================================================
/**
 * A bidirectional byte pipe. Implemented by the libusb host transport and by
 * the on-device usbd socket transport, so channel code is written once.
 */
class Transport {
public:
    virtual ~Transport ();

    virtual bool isOpen () const = 0;

    /// Send all @p len bytes. Returns false on error or short write.
    virtual bool send (const uint8_t *data, size_t len) = 0;

    /**
     * Read whatever is available.
     * @return >0 bytes read, 0 on timeout, -1 on error or disconnect.
     */
    virtual int recv (uint8_t *buf, size_t bufSize, int timeoutMs) = 0;

    virtual void close () = 0;

    /**
     * Why the most recent call failed, or "" when there is nothing to report.
     * Owned by the transport and valid until the next call on it.
     *
     * send()/recv() return a bare bool/int, so without this the reason for a
     * failure has nowhere to go and the caller is left guessing.
     */
    virtual const char *lastError () const { return ""; }
};

} // namespace usbproto

#endif // USBPROTO_H
