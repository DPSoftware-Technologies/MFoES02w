#include "usbproto.h"

#include <cstring>

namespace usbproto {

// =============================================================================
// Header codec — explicit little-endian, never a struct copy
// =============================================================================

namespace {

inline void putU16LE (uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

inline void putU32LE (uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >>  8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

inline uint16_t getU16LE (const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

inline uint32_t getU32LE (const uint8_t *p) {
    return (uint32_t)p[0]
         | ((uint32_t)p[1] <<  8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

/// FRAME_MAGIC as it appears on the wire, for byte-wise resynchronisation.
const uint8_t kMagicBytes[4] = {
    (uint8_t)(FRAME_MAGIC & 0xFF),
    (uint8_t)((FRAME_MAGIC >>  8) & 0xFF),
    (uint8_t)((FRAME_MAGIC >> 16) & 0xFF),
    (uint8_t)((FRAME_MAGIC >> 24) & 0xFF),
};

/**
 * Find @p needle in @p hay starting at @p from.
 * Returns the offset, or `haylen` when absent.
 */
size_t findPattern (const uint8_t *hay, size_t haylen, size_t from,
                    const uint8_t *needle, size_t needlelen) {
    if (needlelen == 0 || haylen < needlelen) return haylen;
    for (size_t i = from; i + needlelen <= haylen; ++i) {
        if (hay[i] == needle[0] && memcmp(hay + i, needle, needlelen) == 0) return i;
    }
    return haylen;
}

/// Bytes worth keeping when no pattern was found: a split magic may straddle
/// the tail, so retain needlelen-1 bytes and drop the rest.
size_t keepTail (size_t needlelen) {
    return needlelen > 0 ? needlelen - 1 : 0;
}

} // namespace

size_t encodeHeader (uint8_t *out, uint8_t channel, uint8_t flags, uint32_t length) {
    putU32LE(out, FRAME_MAGIC);
    out[4] = channel;
    out[5] = flags;
    putU16LE(out + 6, 0);
    putU32LE(out + 8, length);
    return HEADER_SIZE;
}

bool decodeHeader (const uint8_t *in, size_t len, FrameHeader &out) {
    if (!in || len < HEADER_SIZE) return false;
    out.magic    = getU32LE(in);
    out.channel  = in[4];
    out.flags    = in[5];
    out.reserved = getU16LE(in + 6);
    out.length   = getU32LE(in + 8);
    return true;
}

void encodeFrame (std::vector<uint8_t> &out, uint8_t channel, uint8_t flags,
                  const uint8_t *payload, size_t payloadLen) {
    const size_t at = out.size();
    out.resize(at + HEADER_SIZE + payloadLen);
    encodeHeader(out.data() + at, channel, flags, (uint32_t)payloadLen);
    if (payload && payloadLen) {
        memcpy(out.data() + at + HEADER_SIZE, payload, payloadLen);
    }
}

// =============================================================================
// FrameParser
// =============================================================================

void FrameParser::reset () {
    m_buf.clear();
    m_pos = 0;
}

void FrameParser::compact () {
    if (m_pos == 0) return;
    // Only pay for the move once the dead prefix is worth reclaiming, so a
    // steady stream of small frames does not memmove on every single one.
    if (m_pos >= m_buf.size()) {
        m_buf.clear();
        m_pos = 0;
    } else if (m_pos >= USB_BUF_SIZE) {
        m_buf.erase(m_buf.begin(), m_buf.begin() + (std::ptrdiff_t)m_pos);
        m_pos = 0;
    }
}

bool FrameParser::resync () {
    ++m_resyncs;
    // Skip the byte we are sitting on, then look for the next magic.
    const size_t at = findPattern(m_buf.data(), m_buf.size(), m_pos + 1,
                                  kMagicBytes, sizeof(kMagicBytes));
    if (at >= m_buf.size()) {
        // Nothing usable left; keep only what could be a split magic.
        const size_t keep = keepTail(sizeof(kMagicBytes));
        if (m_buf.size() > keep) m_pos = m_buf.size() - keep;
        compact();
        return false;
    }
    m_pos = at;
    return true;
}

void FrameParser::feed (const uint8_t *data, size_t len, const FrameFn &onFrame) {
    if (data && len) m_buf.insert(m_buf.end(), data, data + len);

    for (;;) {
        const size_t avail = m_buf.size() - m_pos;
        if (avail < HEADER_SIZE) break;

        FrameHeader hdr;
        decodeHeader(m_buf.data() + m_pos, avail, hdr);

        if (hdr.magic != FRAME_MAGIC || hdr.length > MAX_FRAME_SIZE) {
            if (!resync()) break;
            continue;
        }

        const size_t total = HEADER_SIZE + (size_t)hdr.length;
        if (avail < total) break;            // frame still arriving

        if (onFrame) {
            onFrame(hdr.channel, hdr.flags,
                    m_buf.data() + m_pos + HEADER_SIZE, (size_t)hdr.length);
        }
        m_pos += total;
        compact();
    }

    compact();
}

// =============================================================================
// StreamAssembler
// =============================================================================

StreamAssembler::StreamAssembler (SizerFn sizer, const uint8_t *syncMagic,
                                  size_t syncLen, size_t maxRecord)
    : m_sizer(std::move(sizer)),
      m_magicLen(syncLen > sizeof(m_magic) ? sizeof(m_magic) : syncLen),
      m_maxRecord(maxRecord)
{
    memset(m_magic, 0, sizeof(m_magic));
    if (syncMagic && m_magicLen) memcpy(m_magic, syncMagic, m_magicLen);
}

void StreamAssembler::reset () {
    m_buf.clear();
    m_pos = 0;
}

void StreamAssembler::compact () {
    if (m_pos == 0) return;
    if (m_pos >= m_buf.size()) {
        m_buf.clear();
        m_pos = 0;
    } else if (m_pos >= USB_BUF_SIZE) {
        m_buf.erase(m_buf.begin(), m_buf.begin() + (std::ptrdiff_t)m_pos);
        m_pos = 0;
    }
}

bool StreamAssembler::resync () {
    ++m_resyncs;
    const size_t at = findPattern(m_buf.data(), m_buf.size(), m_pos + 1,
                                  m_magic, m_magicLen);
    if (at >= m_buf.size()) {
        const size_t keep = keepTail(m_magicLen);
        if (m_buf.size() > keep) m_pos = m_buf.size() - keep;
        compact();
        return false;
    }
    m_pos = at;
    return true;
}

void StreamAssembler::feed (const uint8_t *data, size_t len, const RecordFn &onRecord) {
    if (data && len) m_buf.insert(m_buf.end(), data, data + len);

    for (;;) {
        const size_t avail = m_buf.size() - m_pos;
        if (avail == 0) break;

        // Cheap pre-check: if the magic is known and already contradicted,
        // resync without troubling the sizer.
        if (m_magicLen && avail >= m_magicLen &&
            memcmp(m_buf.data() + m_pos, m_magic, m_magicLen) != 0) {
            if (!resync()) break;
            continue;
        }

        const size_t need = m_sizer ? m_sizer(m_buf.data() + m_pos, avail)
                                    : BAD_RECORD;

        if (need == NEED_MORE) break;                 // header not complete yet
        if (need == BAD_RECORD || need > m_maxRecord) {
            if (!resync()) break;
            continue;
        }
        if (avail < need) break;                      // record still arriving

        if (onRecord) onRecord(m_buf.data() + m_pos, need);
        m_pos += need;
        compact();
    }

    compact();
}

// =============================================================================
// Transport
// =============================================================================

Transport::~Transport () = default;

} // namespace usbproto
