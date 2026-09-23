#ifndef DRAWREPLAY_H
#define DRAWREPLAY_H

#include "GFX.h"

#ifndef GFX_NC5874      // bare-metal target: provided by nc5874_std.h via GFX.h
#include <cstdint>
#include <vector>
#endif

// =============================================================================
// DrawReplay — record draw commands, pack them into bytes, replay elsewhere
// =============================================================================
//
// DrawReplay is a LinuxGFX that rasterises nothing. Every draw call is encoded
// as a compact opcode + payload and appended to an internal command stream.
// DRExport() hands back that stream as a single byte blob; DRRender() decodes
// the blob and issues the equivalent real draw calls on any LinuxGFX target
// (hardware framebuffer, DRM, SDL window, or a GFXcanvas).
//
// The point is bandwidth. A 1280x720 ARGB8888 frame is 3.5 MB. The same frame
// described as "fillScreen + 40 rects + 12 strings" is a few hundred bytes, so
// a remote display can be driven over USB/SPI/TCP without shipping pixels.
//
//   DrawReplay rec(1280, 720);
//   rec.fillScreen(GFX_BLACK);
//   rec.fillRect(10, 10, 100, 50, GFX_RED);
//   rec.setText(20, 200, "hello", GFX_WHITE);
//   std::vector<uint8_t> blob = rec.DRExport();
//   link.send(blob.data(), blob.size());
//   rec.DRFlush();                       // drop history, start the next frame
//
//   // ...on the far side:
//   DrawReplay::DRRender(buf, len, display);
//
// ----- Drop-in use -----------------------------------------------------------
//
// DrawReplay derives from LinuxGFX so existing code that takes a LinuxGFX&
// still works. Calls made directly on a DrawReplay object hit the recording
// overloads and produce one compact command each. Calls made through a
// LinuxGFX& reference reach the base class instead, which decomposes them into
// the virtual pixel/span primitives — those are also recorded, just as a
// longer run of low-level commands. Output is identical either way; only the
// byte count differs.
//
// ----- Fonts -----------------------------------------------------------------
//
// A GFXfont* is a local heap address and cannot travel over a wire. Fonts are
// referenced by a small integer id that both sides agree on: the recorder maps
// pointer -> id via registerFont(), the renderer maps id -> pointer via
// DRFontTable. Id DR_FONT_BUILTIN (0xFF) means "the built-in 5x7 font"
// (setFont(nullptr)). An unknown id at render time falls back to the built-in
// font rather than failing the stream.
//
// ----- Keeping pixels out of the stream --------------------------------------
//
// The bitmap calls are the one family that inlines real pixel data, which is
// exactly the bandwidth the format exists to avoid: one drawRGBBitmap of a
// 200x200 sprite is 160 KB of stream. A record mask selects, per function,
// what is allowed to enter the stream; anything masked off is dropped silently
// and counted by skippedCount().
//
//   rec.DRIgnore(DR_REC_BITMAPS_ALL);              // no pixel payloads at all
//   rec.DRIgnore(DR_REC_BITMAP_RGB |               // drop the fat ones, keep
//                DR_REC_BITMAP_TRI);               // 1-bit glyphs and masks
//   rec.DRSetRecordMask(DR_REC_VECTOR_ONLY);       // same as ignoring all bitmaps
//
// setMaxInlineBytes() is the size-based equivalent: any single command whose
// inline payload would exceed the limit is skipped no matter which flags are
// set. 0 (the default) means no limit.
//
// ----- Limitations -----------------------------------------------------------
//
//   * Recording is write-only: getPixel() always reads back 0, so effects that
//     sample the framebuffer (anti-aliased blends over existing content) are
//     recorded as commands but resolve against the *target's* pixels at render
//     time, which is what you want anyway.
//   * Multi-buffer selection, panel effects, and SDL event handling are local
//     concerns and are not recorded.
//

// ===== WIRE FORMAT ===========================================================
//
// All integers little-endian, packed, no padding. Floats are IEEE-754 binary32.
//
//   Header (DR_HEADER_SIZE = 20 bytes)
//     0  uint8[4]  magic   'D','R','P','1'
//     4  uint16    version DR_VERSION
//     6  uint16    flags   reserved, 0
//     8  int16     width   recorder surface width
//    10  int16     height  recorder surface height
//    12  uint32    cmdCount   number of commands (excluding End)
//    16  uint32    bodyBytes  length of the command stream that follows
//   20  command stream, terminated by DROp::End
//

#define DR_MAGIC_0        'D'
#define DR_MAGIC_1        'R'
#define DR_MAGIC_2        'P'
#define DR_MAGIC_3        '1'
#define DR_VERSION        1u
#define DR_HEADER_SIZE    20u
#define DR_FONT_BUILTIN   0xFFu

/// Command opcodes. Values are frozen wire ABI — append only, never renumber.
enum class DROp : uint8_t {
    End             = 0x00, ///< end of stream (no payload)

    // --- geometry ---
    DrawPixel       = 0x01, ///< i16 x, i16 y, u32 color
    FastVLine       = 0x02, ///< i16 x, i16 y, i16 h, u32 color
    FastHLine       = 0x03, ///< i16 x, i16 y, i16 w, u32 color
    Line            = 0x04, ///< i16 x0,y0,x1,y1, u32 color
    Rect            = 0x05, ///< i16 x,y,w,h, u32 color
    FillRect        = 0x06, ///< i16 x,y,w,h, u32 color
    FillScreen      = 0x07, ///< u32 color
    Circle          = 0x08, ///< i16 x,y,r, u32 color
    FillCircle      = 0x09, ///< i16 x,y,r, u32 color
    Arc             = 0x0A, ///< i16 x,y,r, f32 start, f32 end, u32 color
    RoundRect       = 0x0B, ///< i16 x,y,w,h,radius, u32 color
    FillRoundRect   = 0x0C, ///< i16 x,y,w,h,radius, u32 color
    Triangle        = 0x0D, ///< i16 x0,y0,x1,y1,x2,y2, u32 color
    FillTriangle    = 0x0E, ///< i16 x0,y0,x1,y1,x2,y2, u32 color

    // --- state ---
    SetDrawStyle    = 0x20, ///< u8 strokeWidth, u8 cap, u8 join, u8 antiAlias
    SetRotation     = 0x21, ///< u8 rotation
    InvertDisplay   = 0x22, ///< u8 invert

    // --- text ---
    SetCursor       = 0x30, ///< i16 x, i16 y
    SetTextColor    = 0x31, ///< u32 fg, u32 bg
    SetTextSize     = 0x32, ///< u8 sx, u8 sy
    SetTextWrap     = 0x33, ///< u8 wrap
    SetTextRotation = 0x34, ///< u8 rotation (degrees)
    SetFont         = 0x35, ///< u8 fontId (DR_FONT_BUILTIN = built-in)
    WriteText       = 0x36, ///< u16 len, u8[len] utf8/ascii (no NUL)
    DrawChar        = 0x37, ///< i16 x,y, u8 c, u32 color, u32 bg, u8 sx, u8 sy
    SetText         = 0x38, ///< i16 x,y, u32 color, u32 bg, u8 sx, u8 sy,
                            ///< u8 fontId, u8 rotation, u16 len, u8[len]

    // --- bitmaps ---
    Bitmap1Bit      = 0x40, ///< i16 x,y,w,h, u32 color, u32 bg, u8[stride*h]
    XBitmap         = 0x41, ///< i16 x,y,w,h, u32 color, u8[stride*h]
    GrayBitmap      = 0x42, ///< i16 x,y,w,h, u8[w*h]
    GrayBitmapMask  = 0x43, ///< i16 x,y,w,h, u8[w*h], u8[stride*h]
    RGBBitmap       = 0x44, ///< i16 x,y,w,h, u32[w*h]
    RGBBitmapMask   = 0x45, ///< i16 x,y,w,h, u32[w*h], u8[stride*h]
    RGB565Bitmap    = 0x46, ///< i16 x,y,w,h, u16[w*h]
    BitmapTriangle  = 0x47, ///< i16 x0,y0,x1,y1,x2,y2, i16 texW,texH, u32[texW*texH]

    // --- frame control ---
    SwapBuffers     = 0x50, ///< u8 autoclear
};

// ===== RECORD MASK ===========================================================
//
// One flag per recordable draw family. A cleared flag makes the matching
// recorder calls no-ops: nothing enters the stream, and skippedCount() goes up.
// Purely a recorder-side filter — the wire format and DRRender() are unaffected,
// so a stream recorded with bitmaps masked off still decodes normally.
//
#define DR_REC_GEOMETRY      0x00000001u ///< lines, rects, circles, arcs, triangles
#define DR_REC_TEXT          0x00000002u ///< all text and font/cursor state
#define DR_REC_STYLE         0x00000004u ///< stroke style, rotation, invert
#define DR_REC_FRAME         0x00000008u ///< swapBuffers frame markers
#define DR_REC_PIXEL         0x00000010u ///< single-pixel writes, including the
                                         ///< base-class fallback path
#define DR_REC_BITMAP_1BIT   0x00000100u ///< drawBitmap()          — w*h/8 bytes
#define DR_REC_BITMAP_XBM    0x00000200u ///< drawXBitmap()         — w*h/8 bytes
#define DR_REC_BITMAP_GRAY   0x00000400u ///< drawGrayscaleBitmap() — w*h bytes
#define DR_REC_BITMAP_RGB    0x00000800u ///< drawRGBBitmap()       — w*h*4 bytes
#define DR_REC_BITMAP_565    0x00001000u ///< drawRGB565Bitmap()    — w*h*2 bytes
#define DR_REC_BITMAP_TRI    0x00002000u ///< drawBitmapTriangle()  — texW*texH*4

/// Every bitmap family — the calls that inline raw pixel data.
#define DR_REC_BITMAPS_ALL   0x00003F00u
/// Default: record everything.
#define DR_REC_ALL           0xFFFFFFFFu
/// Commands only, no inline pixel payloads.
#define DR_REC_VECTOR_ONLY   (DR_REC_ALL & ~DR_REC_BITMAPS_ALL)

/// Result of a DRRender()/DRPeek() call.
enum class DRError : uint8_t {
    Ok             = 0, ///< stream decoded and replayed completely
    BadMagic       = 1, ///< missing or wrong 'DRP1' magic
    BadVersion     = 2, ///< stream version newer than DR_VERSION
    Truncated      = 3, ///< payload ended mid-command
    UnknownOpcode  = 4, ///< opcode not understood by this decoder
    BadPayload     = 5, ///< self-inconsistent command (e.g. negative extent)
};

/// Human-readable name for a DRError — handy for logging a rejected frame.
const char *DRErrorString (DRError e);

// ===== DRFontTable ===========================================================
/**
 * Render-side font id -> GFXfont* map.
 *
 * Both ends must agree on the ids. Typically the renderer binds its fonts once
 * at startup with the same ids the recorder registered:
 *
 *   DRFontTable fonts;
 *   fonts.bind(0, &FreeSans12pt7b);
 *   fonts.bind(1, &FreeSansBold24pt7b);
 *   DrawReplay::DRRender(buf, len, display, &fonts);
 */
class DRFontTable {
public:
    /// Bind a font to an id. id DR_FONT_BUILTIN is reserved for the 5x7 font.
    void bind (uint8_t id, const GFXfont *font);

    /// Look up a font. Returns nullptr (built-in font) for unbound ids.
    const GFXfont *get (uint8_t id) const;

    void clear ();

private:
    std::vector<const GFXfont *> m_fonts;
};

// ===== DrawReplay ============================================================

class DrawReplay : public LinuxGFX {
public:
    /**
     * @param w  Surface width the commands are authored against.
     * @param h  Surface height.
     *
     * The dimensions are stored in the exported header so the far side can
     * detect a size mismatch; they also drive local text-wrap bookkeeping.
     */
    DrawReplay (int16_t w, int16_t h);
    ~DrawReplay () override;

    // ===== REPLAY API ========================================================

    /**
     * Pack every command recorded so far into a single self-contained blob
     * (header + command stream + End terminator).
     * The recorder is NOT cleared — call DRFlush() when the frame is sent.
     */
    std::vector<uint8_t> DRExport () const;

    /// Zero-copy variant: appends the blob to @p out and returns bytes appended.
    size_t DRExport (std::vector<uint8_t> &out) const;

    /**
     * Decode a blob produced by DRExport() and issue the equivalent draw calls
     * on @p target.
     *
     * The input is treated as untrusted: every read is bounds-checked and a
     * malformed stream stops at the offending command instead of over-reading.
     * Commands already applied before the error stay applied.
     *
     * @param data    Blob bytes.
     * @param len     Blob length.
     * @param target  Any LinuxGFX — display, or a GFXcanvas for offscreen use.
     * @param fonts   Optional id->font map; nullptr means built-in font only.
     * @return DRError::Ok when the whole stream replayed.
     */
    static DRError DRRender (const uint8_t *data, size_t len, LinuxGFX &target,
                             const DRFontTable *fonts = nullptr);

    /// Convenience overload for a vector blob.
    static DRError DRRender (const std::vector<uint8_t> &blob, LinuxGFX &target,
                             const DRFontTable *fonts = nullptr);

    /// Drop all recorded commands. Draw-state (cursor, colors, font) is kept,
    /// so a per-frame loop can DRFlush() without re-issuing setup calls.
    void DRFlush ();

    /// Drop recorded commands AND reset text/draw state to construction defaults.
    void DRReset ();

    /**
     * Read a blob's header without replaying it.
     * @return DRError::Ok when the header is valid; outputs are then filled in.
     */
    static DRError DRPeek (const uint8_t *data, size_t len,
                           int16_t &outWidth, int16_t &outHeight,
                           uint32_t &outCmdCount, uint32_t &outBodyBytes);

    /**
     * Total length of the blob starting at @p data, for cutting blobs out of a
     * byte stream that does not preserve message boundaries (a USB channel,
     * a pipe, a file of captured frames).
     *
     * @return the blob's full size in bytes, 0 when the header has not fully
     *         arrived yet, or SIZE_MAX when this is not a blob start and the
     *         reader should resynchronise. Those three values are deliberately
     *         what usbproto::StreamAssembler's sizer contract expects, so this
     *         can be handed to it directly.
     */
    static size_t DRRecordSize (const uint8_t *data, size_t len);

    /// The 4 magic bytes every blob starts with ('D','R','P','1'), for
    /// resynchronising a corrupted stream. See DR_SYNC_LEN.
    static const uint8_t *DRSyncMagic ();
    static constexpr size_t DR_SYNC_LEN = 4;

    // ===== RECORD MASK =======================================================

    /// Replace the mask outright. Default is DR_REC_ALL.
    void     DRSetRecordMask (uint32_t mask) { m_recordMask = mask; }
    uint32_t DRGetRecordMask () const        { return m_recordMask; }

    /// Clear the given DR_REC_* flags — those calls stop entering the stream.
    void     DRIgnore (uint32_t features) { m_recordMask &= ~features; }
    /// Set the given DR_REC_* flags.
    void     DRAllow  (uint32_t features) { m_recordMask |=  features; }

    /// True when @p feature (a single DR_REC_* flag) is currently recorded.
    bool     DRRecords (uint32_t feature) const {
        return (m_recordMask & feature) != 0u;
    }

    /**
     * Skip any single command whose inline payload would exceed @p bytes.
     * Applies on top of the record mask; 0 (default) disables the limit.
     * Useful as a blanket guard when bitmap sizes are not known up front.
     */
    void     setMaxInlineBytes (size_t bytes) { m_maxInlineBytes = bytes; }
    size_t   maxInlineBytes    () const       { return m_maxInlineBytes; }

    // ===== RECORDER INTROSPECTION ============================================

    /// Draw calls dropped by the record mask or the inline-size limit.
    size_t   skippedCount () const { return m_skipped; }

    size_t   commandCount () const { return m_cmdCount; }
    size_t   bodyBytes    () const { return m_stream.size(); }
    /// Total size DRExport() would produce (header + body + End terminator).
    size_t   exportSize   () const { return DR_HEADER_SIZE + m_stream.size() + 1u; }
    bool     empty        () const { return m_cmdCount == 0; }

    /// Bytes a raw ARGB8888 frame of this size would have cost, for comparison.
    size_t   rawFrameBytes () const {
        return (size_t)m_width * (size_t)m_height * 4u;
    }

    // ===== FONT REGISTRATION (recorder side) =================================

    /// Map a local GFXfont* to the wire id the renderer will resolve.
    void    registerFont (const GFXfont *font, uint8_t id);

    /// Id currently mapped to @p font, or DR_FONT_BUILTIN when unregistered.
    uint8_t fontId (const GFXfont *font) const;

    /// Record a font change by id directly, without a local GFXfont*.
    void    setFontId (uint8_t id);

    // ===== RECORDING DRAW API ================================================
    // Mirrors LinuxGFX. Each call appends one compact command.

    void drawPixel     (int16_t x, int16_t y, uint32_t color) override;
    void drawFastVLine (int16_t x, int16_t y, int16_t h, uint32_t color) override;
    void drawFastHLine (int16_t x, int16_t y, int16_t w, uint32_t color) override;
    void fillScreen    (uint32_t color) override;

    void drawLine      (int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint32_t color);
    void drawRect      (int16_t x, int16_t y, int16_t w, int16_t h, uint32_t color);
    void fillRect      (int16_t x, int16_t y, int16_t w, int16_t h, uint32_t color);

    void drawCircle    (int16_t x0, int16_t y0, int16_t r, uint32_t color);
    void fillCircle    (int16_t x0, int16_t y0, int16_t r, uint32_t color);
    void drawArc       (int16_t x0, int16_t y0, int16_t r,
                        float startAngle, float endAngle, uint32_t color);

    void drawRoundRect (int16_t x, int16_t y, int16_t w, int16_t h, int16_t radius, uint32_t color);
    void fillRoundRect (int16_t x, int16_t y, int16_t w, int16_t h, int16_t radius, uint32_t color);

    void drawTriangle  (int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                        int16_t x2, int16_t y2, uint32_t color);
    void fillTriangle  (int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                        int16_t x2, int16_t y2, uint32_t color);

    // ----- bitmaps -----------------------------------------------------------

    void drawBitmap (int16_t x, int16_t y, const uint8_t bitmap[],
                     int16_t w, int16_t h, uint32_t color);
    void drawBitmap (int16_t x, int16_t y, const uint8_t bitmap[],
                     int16_t w, int16_t h, uint32_t color, uint32_t bg);
    void drawBitmap (int16_t x, int16_t y, uint8_t *bitmap,
                     int16_t w, int16_t h, uint32_t color);
    void drawBitmap (int16_t x, int16_t y, uint8_t *bitmap,
                     int16_t w, int16_t h, uint32_t color, uint32_t bg);

    void drawXBitmap (int16_t x, int16_t y, const uint8_t bitmap[],
                      int16_t w, int16_t h, uint32_t color);

    void drawGrayscaleBitmap (int16_t x, int16_t y, const uint8_t bitmap[], int16_t w, int16_t h);
    void drawGrayscaleBitmap (int16_t x, int16_t y, uint8_t *bitmap,        int16_t w, int16_t h);
    void drawGrayscaleBitmap (int16_t x, int16_t y, const uint8_t bitmap[],
                              const uint8_t mask[], int16_t w, int16_t h);
    void drawGrayscaleBitmap (int16_t x, int16_t y, uint8_t *bitmap,
                              uint8_t *mask, int16_t w, int16_t h);

    void drawRGBBitmap (int16_t x, int16_t y, const uint32_t bitmap[], int16_t w, int16_t h);
    void drawRGBBitmap (int16_t x, int16_t y,       uint32_t *bitmap,  int16_t w, int16_t h);
    void drawRGBBitmap (int16_t x, int16_t y, const uint32_t bitmap[],
                        const uint8_t mask[], int16_t w, int16_t h);
    void drawRGBBitmap (int16_t x, int16_t y, uint32_t *bitmap,
                        uint8_t *mask, int16_t w, int16_t h);

    void drawRGB565Bitmap (int16_t x, int16_t y, const uint16_t *bitmap, int16_t w, int16_t h);

    void drawBitmapTriangle (int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                             int16_t x2, int16_t y2,
                             const uint32_t *bitmap, int16_t texW, int16_t texH);
    void drawBitmapTriangle (int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                             int16_t x2, int16_t y2,
                             uint32_t *bitmap, int16_t texW, int16_t texH);

    // ----- text --------------------------------------------------------------

    void setCursor      (int16_t x, int16_t y);
    void setTextColor   (uint32_t c);
    void setTextColor   (uint32_t c, uint32_t bg);
    void setTextColorTransparentBg (uint32_t c);
    void setTextSize    (uint8_t s);
    void setTextSize    (uint8_t sx, uint8_t sy);
    void setTextWrap    (bool w);
    void setTextRotation(uint8_t rotation);
    void setFont        (const GFXfont *f = nullptr);
    void drawChar       (int16_t x, int16_t y, unsigned char c,
                         uint32_t color, uint32_t bg, uint8_t size);
    void drawChar       (int16_t x, int16_t y, unsigned char c,
                         uint32_t color, uint32_t bg, uint8_t size_x, uint8_t size_y);
    void writeText      (const char *text);
    void writeTextF     (const char *fmt, ...);
    void setText        (int16_t x, int16_t y, const char *text,
                         uint32_t color = GFX_WHITE, uint32_t bgColor = GFX_TRANSPARENT,
                         uint8_t sizeX = 1, uint8_t sizeY = 1, const GFXfont *font = nullptr,
                         uint8_t rotation = 0);

    void print   (const char *text);
    void println (const char *text);
    void println ();

    // ----- style / control ---------------------------------------------------

    void setDrawStyle  (const GFXDrawStyle &style);
    void setStrokeWidth(uint8_t width);
    void setLineCap    (GFXLineCap cap);
    void setLineJoin   (GFXLineJoin join);
    void setAntiAlias  (bool enable);

    void setRotation   (uint8_t r);
    void invertDisplay (bool i);

    /// Records a frame boundary. Nothing is presented locally.
    void swapBuffers   (bool autoclear = true);

    // Names not shadowed above stay reachable from the base class.
    using LinuxGFX::getDrawStyle;
    using LinuxGFX::getRotation;
    using LinuxGFX::getTextRotation;

protected:
    /// Recording sink for anything that reaches the base class's pixel path
    /// (i.e. draw calls made through a LinuxGFX& reference).
    void     setPixel (int16_t x, int16_t y, uint32_t color) override;
    /// Nothing is rasterised, so there is nothing to read back.
    uint32_t getPixel (int16_t x, int16_t y) const override;

    void drawFastVLineInternal (int16_t x, int16_t y, int16_t h, uint32_t color) override;
    void drawFastHLineInternal (int16_t x, int16_t y, int16_t w, uint32_t color) override;

private:
    // ----- stream writers ----------------------------------------------------
    void beginCmd (DROp op);
    void putU8    (uint8_t v);
    void putU16   (uint16_t v);
    void putI16   (int16_t v);
    void putU32   (uint32_t v);
    void putF32   (float v);
    void putBytes (const void *p, size_t n);
    void putText  (const char *text);

    /// Emit a 1-bit-per-pixel plane, ((w+7)/8)*h bytes.
    void putMonoPlane (const uint8_t *bits, int16_t w, int16_t h);

    /**
     * Record-mask gate. Returns false — and counts a skip — when @p feature is
     * masked off, or when @p inlineBytes exceeds the inline-payload limit.
     */
    bool allow (uint32_t feature, size_t inlineBytes = 0);

    /// True when a shadowed method has already emitted its compact command and
    /// is calling into the base class purely for state bookkeeping. All pixel
    /// output produced during that call is dropped.
    bool m_suppress;

    /// Runs the base implementation of a text call with pixel output dropped,
    /// so the cursor advances exactly as it will on the renderer.
    struct SuppressScope {
        DrawReplay *r;
        explicit SuppressScope (DrawReplay *rec) : r(rec) { r->m_suppress = true; }
        ~SuppressScope () { r->m_suppress = false; }
    };

    std::vector<uint8_t> m_stream;
    size_t               m_cmdCount;

    uint32_t m_recordMask;     ///< DR_REC_* flags currently recorded
    size_t   m_skipped;        ///< calls dropped by the mask / size limit
    size_t   m_maxInlineBytes; ///< 0 = unlimited

    struct FontBinding {
        const GFXfont *font;
        uint8_t        id;
    };
    std::vector<FontBinding> m_fontIds;

    DrawReplay (const DrawReplay &)            = delete;
    DrawReplay &operator= (const DrawReplay &) = delete;
};

#endif // DRAWREPLAY_H
