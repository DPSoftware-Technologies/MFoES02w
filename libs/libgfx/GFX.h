#ifndef GFX_H
#define GFX_H

#include <cstdint>
#include <cstdlib>
#include <cstring>

// ── Type aliases ─────────────────────────────────────────────────────────────
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;

#define MAX(a,b)  ((a)>(b)?(a):(b))
#define MIN(a,b)  ((a)<(b)?(a):(b))
#define SWAP(a,b) do { auto _t=(a);(a)=(b);(b)=_t; } while(0)
#define ABS(a)    ((a)<0?-(a):(a))

// ─── Backend selection ────────────────────────────────────────────────────────
//
// Two back-ends, selected at compile time:
//
//  1. DRM/KMS dumb-buffer  (default — define GFX_USE_DRM_DUMB or nothing)
//     Allocates GPU-side dumb buffers via DRM_IOCTL_MODE_CREATE_DUMB,
//     memory-maps them for CPU writes, and page-flips via KMS with zero copy.
//     No OpenGL, no EGL, no GBM.  The display controller scans out directly
//     from the dumb buffer — this IS direct GPU render/scanout memory.
//     Build: g++ -O2 -std=c++14 -o demo demo.cpp GFX.cpp -ldrm
//
//  2. /dev/fb0 framebuffer  (define GFX_USE_FB)
//     Legacy CPU-rendering into mmap'd /dev/fb0.
//     Supports triple-buffering with malloc buffers + memcpy.
//     Build: g++ -O2 -std=c++14 -DGFX_USE_FB -o demo demo.cpp GFX.cpp
//
// ─────────────────────────────────────────────────────────────────────────────

#if !defined(GFX_USE_DRM_DUMB) && !defined(GFX_USE_FB)
#  define GFX_USE_DRM_DUMB
#endif

#ifdef GFX_USE_DRM_DUMB
// Kernel uAPI header — ships with linux-headers in Buildroot; no libdrm needed.
#  include <drm/drm_mode.h>
#endif

// ===== FONT STRUCTURES (Compatible with Adafruit GFX) ========================

typedef struct {
    u16 bitmapOffset;
    u8  width, height;
    u8  xAdvance;
    s8  xOffset, yOffset;
} GFXglyph;

typedef struct {
    u8       *bitmap;
    GFXglyph *glyph;
    u16       first, last;
    u8        yAdvance;
} GFXfont;

// ===== MULTI-BUFFER SUPPORT (Framebuffer back-end only) ======================

enum BufferIndex { BUFFER_0 = 0, BUFFER_1 = 1, BUFFER_2 = 2 };

typedef struct {
    uint16_t *pData;
    bool      bOwned;
    bool      bReady;
} FrameBuffer;

// ===== LinuxGFX ===============================================================
/**
 * Adafruit-GFX-compatible graphics library for Linux / Buildroot on RPi Zero 2W.
 *
 * Two back-ends, selected at compile time:
 *
 *   1. DRM/KMS dumb-buffer  (default / -DGFX_USE_DRM_DUMB)
 *      Allocates GPU-side "dumb buffers" via DRM_IOCTL_MODE_CREATE_DUMB.
 *      These live in GPU-accessible memory; the CPU writes pixels into them
 *      via a normal mmap, then drmModePageFlip hands them to the display
 *      controller for zero-copy scanout — no memcpy, no GL, no EGL.
 *      Supports double-buffering (draw / display ping-pong).
 *      Build: g++ -O2 -std=c++14 -o demo demo.cpp GFX.cpp -ldrm
 *
 *   2. /dev/fb0 framebuffer  (-DGFX_USE_FB)
 *      Legacy CPU-rendering into mmap'd /dev/fb0.
 *      Supports triple-buffering (off-screen malloc buffers + memcpy).
 *      Build: g++ -O2 -std=c++14 -DGFX_USE_FB -o demo demo.cpp GFX.cpp
 *
 * Backwards-compatible alias: typedef LinuxGFX CircleGFX;
 */
class LinuxGFX {
public:

#ifdef GFX_USE_DRM_DUMB
    /**
     * DRM/KMS dumb-buffer back-end constructor.
     * @param drmdev  DRM/KMS device node (e.g. "/dev/dri/card0").
     *                Pass nullptr to use the default.
     */
    explicit LinuxGFX(const char *drmdev = "/dev/dri/card0");
#else
    /**
     * Framebuffer back-end constructor.
     * @param fbdev  Framebuffer device node (e.g. "/dev/fb0").
     */
    explicit LinuxGFX(const char *fbdev = "/dev/fb0");
#endif

    virtual ~LinuxGFX();

    // ===== CORE DRAW API =====================================================

    void drawPixel      (int16_t x, int16_t y, uint16_t color);
    void startWrite     (void);
    void endWrite       (void);
    void writePixel     (int16_t x, int16_t y, uint16_t color);
    void writeFillRect  (int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void writeFastVLine (int16_t x, int16_t y, int16_t h, uint16_t color);
    void writeFastHLine (int16_t x, int16_t y, int16_t w, uint16_t color);
    void writeLine      (int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);

    // ===== BASIC DRAW API ====================================================

    void drawFastVLine  (int16_t x, int16_t y, int16_t h, uint16_t color);
    void drawFastHLine  (int16_t x, int16_t y, int16_t w, uint16_t color);
    void drawLine       (int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
    void drawRect       (int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void fillRect       (int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void fillScreen     (uint16_t color);

    void drawCircle     (int16_t x0, int16_t y0, int16_t r, uint16_t color);
    void fillCircle     (int16_t x0, int16_t y0, int16_t r, uint16_t color);

    void drawRoundRect  (int16_t x, int16_t y, int16_t w, int16_t h, int16_t radius, uint16_t color);
    void fillRoundRect  (int16_t x, int16_t y, int16_t w, int16_t h, int16_t radius, uint16_t color);

    void drawTriangle   (int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                         int16_t x2, int16_t y2, uint16_t color);
    void fillTriangle   (int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                         int16_t x2, int16_t y2, uint16_t color);

    // ===== BITMAP DRAW API ===================================================

    void drawBitmap(int16_t x, int16_t y, const uint8_t bitmap[],
                    int16_t w, int16_t h, uint16_t color);
    void drawBitmap(int16_t x, int16_t y, const uint8_t bitmap[],
                    int16_t w, int16_t h, uint16_t color, uint16_t bg);
    void drawBitmap(int16_t x, int16_t y, uint8_t *bitmap,
                    int16_t w, int16_t h, uint16_t color);
    void drawBitmap(int16_t x, int16_t y, uint8_t *bitmap,
                    int16_t w, int16_t h, uint16_t color, uint16_t bg);

    void drawXBitmap(int16_t x, int16_t y, const uint8_t bitmap[],
                     int16_t w, int16_t h, uint16_t color);

    void drawGrayscaleBitmap(int16_t x, int16_t y, const uint8_t bitmap[], int16_t w, int16_t h);
    void drawGrayscaleBitmap(int16_t x, int16_t y, uint8_t *bitmap,        int16_t w, int16_t h);
    void drawGrayscaleBitmap(int16_t x, int16_t y, const uint8_t bitmap[],
                             const uint8_t mask[], int16_t w, int16_t h);
    void drawGrayscaleBitmap(int16_t x, int16_t y, uint8_t *bitmap,
                             uint8_t *mask, int16_t w, int16_t h);

    void drawRGBBitmap(int16_t x, int16_t y, const uint16_t bitmap[], int16_t w, int16_t h);
    void drawRGBBitmap(int16_t x, int16_t y, uint16_t *bitmap,        int16_t w, int16_t h);
    void drawRGBBitmap(int16_t x, int16_t y, const uint16_t bitmap[],
                       const uint8_t mask[], int16_t w, int16_t h);
    void drawRGBBitmap(int16_t x, int16_t y, uint16_t *bitmap,
                       uint8_t *mask, int16_t w, int16_t h);

    // ===== TEXT API ==========================================================

    void setCursor      (int16_t x, int16_t y);
    void setTextColor   (uint16_t c);
    void setTextColor   (uint16_t c, uint16_t bg);
    void setTextSize    (uint8_t s);
    void setTextSize    (uint8_t sx, uint8_t sy);
    void setTextWrap    (bool w);
    void drawChar       (int16_t x, int16_t y, unsigned char c,
                         uint16_t color, uint16_t bg, uint8_t size);
    void drawChar       (int16_t x, int16_t y, unsigned char c,
                         uint16_t color, uint16_t bg, uint8_t size_x, uint8_t size_y);
    void writeText      (const char *text);
    void setFont        (const GFXfont *f = nullptr);

    // ===== CONTROL API =======================================================

    void    setRotation  (uint8_t r);
    uint8_t getRotation  (void) const;
    void    invertDisplay(bool i);

    // ===== DIMENSION API =====================================================

    int16_t width()      const;
    int16_t height()     const;
    int16_t getCursorX() const;
    int16_t getCursorY() const;

    // ===== COLOR HELPERS =====================================================

    static uint16_t color565(uint8_t r, uint8_t g, uint8_t b);
    static uint16_t color565(uint32_t rgb);

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

    void      clearBuffer          (int8_t bufferIndex = -1, uint16_t color = 0);
    uint16_t* getBuffer            (uint8_t bufferIndex);

    bool      attachExternalBuffer (uint8_t bufferIndex, uint16_t *pBuffer);
    bool      detachExternalBuffer (uint8_t bufferIndex);
#endif

protected:

    void     setPixel (int16_t x, int16_t y, uint16_t color);
    uint16_t getPixel (int16_t x, int16_t y) const;

    void drawCircleHelper     (int16_t x0, int16_t y0, int16_t r,
                               uint8_t cornername, uint16_t color);
    void fillCircleHelper     (int16_t x0, int16_t y0, int16_t r,
                               uint8_t cornername, int16_t delta, uint16_t color);
    void drawFastVLineInternal(int16_t x, int16_t y, int16_t h, uint16_t color);
    void drawFastHLineInternal(int16_t x, int16_t y, int16_t w, uint16_t color);

    // ── Back-end specific members ────────────────────────────────────────────
#ifdef GFX_USE_DRM_DUMB

    /**
     * DRM dumb buffer descriptor.
     *
     * A dumb buffer is a simple linearly-tiled framebuffer allocated by the
     * DRM driver in GPU-accessible (often CMA / contiguous) memory.  The CPU
     * mmap's it for pixel writes; the display controller (CRTC) scans it out
     * directly — no separate copy to a shadow framebuffer needed.
     */
    struct DumbBuf {
        uint32_t  handle;   ///< GEM object handle returned by CREATE_DUMB
        uint32_t  pitch;    ///< Bytes per scanline (driver-aligned, may be > width*2)
        uint64_t  size;     ///< Total size in bytes
        uint32_t  fbId;     ///< KMS framebuffer ID (from drmModeAddFB)
        uint16_t *map;      ///< CPU-writable mmap pointer (RGB565 pixels)
    };

    int      m_drmFd;                    ///< Open file descriptor for /dev/dri/cardN
    uint32_t m_connId;                   ///< DRM connector ID
    uint32_t m_crtcId;                   ///< DRM CRTC ID
    uint32_t m_modeIdx;                  ///< Selected display mode index

    // Cached copy of the selected mode (drm_mode_modeinfo from kernel uAPI).
    // Stored so swapBuffers() never needs to re-query the connector.
    // drm_mode_modeinfo is a fixed 68-byte POD struct; we carry a raw copy.
    struct drm_mode_modeinfo m_selectedMode;

    static constexpr int kNumDrmBufs = 2;
    DumbBuf  m_drm[kNumDrmBufs];         ///< Double-buffer pair

    uint8_t  m_drawIdx;                  ///< Index of the buffer we're drawing into
    uint8_t  m_dispIdx;                  ///< Index of the buffer currently on screen
    bool     m_flipPending;              ///< Non-blocking page-flip in flight?
    uint16_t *m_pBuffer;                 ///< Alias → m_drm[m_drawIdx].map

    bool initDrm    (const char *drmdev);
    void cleanupDrm ();
    bool allocDumbBuf(DumbBuf &b);
    void freeDumbBuf (DumbBuf &b);

    // libdrm page-flip event handler (static so it matches the C callback sig)
    static void pageFlipHandler(int fd, unsigned int seq,
                                unsigned int tv_sec, unsigned int tv_usec,
                                void *data);

#else // ── /dev/fb0 Framebuffer back-end ──────────────────────────────────────

    int       m_fbFd;
    uint8_t  *m_pFbMem;
    size_t    m_fbMemSize;
    uint32_t  m_pitch;             ///< bytes per line
    uint32_t  m_depth;             ///< bits per pixel
    uint16_t *m_pBuffer;           ///< current draw buffer

    FrameBuffer m_buffers[3];
    uint8_t     m_bufferCount;
    uint8_t     m_drawBufferIndex;
    uint8_t     m_displayBufferIndex;
    bool        m_multiBufferEnabled;

    void _initializeMultiBuffer();
    void _cleanupMultiBuffer();
    void _flushToFb();

#endif // GFX_USE_DRM_DUMB

    // ── Common members ───────────────────────────────────────────────────────
    int16_t  m_width, m_height;
    int16_t  m_cursorX, m_cursorY;
    uint16_t m_textColor, m_textBgColor;
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
