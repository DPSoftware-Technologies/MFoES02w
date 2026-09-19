#include "DrawReplay.h"

#include <cstdio>
#include <cstdarg>
#include <cstring>

// =============================================================================
// Local byte-stream helpers
// =============================================================================

namespace {

/// Bounds-checked little-endian reader. Every accessor is safe to call after a
/// previous one has failed: `ok` latches false and subsequent reads return 0,
/// so the decode loop only has to test `ok` once per command.
struct DRReader {
    const uint8_t *p;
    size_t         n;
    size_t         i;
    bool           ok;

    DRReader (const uint8_t *data, size_t len) : p(data), n(len), i(0), ok(true) {}

    size_t remaining () const { return ok ? (n - i) : 0u; }

    bool need (size_t k) {
        if (!ok || k > n - i) { ok = false; return false; }
        return true;
    }

    uint8_t u8 () {
        if (!need(1)) return 0;
        return p[i++];
    }

    uint16_t u16 () {
        if (!need(2)) return 0;
        uint16_t v = (uint16_t)((uint16_t)p[i] | ((uint16_t)p[i + 1] << 8));
        i += 2;
        return v;
    }

    int16_t i16 () { return (int16_t)u16(); }

    uint32_t u32 () {
        if (!need(4)) return 0;
        uint32_t v = (uint32_t)p[i]
                   | ((uint32_t)p[i + 1] << 8)
                   | ((uint32_t)p[i + 2] << 16)
                   | ((uint32_t)p[i + 3] << 24);
        i += 4;
        return v;
    }

    float f32 () {
        uint32_t bits = u32();
        float    f    = 0.0f;
        memcpy(&f, &bits, sizeof(f));
        return f;
    }

    const uint8_t *bytes (size_t k) {
        if (!need(k)) return nullptr;
        const uint8_t *q = p + i;
        i += k;
        return q;
    }
};

inline void pushU16 (std::vector<uint8_t> &v, uint16_t x) {
    v.push_back((uint8_t)(x & 0xFF));
    v.push_back((uint8_t)((x >> 8) & 0xFF));
}

inline void pushU32 (std::vector<uint8_t> &v, uint32_t x) {
    v.push_back((uint8_t)(x & 0xFF));
    v.push_back((uint8_t)((x >>  8) & 0xFF));
    v.push_back((uint8_t)((x >> 16) & 0xFF));
    v.push_back((uint8_t)((x >> 24) & 0xFF));
}

/// Bytes in a 1-bit-per-pixel plane of w x h.
inline uint64_t monoPlaneBytes (int16_t w, int16_t h) {
    return (((uint64_t)(uint16_t)w + 7u) / 8u) * (uint64_t)(uint16_t)h;
}

/**
 * Pixel-count guard for a decoded bitmap command.
 *
 * w and h are int16 so w*h*4 can reach ~4.3 GB — enough to wrap size_t on a
 * 32-bit userland. The count is computed in 64-bit and rejected against the
 * bytes actually left in the stream, so a malformed header can never provoke a
 * huge allocation.
 */
inline bool extentOk (int16_t w, int16_t h, uint64_t bytesNeeded, const DRReader &rd) {
    if (w <= 0 || h <= 0) return false;
    return bytesNeeded <= (uint64_t)rd.remaining();
}

} // namespace

// =============================================================================
// DRErrorString
// =============================================================================

const char *DRErrorString (DRError e) {
    switch (e) {
        case DRError::Ok:            return "ok";
        case DRError::BadMagic:      return "bad magic";
        case DRError::BadVersion:    return "unsupported version";
        case DRError::Truncated:     return "truncated stream";
        case DRError::UnknownOpcode: return "unknown opcode";
        case DRError::BadPayload:    return "bad payload";
    }
    return "unknown error";
}

// =============================================================================
// DRFontTable
// =============================================================================

void DRFontTable::bind (uint8_t id, const GFXfont *font) {
    if (id == DR_FONT_BUILTIN) return;          // reserved for the 5x7 font
    if (m_fonts.size() <= (size_t)id) m_fonts.resize((size_t)id + 1u, nullptr);
    m_fonts[(size_t)id] = font;
}

const GFXfont *DRFontTable::get (uint8_t id) const {
    if (id == DR_FONT_BUILTIN || (size_t)id >= m_fonts.size()) return nullptr;
    return m_fonts[(size_t)id];
}

void DRFontTable::clear () {
    m_fonts.clear();
}

// =============================================================================
// Construction
// =============================================================================

DrawReplay::DrawReplay (int16_t w, int16_t h)
    : LinuxGFX(CanvasTag{}),
      m_suppress(false),
      m_stream(),
      m_cmdCount(0),
      m_recordMask(DR_REC_ALL),
      m_skipped(0),
      m_maxInlineBytes(0),
      m_fontIds()
{
    m_width  = w > 0 ? w : 0;
    m_height = h > 0 ? h : 0;
    m_pitch  = (uint32_t)m_width * 4u;   // keeps inherited stride maths sane
}

DrawReplay::~DrawReplay () {
}

// =============================================================================
// Stream writers
// =============================================================================

void DrawReplay::beginCmd (DROp op) {
    m_stream.push_back((uint8_t)op);
    ++m_cmdCount;
}

void DrawReplay::putU8 (uint8_t v) {
    m_stream.push_back(v);
}

void DrawReplay::putU16 (uint16_t v) {
    pushU16(m_stream, v);
}

void DrawReplay::putI16 (int16_t v) {
    pushU16(m_stream, (uint16_t)v);
}

void DrawReplay::putU32 (uint32_t v) {
    pushU32(m_stream, v);
}

void DrawReplay::putF32 (float v) {
    uint32_t bits = 0;
    memcpy(&bits, &v, sizeof(bits));
    pushU32(m_stream, bits);
}

void DrawReplay::putBytes (const void *p, size_t n) {
    if (!p || n == 0) return;
    const uint8_t *b = (const uint8_t *)p;
    m_stream.insert(m_stream.end(), b, b + n);
}

void DrawReplay::putText (const char *text) {
    size_t n = text ? strlen(text) : 0u;
    if (n > 0xFFFFu) n = 0xFFFFu;               // one command carries 64 KB max
    putU16((uint16_t)n);
    putBytes(text, n);
}

void DrawReplay::putMonoPlane (const uint8_t *bits, int16_t w, int16_t h) {
    putBytes(bits, (size_t)monoPlaneBytes(w, h));
}

bool DrawReplay::allow (uint32_t feature, size_t inlineBytes) {
    if ((m_recordMask & feature) == 0u) { ++m_skipped; return false; }
    if (m_maxInlineBytes != 0 && inlineBytes > m_maxInlineBytes) { ++m_skipped; return false; }
    return true;
}

// =============================================================================
// Replay API
// =============================================================================

size_t DrawReplay::DRExport (std::vector<uint8_t> &out) const {
    const size_t before = out.size();
    out.reserve(before + exportSize());

    out.push_back((uint8_t)DR_MAGIC_0);
    out.push_back((uint8_t)DR_MAGIC_1);
    out.push_back((uint8_t)DR_MAGIC_2);
    out.push_back((uint8_t)DR_MAGIC_3);
    pushU16(out, (uint16_t)DR_VERSION);
    pushU16(out, 0u);                                   // flags, reserved
    pushU16(out, (uint16_t)m_width);
    pushU16(out, (uint16_t)m_height);
    pushU32(out, (uint32_t)m_cmdCount);
    pushU32(out, (uint32_t)(m_stream.size() + 1u));     // body includes End

    out.insert(out.end(), m_stream.begin(), m_stream.end());
    out.push_back((uint8_t)DROp::End);

    return out.size() - before;
}

std::vector<uint8_t> DrawReplay::DRExport () const {
    std::vector<uint8_t> blob;
    DRExport(blob);
    return blob;
}

void DrawReplay::DRFlush () {
    m_stream.clear();
    m_cmdCount = 0;
    m_skipped  = 0;
}

void DrawReplay::DRReset () {
    DRFlush();
    m_fontIds.clear();

    m_cursorX       = 0;
    m_cursorY       = 0;
    m_textColor     = GFX_WHITE;
    m_textBgColor   = GFX_TRANSPARENT;
    m_textSizeX     = 1;
    m_textSizeY     = 1;
    m_textRotation  = 0;
    m_textWrap      = true;
    m_rotation      = 0;
    m_inverted      = false;
    m_pFont         = nullptr;
    m_drawStyle     = GFXDrawStyle();
}

DRError DrawReplay::DRPeek (const uint8_t *data, size_t len,
                            int16_t &outWidth, int16_t &outHeight,
                            uint32_t &outCmdCount, uint32_t &outBodyBytes) {
    if (!data || len < DR_HEADER_SIZE) return DRError::Truncated;
    if (data[0] != (uint8_t)DR_MAGIC_0 || data[1] != (uint8_t)DR_MAGIC_1 ||
        data[2] != (uint8_t)DR_MAGIC_2 || data[3] != (uint8_t)DR_MAGIC_3) {
        return DRError::BadMagic;
    }

    DRReader hdr(data + 4, DR_HEADER_SIZE - 4u);
    const uint16_t version = hdr.u16();
    hdr.u16();                                          // flags, ignored
    if (version > DR_VERSION) return DRError::BadVersion;

    outWidth     = hdr.i16();
    outHeight    = hdr.i16();
    outCmdCount  = hdr.u32();
    outBodyBytes = hdr.u32();

    if (!hdr.ok) return DRError::Truncated;
    if ((uint64_t)outBodyBytes > (uint64_t)(len - DR_HEADER_SIZE)) return DRError::Truncated;
    return DRError::Ok;
}

const uint8_t *DrawReplay::DRSyncMagic () {
    static const uint8_t magic[DR_SYNC_LEN] = {
        (uint8_t)DR_MAGIC_0, (uint8_t)DR_MAGIC_1,
        (uint8_t)DR_MAGIC_2, (uint8_t)DR_MAGIC_3,
    };
    return magic;
}

size_t DrawReplay::DRRecordSize (const uint8_t *data, size_t len) {
    if (!data) return (size_t)-1;

    // Reject on the magic as early as the bytes allow, so a resynchronising
    // reader does not have to wait for a whole header to learn it is lost.
    const uint8_t *magic = DRSyncMagic();
    const size_t   have  = len < DR_SYNC_LEN ? len : DR_SYNC_LEN;
    for (size_t i = 0; i < have; ++i) {
        if (data[i] != magic[i]) return (size_t)-1;
    }

    if (len < DR_HEADER_SIZE) return 0;          // header still arriving

    int16_t  w = 0, h = 0;
    uint32_t cmds = 0, body = 0;
    if (DRPeek(data, len, w, h, cmds, body) == DRError::BadVersion) {
        return (size_t)-1;
    }

    // DRPeek also range-checks body against len, which is wrong here: the rest
    // of the blob may simply not have arrived yet. Read the length directly.
    const uint32_t bodyBytes = (uint32_t)data[16]
                             | ((uint32_t)data[17] << 8)
                             | ((uint32_t)data[18] << 16)
                             | ((uint32_t)data[19] << 24);

    return (size_t)DR_HEADER_SIZE + (size_t)bodyBytes;
}

DRError DrawReplay::DRRender (const std::vector<uint8_t> &blob, LinuxGFX &target,
                              const DRFontTable *fonts) {
    return DRRender(blob.data(), blob.size(), target, fonts);
}

DRError DrawReplay::DRRender (const uint8_t *data, size_t len, LinuxGFX &target,
                              const DRFontTable *fonts) {
    int16_t  hdrW = 0, hdrH = 0;
    uint32_t cmdCount = 0, bodyBytes = 0;

    const DRError headerErr = DRPeek(data, len, hdrW, hdrH, cmdCount, bodyBytes);
    if (headerErr != DRError::Ok) return headerErr;

    // Trust the declared body length over the buffer length: a blob may arrive
    // inside a larger packet with trailing padding.
    DRReader rd(data + DR_HEADER_SIZE, (size_t)bodyBytes);

    // Scratch buffers reused across commands so a long stream does not
    // reallocate per bitmap.
    std::vector<uint32_t> argb;
    std::vector<uint16_t> rgb565;
    std::vector<char>     str;

    for (;;) {
        const uint8_t opByte = rd.u8();
        if (!rd.ok) return DRError::Truncated;

        switch ((DROp)opByte) {

        case DROp::End:
            return DRError::Ok;

        // ----- geometry ------------------------------------------------------

        case DROp::DrawPixel: {
            const int16_t  x = rd.i16(), y = rd.i16();
            const uint32_t c = rd.u32();
            if (rd.ok) target.drawPixel(x, y, c);
            break;
        }
        case DROp::FastVLine: {
            const int16_t  x = rd.i16(), y = rd.i16(), h = rd.i16();
            const uint32_t c = rd.u32();
            if (rd.ok) target.drawFastVLine(x, y, h, c);
            break;
        }
        case DROp::FastHLine: {
            const int16_t  x = rd.i16(), y = rd.i16(), w = rd.i16();
            const uint32_t c = rd.u32();
            if (rd.ok) target.drawFastHLine(x, y, w, c);
            break;
        }
        case DROp::Line: {
            const int16_t  x0 = rd.i16(), y0 = rd.i16(), x1 = rd.i16(), y1 = rd.i16();
            const uint32_t c  = rd.u32();
            if (rd.ok) target.drawLine(x0, y0, x1, y1, c);
            break;
        }
        case DROp::Rect: {
            const int16_t  x = rd.i16(), y = rd.i16(), w = rd.i16(), h = rd.i16();
            const uint32_t c = rd.u32();
            if (rd.ok) target.drawRect(x, y, w, h, c);
            break;
        }
        case DROp::FillRect: {
            const int16_t  x = rd.i16(), y = rd.i16(), w = rd.i16(), h = rd.i16();
            const uint32_t c = rd.u32();
            if (rd.ok) target.fillRect(x, y, w, h, c);
            break;
        }
        case DROp::FillScreen: {
            const uint32_t c = rd.u32();
            if (rd.ok) target.fillScreen(c);
            break;
        }
        case DROp::Circle: {
            const int16_t  x = rd.i16(), y = rd.i16(), r = rd.i16();
            const uint32_t c = rd.u32();
            if (rd.ok) target.drawCircle(x, y, r, c);
            break;
        }
        case DROp::FillCircle: {
            const int16_t  x = rd.i16(), y = rd.i16(), r = rd.i16();
            const uint32_t c = rd.u32();
            if (rd.ok) target.fillCircle(x, y, r, c);
            break;
        }
        case DROp::Arc: {
            const int16_t  x  = rd.i16(), y = rd.i16(), r = rd.i16();
            const float    a0 = rd.f32(), a1 = rd.f32();
            const uint32_t c  = rd.u32();
            if (rd.ok) target.drawArc(x, y, r, a0, a1, c);
            break;
        }
        case DROp::RoundRect: {
            const int16_t  x = rd.i16(), y = rd.i16(), w = rd.i16(),
                           h = rd.i16(), rad = rd.i16();
            const uint32_t c = rd.u32();
            if (rd.ok) target.drawRoundRect(x, y, w, h, rad, c);
            break;
        }
        case DROp::FillRoundRect: {
            const int16_t  x = rd.i16(), y = rd.i16(), w = rd.i16(),
                           h = rd.i16(), rad = rd.i16();
            const uint32_t c = rd.u32();
            if (rd.ok) target.fillRoundRect(x, y, w, h, rad, c);
            break;
        }
        case DROp::Triangle: {
            const int16_t  x0 = rd.i16(), y0 = rd.i16(), x1 = rd.i16(),
                           y1 = rd.i16(), x2 = rd.i16(), y2 = rd.i16();
            const uint32_t c  = rd.u32();
            if (rd.ok) target.drawTriangle(x0, y0, x1, y1, x2, y2, c);
            break;
        }
        case DROp::FillTriangle: {
            const int16_t  x0 = rd.i16(), y0 = rd.i16(), x1 = rd.i16(),
                           y1 = rd.i16(), x2 = rd.i16(), y2 = rd.i16();
            const uint32_t c  = rd.u32();
            if (rd.ok) target.fillTriangle(x0, y0, x1, y1, x2, y2, c);
            break;
        }

        // ----- state ---------------------------------------------------------

        case DROp::SetDrawStyle: {
            GFXDrawStyle st;
            st.strokeWidth = rd.u8();
            st.lineCap     = (GFXLineCap)rd.u8();
            st.lineJoin    = (GFXLineJoin)rd.u8();
            st.antiAlias   = rd.u8() != 0;
            if (rd.ok) target.setDrawStyle(st);
            break;
        }
        case DROp::SetRotation: {
            const uint8_t r = rd.u8();
            if (rd.ok) target.setRotation(r);
            break;
        }
        case DROp::InvertDisplay: {
            const uint8_t inv = rd.u8();
            if (rd.ok) target.invertDisplay(inv != 0);
            break;
        }

        // ----- text ----------------------------------------------------------

        case DROp::SetCursor: {
            const int16_t x = rd.i16(), y = rd.i16();
            if (rd.ok) target.setCursor(x, y);
            break;
        }
        case DROp::SetTextColor: {
            const uint32_t fg = rd.u32(), bg = rd.u32();
            if (rd.ok) target.setTextColor(fg, bg);
            break;
        }
        case DROp::SetTextSize: {
            const uint8_t sx = rd.u8(), sy = rd.u8();
            if (rd.ok) target.setTextSize(sx, sy);
            break;
        }
        case DROp::SetTextWrap: {
            const uint8_t w = rd.u8();
            if (rd.ok) target.setTextWrap(w != 0);
            break;
        }
        case DROp::SetTextRotation: {
            const uint8_t r = rd.u8();
            if (rd.ok) target.setTextRotation(r);
            break;
        }
        case DROp::SetFont: {
            const uint8_t id = rd.u8();
            if (rd.ok) target.setFont(fonts ? fonts->get(id) : nullptr);
            break;
        }
        case DROp::WriteText: {
            const uint16_t n = rd.u16();
            const uint8_t *s = rd.bytes(n);
            if (!rd.ok) break;
            str.assign(s, s + n);
            str.push_back('\0');
            target.writeText(str.data());
            break;
        }
        case DROp::DrawChar: {
            const int16_t  x  = rd.i16(), y = rd.i16();
            const uint8_t  ch = rd.u8();
            const uint32_t fg = rd.u32(), bg = rd.u32();
            const uint8_t  sx = rd.u8(), sy = rd.u8();
            if (rd.ok) target.drawChar(x, y, (unsigned char)ch, fg, bg, sx, sy);
            break;
        }
        case DROp::SetText: {
            const int16_t  x   = rd.i16(), y = rd.i16();
            const uint32_t fg  = rd.u32(), bg = rd.u32();
            const uint8_t  sx  = rd.u8(), sy = rd.u8();
            const uint8_t  fid = rd.u8();
            const uint8_t  rot = rd.u8();
            const uint16_t n   = rd.u16();
            const uint8_t *s   = rd.bytes(n);
            if (!rd.ok) break;
            str.assign(s, s + n);
            str.push_back('\0');
            target.setText(x, y, str.data(), fg, bg, sx, sy,
                           fonts ? fonts->get(fid) : nullptr, rot);
            break;
        }

        // ----- bitmaps -------------------------------------------------------

        case DROp::Bitmap1Bit: {
            const int16_t  x = rd.i16(), y = rd.i16(), w = rd.i16(), h = rd.i16();
            const uint32_t fg = rd.u32(), bg = rd.u32();
            if (!rd.ok) break;
            const uint64_t need = monoPlaneBytes(w, h);
            if (!extentOk(w, h, need, rd)) return DRError::BadPayload;
            const uint8_t *bits = rd.bytes((size_t)need);
            if (!rd.ok) break;
            target.drawBitmap(x, y, bits, w, h, fg, bg);
            break;
        }
        case DROp::XBitmap: {
            const int16_t  x = rd.i16(), y = rd.i16(), w = rd.i16(), h = rd.i16();
            const uint32_t fg = rd.u32();
            if (!rd.ok) break;
            const uint64_t need = monoPlaneBytes(w, h);
            if (!extentOk(w, h, need, rd)) return DRError::BadPayload;
            const uint8_t *bits = rd.bytes((size_t)need);
            if (!rd.ok) break;
            target.drawXBitmap(x, y, bits, w, h, fg);
            break;
        }
        case DROp::GrayBitmap: {
            const int16_t x = rd.i16(), y = rd.i16(), w = rd.i16(), h = rd.i16();
            if (!rd.ok) break;
            const uint64_t need = (uint64_t)(uint16_t)w * (uint64_t)(uint16_t)h;
            if (!extentOk(w, h, need, rd)) return DRError::BadPayload;
            const uint8_t *px = rd.bytes((size_t)need);
            if (!rd.ok) break;
            target.drawGrayscaleBitmap(x, y, px, w, h);
            break;
        }
        case DROp::GrayBitmapMask: {
            const int16_t x = rd.i16(), y = rd.i16(), w = rd.i16(), h = rd.i16();
            if (!rd.ok) break;
            const uint64_t pxBytes   = (uint64_t)(uint16_t)w * (uint64_t)(uint16_t)h;
            const uint64_t maskBytes = monoPlaneBytes(w, h);
            if (!extentOk(w, h, pxBytes + maskBytes, rd)) return DRError::BadPayload;
            const uint8_t *px   = rd.bytes((size_t)pxBytes);
            const uint8_t *mask = rd.bytes((size_t)maskBytes);
            if (!rd.ok) break;
            target.drawGrayscaleBitmap(x, y, px, mask, w, h);
            break;
        }
        case DROp::RGBBitmap: {
            const int16_t x = rd.i16(), y = rd.i16(), w = rd.i16(), h = rd.i16();
            if (!rd.ok) break;
            const uint64_t count = (uint64_t)(uint16_t)w * (uint64_t)(uint16_t)h;
            if (!extentOk(w, h, count * 4u, rd)) return DRError::BadPayload;
            argb.resize((size_t)count);
            for (size_t k = 0; k < argb.size(); ++k) argb[k] = rd.u32();
            if (!rd.ok) break;
            target.drawRGBBitmap(x, y, argb.data(), w, h);
            break;
        }
        case DROp::RGBBitmapMask: {
            const int16_t x = rd.i16(), y = rd.i16(), w = rd.i16(), h = rd.i16();
            if (!rd.ok) break;
            const uint64_t count     = (uint64_t)(uint16_t)w * (uint64_t)(uint16_t)h;
            const uint64_t maskBytes = monoPlaneBytes(w, h);
            if (!extentOk(w, h, count * 4u + maskBytes, rd)) return DRError::BadPayload;
            argb.resize((size_t)count);
            for (size_t k = 0; k < argb.size(); ++k) argb[k] = rd.u32();
            const uint8_t *mask = rd.bytes((size_t)maskBytes);
            if (!rd.ok) break;
            target.drawRGBBitmap(x, y, argb.data(), mask, w, h);
            break;
        }
        case DROp::RGB565Bitmap: {
            const int16_t x = rd.i16(), y = rd.i16(), w = rd.i16(), h = rd.i16();
            if (!rd.ok) break;
            const uint64_t count = (uint64_t)(uint16_t)w * (uint64_t)(uint16_t)h;
            if (!extentOk(w, h, count * 2u, rd)) return DRError::BadPayload;
            rgb565.resize((size_t)count);
            for (size_t k = 0; k < rgb565.size(); ++k) rgb565[k] = rd.u16();
            if (!rd.ok) break;
            target.drawRGB565Bitmap(x, y, rgb565.data(), w, h);
            break;
        }
        case DROp::BitmapTriangle: {
            const int16_t x0 = rd.i16(), y0 = rd.i16(), x1 = rd.i16(),
                          y1 = rd.i16(), x2 = rd.i16(), y2 = rd.i16();
            const int16_t tw = rd.i16(), th = rd.i16();
            if (!rd.ok) break;
            const uint64_t count = (uint64_t)(uint16_t)tw * (uint64_t)(uint16_t)th;
            if (!extentOk(tw, th, count * 4u, rd)) return DRError::BadPayload;
            argb.resize((size_t)count);
            for (size_t k = 0; k < argb.size(); ++k) argb[k] = rd.u32();
            if (!rd.ok) break;
            target.drawBitmapTriangle(x0, y0, x1, y1, x2, y2, argb.data(), tw, th);
            break;
        }

        // ----- frame control -------------------------------------------------

        case DROp::SwapBuffers: {
            const uint8_t autoclear = rd.u8();
            if (rd.ok) target.swapBuffers(autoclear != 0);
            break;
        }

        default:
            return DRError::UnknownOpcode;
        }

        if (!rd.ok) return DRError::Truncated;
    }
}

// =============================================================================
// Font registration (recorder side)
// =============================================================================

void DrawReplay::registerFont (const GFXfont *font, uint8_t id) {
    if (!font) return;                          // nullptr is always the built-in
    for (size_t i = 0; i < m_fontIds.size(); ++i) {
        if (m_fontIds[i].font == font) { m_fontIds[i].id = id; return; }
    }
    FontBinding b;
    b.font = font;
    b.id   = id;
    m_fontIds.push_back(b);
}

uint8_t DrawReplay::fontId (const GFXfont *font) const {
    if (!font) return (uint8_t)DR_FONT_BUILTIN;
    for (size_t i = 0; i < m_fontIds.size(); ++i) {
        if (m_fontIds[i].font == font) return m_fontIds[i].id;
    }
    return (uint8_t)DR_FONT_BUILTIN;
}

void DrawReplay::setFontId (uint8_t id) {
    if (allow(DR_REC_TEXT)) {
        beginCmd(DROp::SetFont);
        putU8(id);
    }
    // Mirror it locally when we know the pointer, so cursor maths stays right.
    for (size_t i = 0; i < m_fontIds.size(); ++i) {
        if (m_fontIds[i].id == id) { LinuxGFX::setFont(m_fontIds[i].font); return; }
    }
    if (id == DR_FONT_BUILTIN) LinuxGFX::setFont(nullptr);
}

// =============================================================================
// Geometry
// =============================================================================

void DrawReplay::drawPixel (int16_t x, int16_t y, uint32_t color) {
    if (m_suppress || !allow(DR_REC_PIXEL)) return;
    beginCmd(DROp::DrawPixel);
    putI16(x); putI16(y); putU32(color);
}

void DrawReplay::drawFastVLine (int16_t x, int16_t y, int16_t h, uint32_t color) {
    if (m_suppress || !allow(DR_REC_GEOMETRY)) return;
    beginCmd(DROp::FastVLine);
    putI16(x); putI16(y); putI16(h); putU32(color);
}

void DrawReplay::drawFastHLine (int16_t x, int16_t y, int16_t w, uint32_t color) {
    if (m_suppress || !allow(DR_REC_GEOMETRY)) return;
    beginCmd(DROp::FastHLine);
    putI16(x); putI16(y); putI16(w); putU32(color);
}

void DrawReplay::fillScreen (uint32_t color) {
    if (m_suppress || !allow(DR_REC_GEOMETRY)) return;
    beginCmd(DROp::FillScreen);
    putU32(color);
}

void DrawReplay::drawLine (int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint32_t color) {
    if (!allow(DR_REC_GEOMETRY)) return;
    beginCmd(DROp::Line);
    putI16(x0); putI16(y0); putI16(x1); putI16(y1); putU32(color);
}

void DrawReplay::drawRect (int16_t x, int16_t y, int16_t w, int16_t h, uint32_t color) {
    if (!allow(DR_REC_GEOMETRY)) return;
    beginCmd(DROp::Rect);
    putI16(x); putI16(y); putI16(w); putI16(h); putU32(color);
}

void DrawReplay::fillRect (int16_t x, int16_t y, int16_t w, int16_t h, uint32_t color) {
    if (!allow(DR_REC_GEOMETRY)) return;
    beginCmd(DROp::FillRect);
    putI16(x); putI16(y); putI16(w); putI16(h); putU32(color);
}

void DrawReplay::drawCircle (int16_t x0, int16_t y0, int16_t r, uint32_t color) {
    if (!allow(DR_REC_GEOMETRY)) return;
    beginCmd(DROp::Circle);
    putI16(x0); putI16(y0); putI16(r); putU32(color);
}

void DrawReplay::fillCircle (int16_t x0, int16_t y0, int16_t r, uint32_t color) {
    if (!allow(DR_REC_GEOMETRY)) return;
    beginCmd(DROp::FillCircle);
    putI16(x0); putI16(y0); putI16(r); putU32(color);
}

void DrawReplay::drawArc (int16_t x0, int16_t y0, int16_t r,
                          float startAngle, float endAngle, uint32_t color) {
    if (!allow(DR_REC_GEOMETRY)) return;
    beginCmd(DROp::Arc);
    putI16(x0); putI16(y0); putI16(r);
    putF32(startAngle); putF32(endAngle);
    putU32(color);
}

void DrawReplay::drawRoundRect (int16_t x, int16_t y, int16_t w, int16_t h,
                                int16_t radius, uint32_t color) {
    if (!allow(DR_REC_GEOMETRY)) return;
    beginCmd(DROp::RoundRect);
    putI16(x); putI16(y); putI16(w); putI16(h); putI16(radius); putU32(color);
}

void DrawReplay::fillRoundRect (int16_t x, int16_t y, int16_t w, int16_t h,
                                int16_t radius, uint32_t color) {
    if (!allow(DR_REC_GEOMETRY)) return;
    beginCmd(DROp::FillRoundRect);
    putI16(x); putI16(y); putI16(w); putI16(h); putI16(radius); putU32(color);
}

void DrawReplay::drawTriangle (int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                               int16_t x2, int16_t y2, uint32_t color) {
    if (!allow(DR_REC_GEOMETRY)) return;
    beginCmd(DROp::Triangle);
    putI16(x0); putI16(y0); putI16(x1); putI16(y1); putI16(x2); putI16(y2);
    putU32(color);
}

void DrawReplay::fillTriangle (int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                               int16_t x2, int16_t y2, uint32_t color) {
    if (!allow(DR_REC_GEOMETRY)) return;
    beginCmd(DROp::FillTriangle);
    putI16(x0); putI16(y0); putI16(x1); putI16(y1); putI16(x2); putI16(y2);
    putU32(color);
}

// =============================================================================
// Bitmaps — the only commands that inline pixel data (see the record mask)
// =============================================================================

void DrawReplay::drawBitmap (int16_t x, int16_t y, const uint8_t bitmap[],
                             int16_t w, int16_t h, uint32_t color) {
    drawBitmap(x, y, bitmap, w, h, color, GFX_TRANSPARENT);
}

void DrawReplay::drawBitmap (int16_t x, int16_t y, const uint8_t bitmap[],
                             int16_t w, int16_t h, uint32_t color, uint32_t bg) {
    if (!bitmap || w <= 0 || h <= 0) return;
    if (!allow(DR_REC_BITMAP_1BIT, (size_t)monoPlaneBytes(w, h))) return;
    beginCmd(DROp::Bitmap1Bit);
    putI16(x); putI16(y); putI16(w); putI16(h);
    putU32(color); putU32(bg);
    putMonoPlane(bitmap, w, h);
}

void DrawReplay::drawBitmap (int16_t x, int16_t y, uint8_t *bitmap,
                             int16_t w, int16_t h, uint32_t color) {
    drawBitmap(x, y, (const uint8_t *)bitmap, w, h, color, GFX_TRANSPARENT);
}

void DrawReplay::drawBitmap (int16_t x, int16_t y, uint8_t *bitmap,
                             int16_t w, int16_t h, uint32_t color, uint32_t bg) {
    drawBitmap(x, y, (const uint8_t *)bitmap, w, h, color, bg);
}

void DrawReplay::drawXBitmap (int16_t x, int16_t y, const uint8_t bitmap[],
                              int16_t w, int16_t h, uint32_t color) {
    if (!bitmap || w <= 0 || h <= 0) return;
    if (!allow(DR_REC_BITMAP_XBM, (size_t)monoPlaneBytes(w, h))) return;
    beginCmd(DROp::XBitmap);
    putI16(x); putI16(y); putI16(w); putI16(h);
    putU32(color);
    putMonoPlane(bitmap, w, h);
}

void DrawReplay::drawGrayscaleBitmap (int16_t x, int16_t y, const uint8_t bitmap[],
                                      int16_t w, int16_t h) {
    if (!bitmap || w <= 0 || h <= 0) return;
    const size_t bytes = (size_t)w * (size_t)h;
    if (!allow(DR_REC_BITMAP_GRAY, bytes)) return;
    beginCmd(DROp::GrayBitmap);
    putI16(x); putI16(y); putI16(w); putI16(h);
    putBytes(bitmap, bytes);
}

void DrawReplay::drawGrayscaleBitmap (int16_t x, int16_t y, uint8_t *bitmap,
                                      int16_t w, int16_t h) {
    drawGrayscaleBitmap(x, y, (const uint8_t *)bitmap, w, h);
}

void DrawReplay::drawGrayscaleBitmap (int16_t x, int16_t y, const uint8_t bitmap[],
                                      const uint8_t mask[], int16_t w, int16_t h) {
    if (!bitmap || !mask || w <= 0 || h <= 0) return;
    const size_t bytes = (size_t)w * (size_t)h + (size_t)monoPlaneBytes(w, h);
    if (!allow(DR_REC_BITMAP_GRAY, bytes)) return;
    beginCmd(DROp::GrayBitmapMask);
    putI16(x); putI16(y); putI16(w); putI16(h);
    putBytes(bitmap, (size_t)w * (size_t)h);
    putMonoPlane(mask, w, h);
}

void DrawReplay::drawGrayscaleBitmap (int16_t x, int16_t y, uint8_t *bitmap,
                                      uint8_t *mask, int16_t w, int16_t h) {
    drawGrayscaleBitmap(x, y, (const uint8_t *)bitmap, (const uint8_t *)mask, w, h);
}

void DrawReplay::drawRGBBitmap (int16_t x, int16_t y, const uint32_t bitmap[],
                                int16_t w, int16_t h) {
    if (!bitmap || w <= 0 || h <= 0) return;
    const size_t count = (size_t)w * (size_t)h;
    if (!allow(DR_REC_BITMAP_RGB, count * 4u)) return;
    beginCmd(DROp::RGBBitmap);
    putI16(x); putI16(y); putI16(w); putI16(h);
    for (size_t i = 0; i < count; ++i) putU32(bitmap[i]);
}

void DrawReplay::drawRGBBitmap (int16_t x, int16_t y, uint32_t *bitmap,
                                int16_t w, int16_t h) {
    drawRGBBitmap(x, y, (const uint32_t *)bitmap, w, h);
}

void DrawReplay::drawRGBBitmap (int16_t x, int16_t y, const uint32_t bitmap[],
                                const uint8_t mask[], int16_t w, int16_t h) {
    if (!bitmap || !mask || w <= 0 || h <= 0) return;
    const size_t count = (size_t)w * (size_t)h;
    if (!allow(DR_REC_BITMAP_RGB, count * 4u + (size_t)monoPlaneBytes(w, h))) return;
    beginCmd(DROp::RGBBitmapMask);
    putI16(x); putI16(y); putI16(w); putI16(h);
    for (size_t i = 0; i < count; ++i) putU32(bitmap[i]);
    putMonoPlane(mask, w, h);
}

void DrawReplay::drawRGBBitmap (int16_t x, int16_t y, uint32_t *bitmap,
                                uint8_t *mask, int16_t w, int16_t h) {
    drawRGBBitmap(x, y, (const uint32_t *)bitmap, (const uint8_t *)mask, w, h);
}

void DrawReplay::drawRGB565Bitmap (int16_t x, int16_t y, const uint16_t *bitmap,
                                   int16_t w, int16_t h) {
    if (!bitmap || w <= 0 || h <= 0) return;
    const size_t count = (size_t)w * (size_t)h;
    if (!allow(DR_REC_BITMAP_565, count * 2u)) return;
    beginCmd(DROp::RGB565Bitmap);
    putI16(x); putI16(y); putI16(w); putI16(h);
    for (size_t i = 0; i < count; ++i) putU16(bitmap[i]);
}

void DrawReplay::drawBitmapTriangle (int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                                     int16_t x2, int16_t y2,
                                     const uint32_t *bitmap, int16_t texW, int16_t texH) {
    if (!bitmap || texW <= 0 || texH <= 0) return;
    const size_t count = (size_t)texW * (size_t)texH;
    if (!allow(DR_REC_BITMAP_TRI, count * 4u)) return;
    beginCmd(DROp::BitmapTriangle);
    putI16(x0); putI16(y0); putI16(x1); putI16(y1); putI16(x2); putI16(y2);
    putI16(texW); putI16(texH);
    for (size_t i = 0; i < count; ++i) putU32(bitmap[i]);
}

void DrawReplay::drawBitmapTriangle (int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                                     int16_t x2, int16_t y2,
                                     uint32_t *bitmap, int16_t texW, int16_t texH) {
    drawBitmapTriangle(x0, y0, x1, y1, x2, y2, (const uint32_t *)bitmap, texW, texH);
}

// =============================================================================
// Text
//
// Each of these emits its compact command, then re-runs the base implementation
// with pixel output suppressed. That costs a few hundred no-op iterations but
// keeps the local cursor, wrap and size state exactly in step with what the
// renderer will compute, so getCursorX()/getCursorY() stay meaningful and
// chained print() calls land where the caller expects.
// =============================================================================

void DrawReplay::setCursor (int16_t x, int16_t y) {
    if (allow(DR_REC_TEXT)) {
        beginCmd(DROp::SetCursor);
        putI16(x); putI16(y);
    }
    LinuxGFX::setCursor(x, y);
}

void DrawReplay::setTextColor (uint32_t c) {
    setTextColor(c, GFX_TRANSPARENT);
}

void DrawReplay::setTextColor (uint32_t c, uint32_t bg) {
    if (allow(DR_REC_TEXT)) {
        beginCmd(DROp::SetTextColor);
        putU32(c); putU32(bg);
    }
    LinuxGFX::setTextColor(c, bg);
}

void DrawReplay::setTextColorTransparentBg (uint32_t c) {
    setTextColor(c, GFX_TRANSPARENT);
}

void DrawReplay::setTextSize (uint8_t s) {
    setTextSize(s, s);
}

void DrawReplay::setTextSize (uint8_t sx, uint8_t sy) {
    if (allow(DR_REC_TEXT)) {
        beginCmd(DROp::SetTextSize);
        putU8(sx); putU8(sy);
    }
    LinuxGFX::setTextSize(sx, sy);
}

void DrawReplay::setTextWrap (bool w) {
    if (allow(DR_REC_TEXT)) {
        beginCmd(DROp::SetTextWrap);
        putU8(w ? 1u : 0u);
    }
    LinuxGFX::setTextWrap(w);
}

void DrawReplay::setTextRotation (uint8_t rotation) {
    if (allow(DR_REC_TEXT)) {
        beginCmd(DROp::SetTextRotation);
        putU8(rotation);
    }
    LinuxGFX::setTextRotation(rotation);
}

void DrawReplay::setFont (const GFXfont *f) {
    if (allow(DR_REC_TEXT)) {
        beginCmd(DROp::SetFont);
        putU8(fontId(f));
    }
    LinuxGFX::setFont(f);
}

void DrawReplay::drawChar (int16_t x, int16_t y, unsigned char c,
                           uint32_t color, uint32_t bg, uint8_t size) {
    drawChar(x, y, c, color, bg, size, size);
}

void DrawReplay::drawChar (int16_t x, int16_t y, unsigned char c,
                           uint32_t color, uint32_t bg, uint8_t size_x, uint8_t size_y) {
    if (!allow(DR_REC_TEXT)) return;
    beginCmd(DROp::DrawChar);
    putI16(x); putI16(y); putU8((uint8_t)c);
    putU32(color); putU32(bg);
    putU8(size_x); putU8(size_y);
}

void DrawReplay::writeText (const char *text) {
    if (!text || !*text) return;
    if (allow(DR_REC_TEXT)) {
        beginCmd(DROp::WriteText);
        putText(text);
    }
    SuppressScope guard(this);
    LinuxGFX::writeText(text);
}

void DrawReplay::writeTextF (const char *fmt, ...) {
    if (!fmt) return;
    char    buf[512];
    va_list args;
    va_start(args, fmt);
    const int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n <= 0) return;
    writeText(buf);
}

void DrawReplay::setText (int16_t x, int16_t y, const char *text,
                          uint32_t color, uint32_t bgColor,
                          uint8_t sizeX, uint8_t sizeY, const GFXfont *font,
                          uint8_t rotation) {
    if (!text) return;
    if (allow(DR_REC_TEXT)) {
        beginCmd(DROp::SetText);
        putI16(x); putI16(y);
        putU32(color); putU32(bgColor);
        putU8(sizeX); putU8(sizeY);
        putU8(fontId(font)); putU8(rotation);
        putText(text);
    }
    SuppressScope guard(this);
    LinuxGFX::setText(x, y, text, color, bgColor, sizeX, sizeY, font, rotation);
}

void DrawReplay::print (const char *text) {
    writeText(text);
}

void DrawReplay::println (const char *text) {
    writeText(text);
    println();
}

void DrawReplay::println () {
    writeText("\n");
}

// =============================================================================
// Style / control
// =============================================================================

void DrawReplay::setDrawStyle (const GFXDrawStyle &style) {
    if (allow(DR_REC_STYLE)) {
        beginCmd(DROp::SetDrawStyle);
        putU8(style.strokeWidth);
        putU8((uint8_t)style.lineCap);
        putU8((uint8_t)style.lineJoin);
        putU8(style.antiAlias ? 1u : 0u);
    }
    LinuxGFX::setDrawStyle(style);
}

void DrawReplay::setStrokeWidth (uint8_t width) {
    GFXDrawStyle st = m_drawStyle;
    st.strokeWidth  = width;
    setDrawStyle(st);
}

void DrawReplay::setLineCap (GFXLineCap cap) {
    GFXDrawStyle st = m_drawStyle;
    st.lineCap      = cap;
    setDrawStyle(st);
}

void DrawReplay::setLineJoin (GFXLineJoin join) {
    GFXDrawStyle st = m_drawStyle;
    st.lineJoin     = join;
    setDrawStyle(st);
}

void DrawReplay::setAntiAlias (bool enable) {
    GFXDrawStyle st = m_drawStyle;
    st.antiAlias    = enable;
    setDrawStyle(st);
}

void DrawReplay::setRotation (uint8_t r) {
    if (allow(DR_REC_STYLE)) {
        beginCmd(DROp::SetRotation);
        putU8(r);
    }
    LinuxGFX::setRotation(r);
}

void DrawReplay::invertDisplay (bool i) {
    if (allow(DR_REC_STYLE)) {
        beginCmd(DROp::InvertDisplay);
        putU8(i ? 1u : 0u);
    }
    m_inverted = i;
}

void DrawReplay::swapBuffers (bool autoclear) {
    if (!allow(DR_REC_FRAME)) return;
    beginCmd(DROp::SwapBuffers);
    putU8(autoclear ? 1u : 0u);
}

// =============================================================================
// Base-class pixel hooks
//
// These catch draw calls made through a LinuxGFX& reference, which reach the
// base implementations and decompose into spans and pixels. Recording them
// keeps such calls correct, just more verbose on the wire than calling the
// DrawReplay overloads directly.
// =============================================================================

void DrawReplay::setPixel (int16_t x, int16_t y, uint32_t color) {
    if (m_suppress || !allow(DR_REC_PIXEL)) return;
    beginCmd(DROp::DrawPixel);
    putI16(x); putI16(y); putU32(color);
}

uint32_t DrawReplay::getPixel (int16_t, int16_t) const {
    return 0;   // nothing is rasterised, so there is nothing to read back
}

void DrawReplay::drawFastVLineInternal (int16_t x, int16_t y, int16_t h, uint32_t color) {
    if (m_suppress || !allow(DR_REC_GEOMETRY)) return;
    beginCmd(DROp::FastVLine);
    putI16(x); putI16(y); putI16(h); putU32(color);
}

void DrawReplay::drawFastHLineInternal (int16_t x, int16_t y, int16_t w, uint32_t color) {
    if (m_suppress || !allow(DR_REC_GEOMETRY)) return;
    beginCmd(DROp::FastHLine);
    putI16(x); putI16(y); putI16(w); putU32(color);
}
