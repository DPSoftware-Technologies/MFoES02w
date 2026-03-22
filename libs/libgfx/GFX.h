#ifndef GFX_H
#define GFX_H

#include <cstdint>
#include <cstdlib>
#include <cstring>

// ===== BACKEND SELECTION =====================================================
//
// Two back-ends, selected at compile time:
//
//  1. DRM/KMS dumb-buffer  (default — define GFX_USE_DRM_DUMB or nothing)
//     Allocates GPU-side dumb buffers via DRM_IOCTL_MODE_CREATE_DUMB,
//     memory-maps them for CPU writes, and page-flips via KMS with zero copy.
//     No OpenGL, no EGL, no GBM.  Uses XRGB8888 (32 bpp) — full RGB888 output.
//     Build: g++ -O2 -std=c++14 -o demo demo.cpp GFX.cpp -ldrm
//
//  2. /dev/fb0 framebuffer  (define GFX_USE_FB)
//     Legacy CPU-rendering into mmap'd /dev/fb0.
//     Requires the framebuffer to be configured at 32 bpp (ARGB8888).
//     Supports triple-buffering with malloc buffers + memcpy.
//     Build: g++ -O2 -std=c++14 -DGFX_USE_FB -o demo demo.cpp GFX.cpp
//

#if !defined(GFX_USE_DRM_DUMB) && !defined(GFX_USE_FB)
#  define GFX_USE_DRM_DUMB
#endif

#ifdef GFX_USE_DRM_DUMB
#  include <drm/drm_mode.h>
#endif

// ===== COLOR FORMAT: ARGB8888 ================================================
//
// All color values are 32-bit ARGB8888:
//
//   0xAARRGGBB
//     ││││││└─ Blue  0–255
//     ││││└─── Green 0–255
//     ││└───── Red   0–255
//     └─────── Alpha 0=fully transparent … 255=fully opaque
//
// Alpha behaviour:
//   Alpha=255 (0xFF______) — fully opaque fast path, no readback needed.
//   Alpha=0   (0x00______) — GFX_TRANSPARENT: pixel is skipped entirely.
//   Alpha=1–254            — true alpha blend over existing framebuffer pixel.
//
// Backwards-compatible color helpers:
//   color565(r,g,b)            same call as before; now returns opaque ARGB8888
//                              with full 8-bit precision (no more 5/6-bit loss).
//   color888(r,g,b)            explicit RGB888, alpha=255.
//   colorRGB(r,g,b)            alias for color888.
//   colorARGB(a,r,g,b)         full ARGB control.
//
// Convenience color constants:
//   GFX_TRANSPARENT  0x00000000  skip pixel entirely
//   GFX_BLACK        0xFF000000
//   GFX_WHITE        0xFFFFFFFF
//   GFX_RED          0xFFFF0000
//   GFX_GREEN        0xFF00FF00
//   GFX_BLUE         0xFF0000FF
//   GFX_YELLOW       0xFFFFFF00
//   GFX_CYAN         0xFF00FFFF
//   GFX_MAGENTA      0xFFFF00FF
//
#define GFX_TRANSPARENT  0x00000000u
#define GFX_BLACK        0xFF000000u
#define GFX_WHITE        0xFFFFFFFFu
#define GFX_RED          0xFFFF0000u
#define GFX_GREEN        0xFF00FF00u
#define GFX_BLUE         0xFF0000FFu
#define GFX_YELLOW       0xFFFFFF00u
#define GFX_CYAN         0xFF00FFFFu
#define GFX_MAGENTA      0xFFFF00FFu

// ===== FONT STRUCTURES (Compatible with Adafruit GFX) ========================

typedef struct {
    uint16_t bitmapOffset;
    uint8_t  width, height;
    uint8_t  xAdvance;
    int8_t   xOffset, yOffset;
} GFXglyph;

typedef struct {
    uint8_t  *bitmap;
    GFXglyph *glyph;
    uint16_t  first, last;
    uint8_t   yAdvance;
} GFXfont;

// ===== MULTI-BUFFER SUPPORT (Framebuffer back-end only) ======================

enum BufferIndex { BUFFER_0 = 0, BUFFER_1 = 1, BUFFER_2 = 2 };

typedef struct {
    uint32_t *pData;   ///< ARGB8888 pixel buffer
    bool      bOwned;
    bool      bReady;
} FrameBuffer;

// ===== LinuxGFX ===============================================================
/**
 * Adafruit-GFX-compatible graphics library for Linux / Buildroot on RPi Zero 2W.
 *
 * Color format: ARGB8888 (32-bit, 0xAARRGGBB).
 *   Full RGB888 output — no colour quantisation vs the old RGB565 mode.
 *   Per-pixel alpha blending on all primitives and text.
 *   Alpha=0 → pixel skipped (GFX_TRANSPARENT).
 *   Alpha=255 → fully opaque fast path (no readback).
 *   Alpha=1–254 → blended over existing framebuffer contents.
 *
 * Backwards compatibility:
 *   color565(r,g,b) still works — returns opaque ARGB8888 with full 8-bit RGB.
 *   Old uint16_t color variables → change to uint32_t (compiler will warn).
 *
 * Backwards-compatible alias: typedef LinuxGFX CircleGFX;
 */
class LinuxGFX {
public:

#ifdef GFX_USE_DRM_DUMB
    /**
     * DRM/KMS dumb-buffer back-end constructor.
     * @param drmdev  DRM/KMS device node (e.g. "/dev/dri/card0").
     */
    explicit LinuxGFX(const char *drmdev = "/dev/dri/card0");
#else
    /**
     * Framebuffer back-end constructor.
     * @param fbdev  Framebuffer device node (e.g. "/dev/fb0").
     *               The framebuffer must be at 32 bpp.
     */
    explicit LinuxGFX(const char *fbdev = "/dev/fb0");
#endif

    virtual ~LinuxGFX();

    // ===== CORE DRAW API =====================================================

    void drawPixel      (int16_t x, int16_t y, uint32_t color);
    void startWrite     (void);
    void endWrite       (void);
    void writePixel     (int16_t x, int16_t y, uint32_t color);
    void writeFillRect  (int16_t x, int16_t y, int16_t w, int16_t h, uint32_t color);
    void writeFastVLine (int16_t x, int16_t y, int16_t h, uint32_t color);
    void writeFastHLine (int16_t x, int16_t y, int16_t w, uint32_t color);
    void writeLine      (int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint32_t color);

    // ===== BASIC DRAW API ====================================================

    void drawFastVLine  (int16_t x, int16_t y, int16_t h, uint32_t color);
    void drawFastHLine  (int16_t x, int16_t y, int16_t w, uint32_t color);
    void drawLine       (int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint32_t color);
    void drawRect       (int16_t x, int16_t y, int16_t w, int16_t h, uint32_t color);
    void fillRect       (int16_t x, int16_t y, int16_t w, int16_t h, uint32_t color);
    void fillScreen     (uint32_t color);

    void drawCircle     (int16_t x0, int16_t y0, int16_t r, uint32_t color);
    void fillCircle     (int16_t x0, int16_t y0, int16_t r, uint32_t color);

    void drawRoundRect  (int16_t x, int16_t y, int16_t w, int16_t h, int16_t radius, uint32_t color);
    void fillRoundRect  (int16_t x, int16_t y, int16_t w, int16_t h, int16_t radius, uint32_t color);

    void drawTriangle   (int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                         int16_t x2, int16_t y2, uint32_t color);
    void fillTriangle   (int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                         int16_t x2, int16_t y2, uint32_t color);

    // ===== BITMAP DRAW API ===================================================
    // 1-bit bitmaps: color/bg are ARGB8888 (bg=GFX_TRANSPARENT to skip bg pixels)

    void drawBitmap(int16_t x, int16_t y, const uint8_t bitmap[],
                    int16_t w, int16_t h, uint32_t color);
    void drawBitmap(int16_t x, int16_t y, const uint8_t bitmap[],
                    int16_t w, int16_t h, uint32_t color, uint32_t bg);
    void drawBitmap(int16_t x, int16_t y, uint8_t *bitmap,
                    int16_t w, int16_t h, uint32_t color);
    void drawBitmap(int16_t x, int16_t y, uint8_t *bitmap,
                    int16_t w, int16_t h, uint32_t color, uint32_t bg);

    void drawXBitmap(int16_t x, int16_t y, const uint8_t bitmap[],
                     int16_t w, int16_t h, uint32_t color);

    void drawGrayscaleBitmap(int16_t x, int16_t y, const uint8_t bitmap[], int16_t w, int16_t h);
    void drawGrayscaleBitmap(int16_t x, int16_t y, uint8_t *bitmap,        int16_t w, int16_t h);
    void drawGrayscaleBitmap(int16_t x, int16_t y, const uint8_t bitmap[],
                             const uint8_t mask[], int16_t w, int16_t h);
    void drawGrayscaleBitmap(int16_t x, int16_t y, uint8_t *bitmap,
                             uint8_t *mask, int16_t w, int16_t h);

    // RGB bitmaps: pixel data is ARGB8888 (uint32_t per pixel)
    void drawRGBBitmap(int16_t x, int16_t y, const uint32_t bitmap[], int16_t w, int16_t h);
    void drawRGBBitmap(int16_t x, int16_t y,       uint32_t *bitmap,  int16_t w, int16_t h);
    void drawRGBBitmap(int16_t x, int16_t y, const uint32_t bitmap[],
                       const uint8_t mask[], int16_t w, int16_t h);
    void drawRGBBitmap(int16_t x, int16_t y, uint32_t *bitmap,
                       uint8_t *mask, int16_t w, int16_t h);

    void drawRGB565Bitmap(int16_t x, int16_t y, const uint16_t *bitmap, int16_t w, int16_t h);

    // ===== TEXT API ==========================================================

    void setCursor (int16_t x, int16_t y);
    void setTextColor (uint32_t c);                  ///< fg color; bg = GFX_TRANSPARENT
    void setTextColor (uint32_t c, uint32_t bg);     ///< pass GFX_TRANSPARENT for transparent bg
    void setTextColorTransparentBg(uint32_t c);      ///< convenience: fg only, transparent bg
    void setTextSize (uint8_t s);
    void setTextSize (uint8_t sx, uint8_t sy);
    void setTextWrap (bool w);
    void drawChar (int16_t x, int16_t y, unsigned char c,
                   uint32_t color, uint32_t bg, uint8_t size);
    void drawChar (int16_t x, int16_t y, unsigned char c,
                   uint32_t color, uint32_t bg, uint8_t size_x, uint8_t size_y);
    void writeText (const char *text);
    void setFont (const GFXfont *f = nullptr);
    void writeTextF(const char* fmt, ...);

    // ===== CONTROL API =======================================================

    void setRotation (uint8_t r);
    uint8_t getRotation (void) const;
    void invertDisplay(bool i);

    // ===== DIMENSION API =====================================================

    int16_t width() const;
    int16_t height() const;
    int16_t getCursorX() const;
    int16_t getCursorY() const;

    // ===== COLOR HELPERS =====================================================

    /// Backwards-compatible: same signature as before.
    /// Now returns full opaque ARGB8888 — no more 5/6-bit colour loss.
    static uint32_t color565(uint8_t r, uint8_t g, uint8_t b);
    static uint32_t color565(uint32_t rgb);          ///< color565(0xRRGGBB) → 0xFFRRGGBB

    static uint32_t color888(uint8_t r, uint8_t g, uint8_t b);             ///< alpha=255
    static uint32_t colorRGB(uint8_t r, uint8_t g, uint8_t b);            ///< alias for color888
    static uint32_t colorARGB(uint8_t a, uint8_t r, uint8_t g, uint8_t b); ///< full ARGB

    static uint32_t fromRGB565(uint16_t c);

    // ===== PRESENT / SWAP BUFFERS ============================================
    /**
     * Present the rendered frame.
     *
     *  DRM dumb-buffer back-end:
     *    Flips draw ↔ display buffers via KMS.  Zero memcpy.
     *    @param async      true  = non-blocking drmModePageFlip (vblank-synced);
     *                      false = blocking drmModeSetCrtc (immediate).
     *    @param autoclear  Zero the new draw buffer after the flip.
     *
     *  Framebuffer back-end:
     *    With multi-buffering: memcpy to /dev/fb0.
     *    Without multi-buffering: no-op (direct writes already visible).
     */
#ifdef GFX_USE_DRM_DUMB
    void swapBuffers(bool async = true, bool autoclear = true);

    /** Block until a pending non-blocking page-flip completes. */
    void waitForFlip();

    /** Returns true if a non-blocking flip is currently in flight. */
    bool isFlipPending() const { return m_flipPending; }

#else
    void swapBuffers(bool autoclear = true);

    // ===== MULTI-BUFFER API (Framebuffer back-end only) ======================

    bool      enableMultiBuffer    (uint8_t numBuffers = 2);
    bool      isMultiBuffered      () const;
    uint8_t   getBufferCount       () const;
    uint8_t   getDrawBufferIndex   () const;
    uint8_t   getDisplayBufferIndex() const;

    bool      selectDrawBuffer     (uint8_t bufferIndex);
    bool      selectDisplayBuffer  (uint8_t bufferIndex);

    void      clearBuffer          (int8_t bufferIndex = -1, uint32_t color = 0);
    uint32_t* getBuffer            (uint8_t bufferIndex);

    bool      attachExternalBuffer (uint8_t bufferIndex, uint32_t *pBuffer);
    bool      detachExternalBuffer (uint8_t bufferIndex);
#endif

protected:

    void     setPixel (int16_t x, int16_t y, uint32_t color);
    uint32_t getPixel (int16_t x, int16_t y) const;

    /// Alpha-blend src over dst.  Both ARGB8888.  Returns blended ARGB8888.
    static inline uint32_t blendARGB(uint32_t src, uint32_t dst) {
        uint32_t sa = (src >> 24) & 0xFF;
        if (sa == 0xFF) return src;             // fully opaque fast path
        if (sa == 0x00) return dst;             // fully transparent fast path
        uint32_t da = 255u - sa;
        uint8_t r = (uint8_t)(((src >> 16 & 0xFF) * sa + (dst >> 16 & 0xFF) * da) / 255u);
        uint8_t g = (uint8_t)(((src >>  8 & 0xFF) * sa + (dst >>  8 & 0xFF) * da) / 255u);
        uint8_t b = (uint8_t)(((src       & 0xFF) * sa + (dst       & 0xFF) * da) / 255u);
        return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    }

    void drawCircleHelper     (int16_t x0, int16_t y0, int16_t r,
                               uint8_t cornername, uint32_t color);
    void fillCircleHelper     (int16_t x0, int16_t y0, int16_t r,
                               uint8_t cornername, int16_t delta, uint32_t color);
    void drawFastVLineInternal(int16_t x, int16_t y, int16_t h, uint32_t color);
    void drawFastHLineInternal(int16_t x, int16_t y, int16_t w, uint32_t color);

    // ===== Back-end specific members =========================================
#ifdef GFX_USE_DRM_DUMB

    struct DumbBuf {
        uint32_t  handle;   ///< GEM object handle
        uint32_t  pitch;    ///< Bytes per scanline
        uint64_t  size;     ///< Total size in bytes
        uint32_t  fbId;     ///< KMS framebuffer ID
        uint32_t *map;      ///< CPU-writable mmap pointer (XRGB8888 pixels)
    };

    int      m_drmFd;
    uint32_t m_connId;
    uint32_t m_crtcId;
    uint32_t m_modeIdx;

    struct drm_mode_modeinfo m_selectedMode;

    static constexpr int kNumDrmBufs = 2;
    DumbBuf  m_drm[kNumDrmBufs];

    uint8_t   m_drawIdx;
    uint8_t   m_dispIdx;
    bool      m_flipPending;
    uint32_t *m_pBuffer;   ///< Alias → m_drm[m_drawIdx].map

    bool initDrm    (const char *drmdev);
    void cleanupDrm ();
    bool allocDumbBuf(DumbBuf &b);
    void freeDumbBuf (DumbBuf &b);

    static void pageFlipHandler(int fd, unsigned int seq,
                                unsigned int tv_sec, unsigned int tv_usec,
                                void *data);

#else // /dev/fb0 Framebuffer back-end

    int       m_fbFd;
    uint8_t  *m_pFbMem;
    size_t    m_fbMemSize;
    uint32_t  m_pitch;          ///< bytes per line
    uint32_t  m_depth;          ///< bits per pixel (must be 32)
    uint32_t *m_pBuffer;        ///< current draw buffer (ARGB8888)

    FrameBuffer m_buffers[3];
    uint8_t     m_bufferCount;
    uint8_t     m_drawBufferIndex;
    uint8_t     m_displayBufferIndex;
    bool        m_multiBufferEnabled;

    void _initializeMultiBuffer();
    void _cleanupMultiBuffer();
    void _flushToFb();

#endif // GFX_USE_DRM_DUMB

    // ===== Common members ====================================================
    int16_t  m_width, m_height;
    int16_t  m_cursorX, m_cursorY;
    uint32_t m_textColor, m_textBgColor;
    uint8_t  m_textSizeX, m_textSizeY;
    bool     m_textWrap;
    uint8_t  m_rotation;
    bool     m_inverted;
    bool     m_inTransaction;

    const GFXfont *m_pFont;
    bool           m_fontSizeMultiplied;
};

// Backwards-compat alias
typedef LinuxGFX CircleGFX;

#endif // GFX_H