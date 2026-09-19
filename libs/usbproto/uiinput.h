#ifndef USBPROTO_UIINPUT_H
#define USBPROTO_UIINPUT_H

#include <cstddef>
#include <cstdint>
#include <cstring>

// =============================================================================
// uiinput — touch events travelling host -> device
// =============================================================================
//
// The return path for the DrawReplay link. The board sends its UI as draw
// commands; this carries the taps back, so a viewer on the far end can drive
// the real interface.
//
// Records are injected into the board's touch queue exactly where the GT911
// driver's callbacks put theirs, so a remote tap is indistinguishable from a
// finger on the glass — it goes through the same widget dispatch and honours
// the same hide_ui gate.
//
// ----- Why a magic and a fixed size ------------------------------------------
//
// usbd happens to preserve message boundaries in this direction (its
// channel_rx_thread writes one length-prefixed socket message per frame), but
// the opposite direction does not, and relying on that asymmetry would be
// fragile. A record carries its own magic and has a fixed length, so a reader
// can resynchronise and so several records may share one datagram.
//
// The magic is deliberately NOT 'DRP1': a draw blob and an input record must
// never be mistaken for one another if they ever share a channel.
//
// ----- Layout (22 bytes, little-endian) --------------------------------------
//
//    0  uint8[4]  magic 'D','R','I','1'
//    4  uint8     event      (Event below)
//    5  uint8     id         finger id; 0 for a single pointer
//    6  uint16    x
//    8  uint16    y
//   10  uint16    size       contact size; 0 when unknown
//   12  uint8     active     non-zero while the pointer is down
//   13  uint8     reserved
//   14  uint32    duration_ms
//   18  int16     dx
//   20  int16     dy
//

namespace uiinput {

constexpr size_t RECORD_SIZE = 22;
constexpr size_t MAGIC_LEN   = 4;

/// Mirrors TouchEvent in libs/hwinterface/gt911.h. Values are wire ABI.
enum Event : uint8_t {
    EV_PRESS   = 0,
    EV_RELEASE = 1,
    EV_MOVE    = 2,
    EV_HOLD    = 3,
    EV__MAX    = 3,
};

inline const uint8_t *magic () {
    static const uint8_t m[MAGIC_LEN] = { 'D', 'R', 'I', '1' };
    return m;
}

struct TouchRecord {
    uint8_t  event       = EV_PRESS;
    uint8_t  id          = 0;
    uint16_t x           = 0;
    uint16_t y           = 0;
    uint16_t size        = 0;
    uint8_t  active      = 0;
    uint32_t duration_ms = 0;
    int16_t  dx          = 0;
    int16_t  dy          = 0;
};

// ----- little-endian helpers -------------------------------------------------

inline void putU16 (uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

inline void putU32 (uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >>  8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

inline uint16_t getU16 (const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

inline uint32_t getU32 (const uint8_t *p) {
    return (uint32_t)p[0]
         | ((uint32_t)p[1] <<  8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

// ----- codec -----------------------------------------------------------------

/// Write @p r into @p out, which must have room for RECORD_SIZE bytes.
inline size_t encode (uint8_t *out, const TouchRecord &r) {
    memcpy(out, magic(), MAGIC_LEN);
    out[4]  = r.event;
    out[5]  = r.id;
    putU16(out + 6,  r.x);
    putU16(out + 8,  r.y);
    putU16(out + 10, r.size);
    out[12] = r.active;
    out[13] = 0;
    putU32(out + 14, r.duration_ms);
    putU16(out + 18, (uint16_t)r.dx);
    putU16(out + 20, (uint16_t)r.dy);
    return RECORD_SIZE;
}

/// Decode one record. Returns false on a short buffer, a bad magic, or an
/// event code this build does not know.
inline bool decode (const uint8_t *in, size_t len, TouchRecord &r) {
    if (!in || len < RECORD_SIZE) return false;
    if (memcmp(in, magic(), MAGIC_LEN) != 0) return false;
    if (in[4] > EV__MAX) return false;

    r.event       = in[4];
    r.id          = in[5];
    r.x           = getU16(in + 6);
    r.y           = getU16(in + 8);
    r.size        = getU16(in + 10);
    r.active      = in[12];
    r.duration_ms = getU32(in + 14);
    r.dx          = (int16_t)getU16(in + 18);
    r.dy          = (int16_t)getU16(in + 20);
    return true;
}

// ===== CONTROL RECORDS =======================================================
//
// Host -> device, alongside touch on the same channel.
//
// A viewer attaching is invisible to the board: usbd answers the channel's
// FLAG_OPEN itself and never tells the app, and the app's unix socket to usbd
// does not drop when the far end goes away. So a reattaching viewer has to
// announce itself, otherwise the board keeps skipping unchanged frames and the
// new viewer stares at a blank window until something on screen happens to
// move.
//
// Layout (8 bytes):
//   0  uint8[4]  magic 'D','R','C','1'
//   4  uint8     command
//   5  uint8[3]  reserved
//

constexpr size_t CTRL_SIZE = 8;

enum Ctrl : uint8_t {
    /// "I just attached and my window is blank — send a frame even if the
    /// screen has not changed."
    CTRL_REQUEST_FULL_FRAME = 0,
    CTRL__MAX               = 0,
};

inline const uint8_t *ctrlMagic () {
    static const uint8_t m[MAGIC_LEN] = { 'D', 'R', 'C', '1' };
    return m;
}

struct CtrlRecord {
    uint8_t cmd = CTRL_REQUEST_FULL_FRAME;
};

inline size_t encodeCtrl (uint8_t *out, const CtrlRecord &c) {
    memcpy(out, ctrlMagic(), MAGIC_LEN);
    out[4] = c.cmd;
    out[5] = 0;
    out[6] = 0;
    out[7] = 0;
    return CTRL_SIZE;
}

inline bool decodeCtrl (const uint8_t *in, size_t len, CtrlRecord &c) {
    if (!in || len < CTRL_SIZE) return false;
    if (memcmp(in, ctrlMagic(), MAGIC_LEN) != 0) return false;
    if (in[4] > CTRL__MAX) return false;
    c.cmd = in[4];
    return true;
}

/// Cheap type probes, so a reader can dispatch on a mixed record stream.
inline bool isCtrl (const uint8_t *d, size_t len) {
    return d && len >= MAGIC_LEN && memcmp(d, ctrlMagic(), MAGIC_LEN) == 0;
}

inline bool isTouch (const uint8_t *d, size_t len) {
    return d && len >= MAGIC_LEN && memcmp(d, magic(), MAGIC_LEN) == 0;
}

/**
 * Length of the record at @p data, for usbproto::StreamAssembler.
 * @return RECORD_SIZE, 0 when more bytes are needed, or SIZE_MAX to resync.
 */
inline size_t recordSize (const uint8_t *data, size_t len) {
    if (!data) return (size_t)-1;
    const size_t have = len < MAGIC_LEN ? len : MAGIC_LEN;
    for (size_t i = 0; i < have; ++i) {
        if (data[i] != magic()[i]) return (size_t)-1;
    }
    if (len < RECORD_SIZE) return 0;
    return RECORD_SIZE;
}

} // namespace uiinput

#endif // USBPROTO_UIINPUT_H
