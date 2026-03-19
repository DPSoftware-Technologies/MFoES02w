#ifndef GFX_H
#define GFX_H

#include <cstdint>
#include <cstdlib>
#include <cstring>

// ── Type aliases (replaces circle/types.h) ───────────────────────────────────
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
// Define GFX_USE_OPENGL_ES before including this header (or in your Makefile)
// to use the hardware-accelerated OpenGL ES 2.0 back-end via EGL/DRM.
// Without that define the raw /dev/fb0 framebuffer back-end is used.
// ─────────────────────────────────────────────────────────────────────────────

#ifdef GFX_USE_OPENGL_ES
#  include <EGL/egl.h>
#  include <GLES2/gl2.h>
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
    bool   bOwned;
    bool   bReady;
} FrameBuffer;

// ===== LinuxGFX ===============================================================
/**
 * Adafruit-GFX-compatible graphics library for Linux / Buildroot on RPi Zero 2W.
 *
 * Two back-ends, selected at compile time:
 *
 *   1. /dev/fb0 framebuffer (default)
 *      Pure CPU rendering into a mmap'd framebuffer.
 *      Supports triple-buffering (off-screen buffers + memcpy to fb).
 *      Constructor: LinuxGFX(const char *fbdev = "/dev/fb0")
 *
 *   2. OpenGL ES 2.0 via EGL/GBM  (define GFX_USE_OPENGL_ES)
 *      Uses the VideoCore / V3D GPU.  fillRect, fillScreen and
 *      drawRGBBitmap are GPU-accelerated; all other primitives fall
 *      back to CPU via 1×1 quads (fast enough for lines/text).
 *      Constructor: LinuxGFX(const char *drmdev = "/dev/dri/card0")
 *
 * The class was previously called CircleGFX; it is now LinuxGFX.
 * A typedef keeps old code working: typedef LinuxGFX CircleGFX;
 */
class LinuxGFX {
public:

#ifdef GFX_USE_OPENGL_ES
    /**
     * OpenGL ES 2.0 back-end constructor.
     * @param drmdev  DRM/KMS device node, e.g. "/dev/dri/card0".
     *                Pass nullptr to use the default.
     */
    explicit LinuxGFX(const char *drmdev = "/dev/dri/card0");
#else
    /**
     * Framebuffer back-end constructor.
     * @param fbdev  Framebuffer device node, e.g. "/dev/fb0".
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

    // ===== SWAP BUFFERS / PRESENT ============================================
    /**
     * Present the rendered frame.
     *
     *  - OpenGL ES back-end: calls eglSwapBuffers().
     *  - Framebuffer back-end with multi-buffering: memcpy to /dev/fb0.
     *  - Framebuffer back-end without multi-buffering: no-op (direct write).
     *
     * @param autoclear  (FB back-end only) Clear draw buffer after swap.
     */
#ifdef GFX_USE_OPENGL_ES
    void swapBuffers();
#else
    void swapBuffers(bool autoclear = true);

    // ===== MULTI-BUFFER API (Framebuffer back-end only) ======================

    bool  enableMultiBuffer    (uint8_t numBuffers = 2);
    bool  isMultiBuffered      () const;
    uint8_t  getBufferCount       () const;
    uint8_t  getDrawBufferIndex   () const;
    uint8_t  getDisplayBufferIndex() const;

    bool  selectDrawBuffer    (uint8_t bufferIndex);
    bool  selectDisplayBuffer (uint8_t bufferIndex);

    void     clearBuffer         (int8_t bufferIndex = -1, uint16_t color = 0);
    uint16_t* getBuffer          (uint8_t bufferIndex);

    bool  attachExternalBuffer(uint8_t bufferIndex, uint16_t *pBuffer);
    bool  detachExternalBuffer(uint8_t bufferIndex);
#endif

protected:

    void     setPixel (int16_t x, int16_t y, uint16_t color);
    uint16_t getPixel (int16_t x, int16_t y) const;

    void drawCircleHelper    (int16_t x0, int16_t y0, int16_t r,
                              uint8_t cornername, uint16_t color);
    void fillCircleHelper    (int16_t x0, int16_t y0, int16_t r,
                              uint8_t cornername, int16_t delta, uint16_t color);
    void drawFastVLineInternal(int16_t x, int16_t y, int16_t h, uint16_t color);
    void drawFastHLineInternal(int16_t x, int16_t y, int16_t w, uint16_t color);

    // ── Back-end specific members ────────────────────────────────────────────
#ifdef GFX_USE_OPENGL_ES

    // EGL / GBM handles
    int          m_drmFd;
    void        *m_pGbmDevice;   // gbm_device*   (opaque to avoid gbm.h here)
    void        *m_pGbmSurface;  // gbm_surface*
    EGLDisplay   m_eglDisplay;
    EGLContext   m_eglContext;
    EGLSurface   m_eglSurface;

    // GLSL programs
    GLuint m_shaderFlat;
    GLuint m_uFlatColor, m_uFlatMVP;
    GLuint m_vboQuad;

    GLuint m_shaderTex;
    GLuint m_uTexMVP, m_uTexSampler;

    GLuint  m_scratchTex;
    int16_t m_scratchW, m_scratchH;

    GLuint  compileShader  (GLenum type, const char *src);
    GLuint  linkProgram    (GLuint vs, GLuint fs);
    void    initGLResources();
    void    drawGLRect     (int16_t x, int16_t y, int16_t w, int16_t h,
                            float r, float g, float b, float a);
    void    uploadAndDrawTex(int16_t x, int16_t y, int16_t w, int16_t h,
                             const uint16_t *pixels);

    // DRM/KMS connector/CRTC state (minimal – only what's needed for EGL)
    uint32_t m_drmConnId;
    uint32_t m_drmCrtcId;
    uint32_t m_drmModeIdx;

    bool initDrmEgl(const char *drmdev);
    void cleanupDrmEgl();

#else // Framebuffer back-end

    int       m_fbFd;
    uint8_t  *m_pFbMem;
    size_t    m_fbMemSize;
    uint32_t  m_pitch;        // bytes per line
    uint32_t  m_depth;        // bits per pixel
    uint16_t *m_pBuffer;      // current draw buffer (may be off-screen)

    // Multi-buffer state
    FrameBuffer m_buffers[3];
    uint8_t     m_bufferCount;
    uint8_t     m_drawBufferIndex;
    uint8_t     m_displayBufferIndex;
    bool     m_multiBufferEnabled;

    void _initializeMultiBuffer();
    void _cleanupMultiBuffer();
    void _flushToFb();          // copy draw buffer → /dev/fb0

#endif

    // ── Common members ───────────────────────────────────────────────────────
    int16_t  m_width, m_height;
    int16_t  m_cursorX, m_cursorY;
    uint16_t m_textColor, m_textBgColor;
    uint8_t  m_textSizeX, m_textSizeY;
    bool  m_textWrap;
    uint8_t  m_rotation;
    bool  m_inverted;
    bool  m_inTransaction;

    const GFXfont *m_pFont;
    bool        m_fontSizeMultiplied;
};

// Backwards-compat alias
typedef LinuxGFX CircleGFX;

#endif // GFX_H
