#include "GFX.h"

// Belt-and-suspenders: define sentinels if an older header is in the path.
#ifndef GFX_TRANSPARENT
#  define GFX_TRANSPARENT 0x00000000u
#endif

#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <poll.h>
#include <cmath>
#include <algorithm>
#include <vector>

//  DRM / KMS DUMB-BUFFER BACK-END  —  zero libdrm dependency
#ifdef GFX_USE_DRM_DUMB

#include <drm/drm.h>
#include <drm/drm_mode.h>

static inline int drm_ioctl(int fd, unsigned long request, void *arg) {
    int ret;
    do { ret = ioctl(fd, request, arg); } while (ret == -1 && errno == EINTR);
    return ret;
}

#ifndef DRM_IOCTL_BASE
#  define DRM_IOCTL_BASE 'd'
#  define DRM_IO(nr)        _IO  (DRM_IOCTL_BASE, nr)
#  define DRM_IOR(nr,type)  _IOR (DRM_IOCTL_BASE, nr, type)
#  define DRM_IOW(nr,type)  _IOW (DRM_IOCTL_BASE, nr, type)
#  define DRM_IOWR(nr,type) _IOWR(DRM_IOCTL_BASE, nr, type)
#endif

#ifndef DRM_IOCTL_SET_CLIENT_CAP
#  define DRM_IOCTL_SET_CLIENT_CAP DRM_IOW(0x0d, struct drm_set_client_cap)
struct drm_set_client_cap { uint64_t capability; uint64_t value; };
#endif

#ifndef DRM_IOCTL_MODE_GETRESOURCES
#  define DRM_IOCTL_MODE_GETRESOURCES   DRM_IOWR(0xA0, struct drm_mode_card_res)
#  define DRM_IOCTL_MODE_GETCONNECTOR   DRM_IOWR(0xA7, struct drm_mode_get_connector)
#  define DRM_IOCTL_MODE_GETENCODER     DRM_IOWR(0xA6, struct drm_mode_get_encoder)
#  define DRM_IOCTL_MODE_GETCRTC        DRM_IOWR(0xA1, struct drm_mode_crtc)
#  define DRM_IOCTL_MODE_SETCRTC        DRM_IOWR(0xA2, struct drm_mode_crtc)
#  define DRM_IOCTL_MODE_PAGE_FLIP      DRM_IOWR(0xB0, struct drm_mode_crtc_page_flip)
#  define DRM_IOCTL_MODE_ADDFB          DRM_IOWR(0xAE, struct drm_mode_fb_cmd)
#  define DRM_IOCTL_MODE_ADDFB2         DRM_IOWR(0xB8, struct drm_mode_fb_cmd2)
#  define DRM_IOCTL_MODE_RMFB           DRM_IOWR(0xAF, unsigned int)
#  define DRM_IOCTL_MODE_CREATE_DUMB    DRM_IOWR(0xB2, struct drm_mode_create_dumb)
#  define DRM_IOCTL_MODE_MAP_DUMB       DRM_IOWR(0xB3, struct drm_mode_map_dumb)
#  define DRM_IOCTL_MODE_DESTROY_DUMB   DRM_IOWR(0xB4, struct drm_mode_destroy_dumb)
#endif

// DRM_FORMAT_XRGB8888 — little-endian: byte order in memory is B,G,R,X.
// Matches how we store uint32_t: 0x00RRGGBB with X byte ignored by display.
#ifndef DRM_FORMAT_XRGB8888
#  define DRM_FORMAT_XRGB8888 0x34325258u   // fourcc 'XR24'
#endif

// drm_mode_fb_cmd2 — used for ADDFB2 which supports fourcc pixel formats.
#ifndef DRM_MODE_FB_CMD2_DEFINED
#  define DRM_MODE_FB_CMD2_DEFINED
struct drm_mode_fb_cmd2 {
    uint32_t fb_id;
    uint32_t width, height;
    uint32_t pixel_format;
    uint32_t flags;
    uint32_t handles[4];
    uint32_t pitches[4];
    uint32_t offsets[4];
    uint64_t modifier[4];
};
#endif

#ifndef DRM_CLIENT_CAP_UNIVERSAL_PLANES
#  define DRM_CLIENT_CAP_UNIVERSAL_PLANES 2
#endif

#ifndef DRM_MODE_CONNECTED
#  define DRM_MODE_CONNECTED         1
#  define DRM_MODE_DISCONNECTED      2
#  define DRM_MODE_UNKNOWNCONNECTION 3
#endif

#ifndef DRM_MODE_PAGE_FLIP_EVENT
#  define DRM_MODE_PAGE_FLIP_EVENT 0x01
#endif

#ifndef DRM_EVENT_FLIP_COMPLETE
#  define DRM_EVENT_FLIP_COMPLETE 0x02
struct drm_event { uint32_t type; uint32_t length; };
struct drm_event_vblank {
    struct drm_event base;
    uint64_t user_data;
    uint32_t tv_sec;
    uint32_t tv_usec;
    uint32_t sequence;
    uint32_t crtc_id;
};
#endif


//  allocDumbBuf — 32 bpp XRGB8888

bool LinuxGFX::allocDumbBuf(DumbBuf &b) {
    // 1. CREATE_DUMB — allocate a 32 bpp linearly-tiled buffer.
    struct drm_mode_create_dumb creq = {};
    creq.width  = (uint32_t)m_width;
    creq.height = (uint32_t)m_height;
    creq.bpp    = 32;   // XRGB8888
    if (drm_ioctl(m_drmFd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
        fprintf(stderr, "GFX DRM: CREATE_DUMB failed: %s\n", strerror(errno));
        return false;
    }
    b.handle = creq.handle;
    b.pitch  = creq.pitch;
    b.size   = creq.size;

    // 2. ADDFB2 with DRM_FORMAT_XRGB8888.
    //    Fall back to legacy ADDFB (depth=24 bpp=32) if ADDFB2 is unavailable.
    struct drm_mode_fb_cmd2 fb2 = {};
    fb2.width        = (uint32_t)m_width;
    fb2.height       = (uint32_t)m_height;
    fb2.pixel_format = DRM_FORMAT_XRGB8888;
    fb2.handles[0]   = b.handle;
    fb2.pitches[0]   = b.pitch;

    if (drm_ioctl(m_drmFd, DRM_IOCTL_MODE_ADDFB2, &fb2) == 0) {
        b.fbId = fb2.fb_id;
    } else {
        // Fallback: legacy ADDFB with depth=24, bpp=32 (XRGB8888 compatible).
        struct drm_mode_fb_cmd fbreq = {};
        fbreq.width  = (uint32_t)m_width;
        fbreq.height = (uint32_t)m_height;
        fbreq.pitch  = b.pitch;
        fbreq.bpp    = 32;
        fbreq.depth  = 24;
        fbreq.handle = b.handle;
        if (drm_ioctl(m_drmFd, DRM_IOCTL_MODE_ADDFB, &fbreq) < 0) {
            fprintf(stderr, "GFX DRM: ADDFB/ADDFB2 both failed: %s\n", strerror(errno));
            struct drm_mode_destroy_dumb dreq = { b.handle };
            drm_ioctl(m_drmFd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
            return false;
        }
        b.fbId = fbreq.fb_id;
    }

    // 3. MAP_DUMB — get the mmap offset.
    struct drm_mode_map_dumb mreq = {};
    mreq.handle = b.handle;
    if (drm_ioctl(m_drmFd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0) {
        fprintf(stderr, "GFX DRM: MAP_DUMB failed: %s\n", strerror(errno));
        unsigned int fbid = b.fbId;
        drm_ioctl(m_drmFd, DRM_IOCTL_MODE_RMFB, &fbid);
        struct drm_mode_destroy_dumb dreq = { b.handle };
        drm_ioctl(m_drmFd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
        return false;
    }

    // 4. mmap — CPU-writable pointer into GPU-accessible memory.
    b.map = (uint32_t*)mmap(nullptr, (size_t)b.size,
                            PROT_READ | PROT_WRITE, MAP_SHARED,
                            m_drmFd, (off_t)mreq.offset);
    if (b.map == MAP_FAILED) {
        fprintf(stderr, "GFX DRM: mmap dumb buf failed: %s\n", strerror(errno));
        b.map = nullptr;
        unsigned int fbid = b.fbId;
        drm_ioctl(m_drmFd, DRM_IOCTL_MODE_RMFB, &fbid);
        struct drm_mode_destroy_dumb dreq = { b.handle };
        drm_ioctl(m_drmFd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
        return false;
    }

    memset(b.map, 0, (size_t)b.size);
    return true;
}


//  freeDumbBuf

void LinuxGFX::freeDumbBuf(DumbBuf &b) {
    if (b.map && b.map != MAP_FAILED) {
        munmap(b.map, (size_t)b.size);
        b.map = nullptr;
    }
    if (b.fbId) {
        unsigned int fbid = b.fbId;
        drm_ioctl(m_drmFd, DRM_IOCTL_MODE_RMFB, &fbid);
        b.fbId = 0;
    }
    if (b.handle) {
        struct drm_mode_destroy_dumb dreq = { b.handle };
        drm_ioctl(m_drmFd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
        b.handle = 0;
    }
}


//  initDrm

bool LinuxGFX::initDrm(const char *drmdev) {
    m_drmFd = open(drmdev, O_RDWR | O_CLOEXEC);
    if (m_drmFd < 0) {
        fprintf(stderr, "GFX DRM: cannot open %s: %s\n", drmdev, strerror(errno));
        return false;
    }

    struct drm_set_client_cap cap = { DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1 };
    drm_ioctl(m_drmFd, DRM_IOCTL_SET_CLIENT_CAP, &cap);

    struct drm_mode_card_res res = {};
    if (drm_ioctl(m_drmFd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) {
        fprintf(stderr, "GFX DRM: GETRESOURCES failed: %s\n", strerror(errno));
        return false;
    }
    if (res.count_connectors == 0 || res.count_crtcs == 0) {
        fprintf(stderr, "GFX DRM: no connectors or CRTCs\n");
        return false;
    }

    uint32_t conn_ids[16] = {}, crtc_ids[8] = {};
    res.connector_id_ptr  = (uint64_t)(uintptr_t)conn_ids;
    res.crtc_id_ptr       = (uint64_t)(uintptr_t)crtc_ids;
    res.count_connectors  = res.count_connectors < 16 ? res.count_connectors : 16;
    res.count_crtcs       = res.count_crtcs      < 8  ? res.count_crtcs      : 8;
    if (drm_ioctl(m_drmFd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) {
        fprintf(stderr, "GFX DRM: GETRESOURCES (2) failed: %s\n", strerror(errno));
        return false;
    }

    m_connId = 0; m_crtcId = 0; m_modeIdx = 0;
    memset(&m_selectedMode, 0, sizeof(m_selectedMode));

    for (uint32_t ci = 0; ci < res.count_connectors && !m_connId; ci++) {
        struct drm_mode_get_connector conn = {};
        conn.connector_id = conn_ids[ci];
        drm_ioctl(m_drmFd, DRM_IOCTL_MODE_GETCONNECTOR, &conn);
        if (conn.connection != DRM_MODE_CONNECTED || conn.count_modes == 0) continue;

        uint32_t enc_ids[8] = {};
        struct drm_mode_modeinfo modes[64] = {};
        conn.modes_ptr      = (uint64_t)(uintptr_t)modes;
        conn.encoders_ptr   = (uint64_t)(uintptr_t)enc_ids;
        conn.count_modes    = conn.count_modes    < 64 ? conn.count_modes    : 64;
        conn.count_encoders = conn.count_encoders < 8  ? conn.count_encoders : 8;
        if (drm_ioctl(m_drmFd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) < 0) continue;
        if (conn.count_modes == 0) continue;

        m_connId       = conn_ids[ci];
        m_selectedMode = modes[0];
        m_width        = (int16_t)modes[0].hdisplay;
        m_height       = (int16_t)modes[0].vdisplay;

        if (conn.encoder_id) {
            struct drm_mode_get_encoder enc = {};
            enc.encoder_id = conn.encoder_id;
            if (drm_ioctl(m_drmFd, DRM_IOCTL_MODE_GETENCODER, &enc) == 0)
                m_crtcId = enc.crtc_id;
        }
        if (!m_crtcId && res.count_crtcs > 0)
            m_crtcId = crtc_ids[0];
    }

    if (!m_connId) { fprintf(stderr, "GFX DRM: no connected display found\n"); return false; }
    if (!m_crtcId) { fprintf(stderr, "GFX DRM: could not find a CRTC\n");      return false; }

    for (int i = 0; i < kNumDrmBufs; i++) {
        if (!allocDumbBuf(m_drm[i])) {
            for (int j = 0; j < i; j++) freeDumbBuf(m_drm[j]);
            return false;
        }
    }

    m_dispIdx = 0; m_drawIdx = 1;

    struct drm_mode_crtc set = {};
    set.crtc_id              = m_crtcId;
    set.fb_id                = m_drm[m_dispIdx].fbId;
    set.set_connectors_ptr   = (uint64_t)(uintptr_t)&m_connId;
    set.count_connectors     = 1;
    set.mode                 = m_selectedMode;
    set.mode_valid           = 1;
    if (drm_ioctl(m_drmFd, DRM_IOCTL_MODE_SETCRTC, &set) < 0) {
        fprintf(stderr, "GFX DRM: SETCRTC failed: %s\n", strerror(errno));
        return false;
    }

    m_pBuffer    = m_drm[m_drawIdx].map;
    m_flipPending = false;

    fprintf(stderr, "GFX DRM: dumb-buffer init OK (%dx%d XRGB8888, pitch=%u)\n",
            m_width, m_height, m_drm[0].pitch);
    return true;
}


//  cleanupDrm

void LinuxGFX::cleanupDrm() {
    struct drm_mode_crtc dis = {}; dis.crtc_id = m_crtcId;
    drm_ioctl(m_drmFd, DRM_IOCTL_MODE_SETCRTC, &dis);
    for (int i = 0; i < kNumDrmBufs; i++) freeDumbBuf(m_drm[i]);
    if (m_drmFd >= 0) { close(m_drmFd); m_drmFd = -1; }
}


//  pageFlipHandler

void LinuxGFX::pageFlipHandler(int, unsigned int, unsigned int, unsigned int,
                                void *data) {
    LinuxGFX *self = static_cast<LinuxGFX *>(data);
    self->m_flipPending = false;
    self->m_dispIdx     = self->m_drawIdx;
}


//  Constructor / Destructor

LinuxGFX::LinuxGFX(const char *drmdev)
    : m_drmFd(-1),
      m_connId(0), m_crtcId(0), m_modeIdx(0),
      m_drawIdx(1), m_dispIdx(0),
      m_flipPending(false), m_pBuffer(nullptr),
      m_width(0), m_height(0),
      m_cursorX(0), m_cursorY(0),
      m_textColor(GFX_WHITE), m_textBgColor(GFX_TRANSPARENT),
      m_textSizeX(1), m_textSizeY(1),
      m_textWrap(true), m_rotation(0),
      m_inverted(false), m_inTransaction(false),
      m_pFont(nullptr), m_fontSizeMultiplied(true)
{
    for (int i = 0; i < kNumDrmBufs; i++)
        m_drm[i] = { 0, 0, 0, 0, nullptr };
    memset(&m_selectedMode, 0, sizeof(m_selectedMode));
    initDrm(drmdev ? drmdev : "/dev/dri/card0");
}

LinuxGFX::~LinuxGFX() {
    if (m_flipPending) waitForFlip();
    cleanupDrm();
}


//  Pixel access — XRGB8888: stride in uint32_t units = pitch/4

void LinuxGFX::setPixel(int16_t x, int16_t y, uint32_t color) {
    if (x < 0 || x >= m_width || y < 0 || y >= m_height || !m_pBuffer) return;
    m_pBuffer[(uint32_t)y * (m_drm[m_drawIdx].pitch / 4) + (uint32_t)x] = color;
}

uint32_t LinuxGFX::getPixel(int16_t x, int16_t y) const {
    if (x < 0 || x >= m_width || y < 0 || y >= m_height || !m_pBuffer) return 0;
    return m_pBuffer[(uint32_t)y * (m_drm[m_drawIdx].pitch / 4) + (uint32_t)x];
}


//  writeFillRect — fast inner loop, ARGB-aware

void LinuxGFX::writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t color) {
    if ((color >> 24) == 0) return;  // fully transparent — skip
    if (!m_pBuffer) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > m_width)  w = m_width  - x;
    if (y + h > m_height) h = m_height - y;
    if (w <= 0 || h <= 0) return;

    uint32_t stride = m_drm[m_drawIdx].pitch / 4;
    uint32_t *row = m_pBuffer + (uint32_t)y * stride + (uint32_t)x;
    uint32_t sa = (color >> 24) & 0xFF;

    if (sa == 0xFF) {
        // Fully opaque — fill with memset-equivalent word fill
        for (int16_t j = 0; j < h; j++, row += stride) {
            uint32_t *p = row;
            for (int16_t i = 0; i < w; i++) p[i] = color;
        }
    } else {
        // Semi-transparent — blend each pixel
        for (int16_t j = 0; j < h; j++, row += stride) {
            for (int16_t i = 0; i < w; i++)
                row[i] = blendARGB(color, row[i]);
        }
    }
}


//  fillScreen

void LinuxGFX::fillScreen(uint32_t color) {
    if ((color >> 24) == 0) return;   // fully transparent = no-op
    if (!m_pBuffer) return;
    uint32_t n = (uint32_t)m_width * (uint32_t)m_height;
    if (color == GFX_BLACK) {
        memset(m_pBuffer, 0, n * 4);
    } else if ((color >> 24) == 0xFF) {
        for (uint32_t i = 0; i < n; i++) m_pBuffer[i] = color;
    } else {
        // Semi-transparent fill over entire buffer
        for (uint32_t i = 0; i < n; i++)
            m_pBuffer[i] = blendARGB(color, m_pBuffer[i]);
    }
}


//  drawRGBBitmap — fast blit for ARGB8888 source bitmaps (DRM back-end)

void LinuxGFX::drawRGBBitmap(int16_t x, int16_t y, const uint32_t bitmap[],
                              int16_t w, int16_t h) {
    if (!m_pBuffer || !bitmap) return;
    int16_t x0 = x, y0 = y, x1 = x + w, y1 = y + h, bx = 0, by = 0;
    if (x0 < 0) { bx = -x0; x0 = 0; }
    if (y0 < 0) { by = -y0; y0 = 0; }
    if (x1 > m_width)  x1 = m_width;
    if (y1 > m_height) y1 = m_height;
    int16_t dw = x1 - x0, dh = y1 - y0;
    if (dw <= 0 || dh <= 0) return;

    uint32_t dstStride = m_drm[m_drawIdx].pitch / 4;
    uint32_t *dst      = m_pBuffer + (uint32_t)y0 * dstStride + (uint32_t)x0;
    const uint32_t *src = bitmap + (uint32_t)by * (uint32_t)w + (uint32_t)bx;

    for (int16_t j = 0; j < dh; j++, dst += dstStride, src += (uint32_t)w) {
        for (int16_t i = 0; i < dw; i++)
            dst[i] = blendARGB(src[i], dst[i]);
    }
}

void LinuxGFX::drawRGBBitmap(int16_t x, int16_t y, uint32_t *bitmap, int16_t w, int16_t h) {
    drawRGBBitmap(x, y, (const uint32_t*)bitmap, w, h);
}


//  swapBuffers

void LinuxGFX::swapBuffers(bool async, bool autoclear) {
    if (m_drmFd < 0 || !m_pBuffer) return;
    if (m_flipPending) waitForFlip();

    uint8_t nextDisp = m_drawIdx;
    uint8_t nextDraw = m_dispIdx;

    if (async) {
        struct drm_mode_crtc_page_flip flip = {};
        flip.crtc_id   = m_crtcId;
        flip.fb_id     = m_drm[nextDisp].fbId;
        flip.flags     = DRM_MODE_PAGE_FLIP_EVENT;
        flip.user_data = (uint64_t)(uintptr_t)this;

        if (drm_ioctl(m_drmFd, DRM_IOCTL_MODE_PAGE_FLIP, &flip) == 0) {
            m_flipPending = true;
        } else {
            fprintf(stderr, "GFX DRM: PAGE_FLIP failed (%s), falling back\n", strerror(errno));
            async = false;
        }
    }

    if (!async) {
        struct drm_mode_crtc set = {};
        set.crtc_id            = m_crtcId;
        set.fb_id              = m_drm[nextDisp].fbId;
        set.set_connectors_ptr = (uint64_t)(uintptr_t)&m_connId;
        set.count_connectors   = 1;
        set.mode               = m_selectedMode;
        set.mode_valid         = 1;
        drm_ioctl(m_drmFd, DRM_IOCTL_MODE_SETCRTC, &set);
        m_dispIdx = nextDisp;
    }

    m_drawIdx = nextDraw;
    m_pBuffer = m_drm[m_drawIdx].map;
    if (autoclear) memset(m_pBuffer, 0, (size_t)m_drm[m_drawIdx].size);
}


//  waitForFlip

void LinuxGFX::waitForFlip() {
    if (!m_flipPending || m_drmFd < 0) return;
    struct pollfd pfd = { m_drmFd, POLLIN, 0 };
    while (m_flipPending) {
        int r = poll(&pfd, 1, 1000);
        if (r < 0 && errno != EINTR) break;
        if (r <= 0) continue;
        uint8_t buf[1024];
        ssize_t len = read(m_drmFd, buf, sizeof(buf));
        for (ssize_t i = 0; i < len; ) {
            struct drm_event *ev = (struct drm_event*)(buf + i);
            if ((size_t)(len - i) < sizeof(*ev)) break;
            if (ev->type == DRM_EVENT_FLIP_COMPLETE) {
                struct drm_event_vblank *vbl = (struct drm_event_vblank*)(buf + i);
                LinuxGFX *self = (LinuxGFX*)(uintptr_t)vbl->user_data;
                if (self) { self->m_flipPending = false; self->m_dispIdx = self->m_drawIdx; }
            }
            i += (ssize_t)ev->length;
        }
    }
}

#else
//  /dev/fb0 FRAMEBUFFER BACK-END  (32 bpp ARGB8888)

#include <linux/fb.h>

LinuxGFX::LinuxGFX(const char *fbdev)
    : m_fbFd(-1), m_pFbMem(nullptr), m_fbMemSize(0),
      m_pitch(0), m_depth(0), m_pBuffer(nullptr),
      m_bufferCount(1), m_drawBufferIndex(0), m_displayBufferIndex(0),
      m_multiBufferEnabled(false),
      m_width(0), m_height(0),
      m_cursorX(0), m_cursorY(0),
      m_textColor(GFX_WHITE), m_textBgColor(GFX_TRANSPARENT),
      m_textSizeX(1), m_textSizeY(1),
      m_textWrap(true), m_rotation(0),
      m_inverted(false), m_inTransaction(false),
      m_pFont(nullptr), m_fontSizeMultiplied(true)
{
    const char *dev = fbdev ? fbdev : "/dev/fb0";
    m_fbFd = open(dev, O_RDWR);
    if (m_fbFd < 0) {
        fprintf(stderr, "GFX: cannot open %s: %s\n", dev, strerror(errno));
        return;
    }

    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    if (ioctl(m_fbFd, FBIOGET_FSCREENINFO, &finfo) ||
        ioctl(m_fbFd, FBIOGET_VSCREENINFO, &vinfo)) {
        fprintf(stderr, "GFX: ioctl FBIOGET failed\n");
        close(m_fbFd); m_fbFd = -1; return;
    }

    if (vinfo.bits_per_pixel != 32) {
        vinfo.bits_per_pixel = 32;
        ioctl(m_fbFd, FBIOPUT_VSCREENINFO, &vinfo);
        ioctl(m_fbFd, FBIOGET_FSCREENINFO, &finfo);
        ioctl(m_fbFd, FBIOGET_VSCREENINFO, &vinfo);
        if (vinfo.bits_per_pixel != 32) {
            fprintf(stderr, "GFX: framebuffer does not support 32 bpp\n");
            close(m_fbFd); m_fbFd = -1; return;
        }
    }

    m_width     = (int16_t)vinfo.xres;
    m_height    = (int16_t)vinfo.yres;
    m_pitch     = finfo.line_length;
    m_depth     = vinfo.bits_per_pixel;
    m_fbMemSize = (size_t)finfo.smem_len;

    m_pFbMem = (uint8_t*)mmap(nullptr, m_fbMemSize,
                               PROT_READ | PROT_WRITE, MAP_SHARED, m_fbFd, 0);
    if (m_pFbMem == MAP_FAILED) {
        fprintf(stderr, "GFX: mmap failed: %s\n", strerror(errno));
        m_pFbMem = nullptr; close(m_fbFd); m_fbFd = -1; return;
    }

    m_pBuffer = (uint32_t*)m_pFbMem;
    _initializeMultiBuffer();

    fprintf(stderr, "GFX: framebuffer %s OK (%dx%d @ %d bpp ARGB8888, pitch=%d)\n",
            dev, m_width, m_height, m_depth, m_pitch);
}

LinuxGFX::~LinuxGFX() {
    _cleanupMultiBuffer();
    if (m_pFbMem && m_pFbMem != MAP_FAILED) munmap(m_pFbMem, m_fbMemSize);
    if (m_fbFd >= 0) close(m_fbFd);
}

void LinuxGFX::setPixel(int16_t x, int16_t y, uint32_t color) {
    if (x < 0 || x >= m_width || y < 0 || y >= m_height || !m_pBuffer) return;
    m_pBuffer[(uint32_t)y * (m_pitch / 4) + (uint32_t)x] = color;
}

uint32_t LinuxGFX::getPixel(int16_t x, int16_t y) const {
    if (x < 0 || x >= m_width || y < 0 || y >= m_height || !m_pBuffer) return 0;
    return m_pBuffer[(uint32_t)y * (m_pitch / 4) + (uint32_t)x];
}

void LinuxGFX::writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t color) {
    if ((color >> 24) == 0) return;
    for (int16_t i = y; i < y + h; i++) writeFastHLine(x, i, w, color);
}

void LinuxGFX::fillScreen(uint32_t color) {
    if ((color >> 24) == 0) return;
    fillRect(0, 0, m_width, m_height, color);
}

void LinuxGFX::drawRGBBitmap(int16_t x, int16_t y, const uint32_t bitmap[],
                              int16_t w, int16_t h) {
    startWrite();
    for (int16_t j = 0; j < h; j++, y++)
        for (int16_t i = 0; i < w; i++)
            writePixel(x + i, y, bitmap[j * w + i]);
    endWrite();
}

void LinuxGFX::drawRGBBitmap(int16_t x, int16_t y, uint32_t *bitmap, int16_t w, int16_t h) {
    drawRGBBitmap(x, y, (const uint32_t*)bitmap, w, h);
}

void LinuxGFX::_flushToFb() {
    if (!m_pFbMem || !m_pBuffer) return;
    if ((uint8_t*)m_pBuffer == m_pFbMem) return;
    memcpy(m_pFbMem, m_pBuffer, (size_t)m_width * (size_t)m_height * 4);
}

void LinuxGFX::_initializeMultiBuffer() {
    for (uint8_t i = 0; i < 3; i++) {
        m_buffers[i].pData = nullptr;
        m_buffers[i].bOwned = false;
        m_buffers[i].bReady = false;
    }
    m_bufferCount = 1; m_drawBufferIndex = 0; m_displayBufferIndex = 0;
    m_multiBufferEnabled = false;
    m_buffers[0].pData = m_pBuffer; m_buffers[0].bOwned = false;
}

void LinuxGFX::_cleanupMultiBuffer() {
    for (uint8_t i = 0; i < 3; i++) {
        if (m_buffers[i].bOwned && m_buffers[i].pData) {
            free(m_buffers[i].pData);
            m_buffers[i].pData = nullptr;
        }
    }
    m_multiBufferEnabled = false;
}

bool LinuxGFX::enableMultiBuffer(uint8_t numBuffers) {
    if (numBuffers < 1 || numBuffers > 3) numBuffers = 2;
    size_t bufSize = (size_t)m_width * (size_t)m_height * sizeof(uint32_t);

    for (uint8_t i = 0; i < m_bufferCount; i++)
        if (m_buffers[i].bOwned && m_buffers[i].pData) {
            free(m_buffers[i].pData);
            m_buffers[i].pData = nullptr; m_buffers[i].bOwned = false;
        }

    m_bufferCount = numBuffers;
    for (uint8_t i = 0; i < m_bufferCount; i++) {
        m_buffers[i].pData = (uint32_t*)malloc(bufSize);
        if (!m_buffers[i].pData) {
            for (uint8_t j = 0; j < i; j++) { free(m_buffers[j].pData); m_buffers[j].pData = nullptr; }
            m_bufferCount = 1; m_buffers[0].pData = m_pBuffer; m_buffers[0].bOwned = false;
            m_multiBufferEnabled = false; return false;
        }
        m_buffers[i].bOwned = true; m_buffers[i].bReady = false;
        memset(m_buffers[i].pData, 0, bufSize);
    }
    m_drawBufferIndex = 0; m_displayBufferIndex = 0;
    m_multiBufferEnabled = true;
    m_pBuffer = m_buffers[0].pData;
    return true;
}

bool     LinuxGFX::isMultiBuffered()        const { return m_multiBufferEnabled; }
uint8_t  LinuxGFX::getBufferCount()         const { return m_bufferCount; }
uint8_t  LinuxGFX::getDrawBufferIndex()     const { return m_drawBufferIndex; }
uint8_t  LinuxGFX::getDisplayBufferIndex()  const { return m_displayBufferIndex; }

void LinuxGFX::swapBuffers(bool autoclear) {
    if (!m_multiBufferEnabled) { _flushToFb(); return; }
    m_buffers[m_drawBufferIndex].bReady = true;
    m_displayBufferIndex = m_drawBufferIndex;
    if (m_pFbMem)
        memcpy(m_pFbMem, m_buffers[m_displayBufferIndex].pData,
               (size_t)m_width * (size_t)m_height * 4);
    m_drawBufferIndex = (m_drawBufferIndex + 1) % m_bufferCount;
    if (autoclear)
        memset(m_buffers[m_drawBufferIndex].pData, 0,
               (size_t)m_width * (size_t)m_height * 4);
    m_pBuffer = m_buffers[m_drawBufferIndex].pData;
}

bool LinuxGFX::selectDrawBuffer(uint8_t idx) {
    if (!m_multiBufferEnabled || idx >= m_bufferCount) return false;
    m_drawBufferIndex = idx; m_pBuffer = m_buffers[idx].pData; return true;
}

bool LinuxGFX::selectDisplayBuffer(uint8_t idx) {
    if (!m_multiBufferEnabled || idx >= m_bufferCount) return false;
    m_displayBufferIndex = idx;
    if (m_pFbMem)
        memcpy(m_pFbMem, m_buffers[idx].pData, (size_t)m_width * (size_t)m_height * 4);
    return true;
}

void LinuxGFX::clearBuffer(int8_t idx, uint32_t color) {
    uint32_t n = (uint32_t)m_width * (uint32_t)m_height;
    auto clearOne = [&](uint8_t i) {
        if (!m_buffers[i].pData) return;
        if (color == GFX_BLACK) memset(m_buffers[i].pData, 0, n * 4);
        else for (uint32_t j = 0; j < n; j++) m_buffers[i].pData[j] = color;
    };
    if (idx == -1)       { for (uint8_t i = 0; i < m_bufferCount; i++) clearOne(i); }
    else if (idx == -2)  { clearOne(m_drawBufferIndex); }
    else if ((uint8_t)idx < m_bufferCount) clearOne((uint8_t)idx);
}

uint32_t* LinuxGFX::getBuffer(uint8_t idx) {
    return idx < m_bufferCount ? m_buffers[idx].pData : nullptr;
}

bool LinuxGFX::attachExternalBuffer(uint8_t idx, uint32_t *pBuf) {
    if (idx >= 3 || !pBuf) return false;
    if (m_buffers[idx].bOwned && m_buffers[idx].pData) free(m_buffers[idx].pData);
    m_buffers[idx].pData = pBuf; m_buffers[idx].bOwned = false; m_buffers[idx].bReady = false;
    if (idx >= m_bufferCount) m_bufferCount = idx + 1;
    return true;
}

bool LinuxGFX::detachExternalBuffer(uint8_t idx) {
    if (idx >= m_bufferCount) return false;
    if (!m_buffers[idx].bOwned) {
        m_buffers[idx].pData = nullptr; m_buffers[idx].bReady = false; return true;
    }
    return false;
}

#endif // GFX_USE_DRM_DUMB

//  COMMON CODE  (identical for both back-ends)

void LinuxGFX::startWrite(void) { m_inTransaction = true;  }
void LinuxGFX::endWrite  (void) { m_inTransaction = false; }

void LinuxGFX::drawPixel(int16_t x, int16_t y, uint32_t color) {
    startWrite(); writePixel(x, y, color); endWrite();
}

// writePixel — the universal pixel write entry point.
// Handles transparency check and alpha blending.
void LinuxGFX::writePixel(int16_t x, int16_t y, uint32_t color) {
    uint32_t alpha = color >> 24;
    if (alpha == 0) return;   // fully transparent — skip
    if (x < 0 || x >= m_width || y < 0 || y >= m_height) return;
    if (alpha == 0xFF) {
        setPixel(x, y, color);
    } else {
        setPixel(x, y, blendARGB(color, getPixel(x, y)));
    }
}

void LinuxGFX::writeFastVLine(int16_t x, int16_t y, int16_t h, uint32_t color) {
    if ((color >> 24) == 0) return;
    for (int16_t i = y; i < y + h; i++) writePixel(x, i, color);
}

void LinuxGFX::writeFastHLine(int16_t x, int16_t y, int16_t w, uint32_t color) {
    if ((color >> 24) == 0) return;
    if (y < 0 || y >= m_height) return;
    int16_t xs = std::max<int16_t>(0, x);
    int16_t xe = std::min<int16_t>(m_width, x + w);
    for (int16_t i = xs; i < xe; i++) writePixel(i, y, color);
}

void LinuxGFX::writeLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint32_t color) {
    int16_t dx = std::abs(x1 - x0), dy = std::abs(y1 - y0);
    int16_t sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int16_t err = dx - dy, x = x0, y = y0;
    while (true) {
        writePixel(x, y, color);
        if (x == x1 && y == y1) break;
        int16_t e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 <  dx) { err += dx; y += sy; }
    }
}

void LinuxGFX::drawFastVLine(int16_t x, int16_t y, int16_t h, uint32_t c) { startWrite(); writeFastVLine(x,y,h,c); endWrite(); }
void LinuxGFX::drawFastHLine(int16_t x, int16_t y, int16_t w, uint32_t c) { startWrite(); writeFastHLine(x,y,w,c); endWrite(); }
void LinuxGFX::drawLine(int16_t x0,int16_t y0,int16_t x1,int16_t y1,uint32_t c) { startWrite(); writeLine(x0,y0,x1,y1,c); endWrite(); }

void LinuxGFX::drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t c) {
    drawFastHLine(x, y,       w, c); drawFastHLine(x, y + h - 1, w, c);
    drawFastVLine(x, y,       h, c); drawFastVLine(x + w - 1, y, h, c);
}

void LinuxGFX::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t c) {
    startWrite(); writeFillRect(x, y, w, h, c); endWrite();
}

// Circles
void LinuxGFX::drawCircleHelper(int16_t x0, int16_t y0, int16_t r,
                                 uint8_t c, uint32_t color) {
    int16_t f = 1 - r, ddx = 1, ddy = -2 * r, x = 0, y = r;
    while (x < y) {
        if (f >= 0) { y--; ddy += 2; f += ddy; }
        x++; ddx += 2; f += ddx;
        if (c & 0x4) { writePixel(x0+x,y0+y,color); writePixel(x0+y,y0+x,color); }
        if (c & 0x2) { writePixel(x0+x,y0-y,color); writePixel(x0+y,y0-x,color); }
        if (c & 0x8) { writePixel(x0-y,y0+x,color); writePixel(x0-x,y0+y,color); }
        if (c & 0x1) { writePixel(x0-y,y0-x,color); writePixel(x0-x,y0-y,color); }
    }
}

void LinuxGFX::fillCircleHelper(int16_t x0, int16_t y0, int16_t r,
                                  uint8_t c, int16_t delta, uint32_t color) {
    int16_t f = 1 - r, ddx = 1, ddy = -2 * r, x = 0, y = r;
    while (x < y) {
        if (f >= 0) { y--; ddy += 2; f += ddy; }
        x++; ddx += 2; f += ddx;
        if (c & 0x1) {
            writeFastVLine(x0+x, y0-y, 2*y+1+delta, color);
            writeFastVLine(x0+y, y0-x, 2*x+1+delta, color);
        }
        if (c & 0x2) {
            writeFastVLine(x0-x, y0-y, 2*y+1+delta, color);
            writeFastVLine(x0-y, y0-x, 2*x+1+delta, color);
        }
    }
}

void LinuxGFX::drawCircle(int16_t x0, int16_t y0, int16_t r, uint32_t color) {
    startWrite();
    int16_t f = 1-r, ddx = 1, ddy = -2*r, x = 0, y = r;
    writePixel(x0,y0+r,color); writePixel(x0,y0-r,color);
    writePixel(x0+r,y0,color); writePixel(x0-r,y0,color);
    while (x < y) {
        if (f >= 0) { y--; ddy += 2; f += ddy; }
        x++; ddx += 2; f += ddx;
        writePixel(x0+x,y0+y,color); writePixel(x0-x,y0+y,color);
        writePixel(x0+x,y0-y,color); writePixel(x0-x,y0-y,color);
        writePixel(x0+y,y0+x,color); writePixel(x0-y,y0+x,color);
        writePixel(x0+y,y0-x,color); writePixel(x0-y,y0-x,color);
    }
    endWrite();
}

void LinuxGFX::fillCircle(int16_t x0, int16_t y0, int16_t r, uint32_t color) {
    startWrite();
    writeFastVLine(x0, y0-r, 2*r+1, color);
    fillCircleHelper(x0, y0, r, 3, 0, color);
    endWrite();
}

// Rounded rectangles
void LinuxGFX::drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h,
                               int16_t r, uint32_t color) {
    startWrite();
    int16_t mx = ((w<h)?w:h)/2; if(r>mx) r=mx;
    writeFastHLine(x+r, y,       w-2*r, color);
    writeFastHLine(x+r, y+h-1,   w-2*r, color);
    writeFastVLine(x,     y+r,   h-2*r, color);
    writeFastVLine(x+w-1, y+r,   h-2*r, color);
    drawCircleHelper(x+r,     y+r,     r, 1, color);
    drawCircleHelper(x+w-r-1, y+r,     r, 2, color);
    drawCircleHelper(x+w-r-1, y+h-r-1, r, 4, color);
    drawCircleHelper(x+r,     y+h-r-1, r, 8, color);
    endWrite();
}

void LinuxGFX::fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h,
                               int16_t r, uint32_t color) {
    startWrite();
    int16_t mx = ((w<h)?w:h)/2; if(r>mx) r=mx;
    writeFillRect(x+r, y, w-2*r, h, color);
    fillCircleHelper(x+w-r-1, y+r, r, 1, h-2*r-1, color);
    fillCircleHelper(x+r,     y+r, r, 2, h-2*r-1, color);
    endWrite();
}

// Triangles
void LinuxGFX::drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                             int16_t x2, int16_t y2, uint32_t color) {
    drawLine(x0,y0,x1,y1,color); drawLine(x1,y1,x2,y2,color); drawLine(x2,y2,x0,y0,color);
}

void LinuxGFX::fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                             int16_t x2, int16_t y2, uint32_t color) {
    if (y0>y1){std::swap(y0,y1);std::swap(x0,x1);}
    if (y1>y2){std::swap(y1,y2);std::swap(x1,x2);}
    if (y0>y1){std::swap(y0,y1);std::swap(x0,x1);}
    if (y0==y2) {
        int16_t a=std::min(x0,std::min(x1,x2)), b=std::max(x0,std::max(x1,x2));
        drawFastHLine(a,y0,b-a+1,color); return;
    }
    startWrite();
    int16_t dx01=x1-x0,dy01=y1-y0,dx02=x2-x0,dy02=y2-y0,dx12=x2-x1,dy12=y2-y1;
    int32_t sa=0,sb=0;
    int16_t last=(y1==y2)?y1:y1-1;
    for (int16_t y=y0;y<=last;y++) {
        int16_t a=x0+sa/dy01,b=x0+sb/dy02; sa+=dx01; sb+=dx02;
        if (a>b) std::swap(a,b);
        writeFastHLine(a,y,b-a+1,color);
    }
    sa=0; sb=(int32_t)dx02*(y1-y0);
    for (int16_t y=y1;y<=y2;y++) {
        int16_t a=x1+sa/dy12,b=x0+sb/dy02; sa+=dx12; sb+=dx02;
        if (a>b) std::swap(a,b);
        writeFastHLine(a,y,b-a+1,color);
    }
    endWrite();
}

// Bitmaps
void LinuxGFX::drawBitmap(int16_t x, int16_t y, const uint8_t bitmap[],
                           int16_t w, int16_t h, uint32_t color) {
    int16_t bw=(w+7)/8; uint8_t b=0;
    startWrite();
    for (int16_t j=0;j<h;j++,y++)
        for (int16_t i=0;i<w;i++) {
            if (i&7) b<<=1; else b=bitmap[j*bw+i/8];
            if (b&0x80) writePixel(x+i,y,color);
        }
    endWrite();
}

void LinuxGFX::drawBitmap(int16_t x, int16_t y, const uint8_t bitmap[],
                           int16_t w, int16_t h, uint32_t color, uint32_t bg) {
    int16_t bw=(w+7)/8; uint8_t b=0;
    startWrite();
    for (int16_t j=0;j<h;j++,y++)
        for (int16_t i=0;i<w;i++) {
            if (i&7) b<<=1; else b=bitmap[j*bw+i/8];
            uint32_t px=(b&0x80)?color:bg;
            if ((px>>24)!=0) writePixel(x+i,y,px);
        }
    endWrite();
}

void LinuxGFX::drawBitmap(int16_t x,int16_t y,uint8_t *bitmap,int16_t w,int16_t h,uint32_t color)
{ drawBitmap(x,y,(const uint8_t*)bitmap,w,h,color); }
void LinuxGFX::drawBitmap(int16_t x,int16_t y,uint8_t *bitmap,int16_t w,int16_t h,uint32_t color,uint32_t bg)
{ drawBitmap(x,y,(const uint8_t*)bitmap,w,h,color,bg); }

void LinuxGFX::drawXBitmap(int16_t x, int16_t y, const uint8_t bitmap[],
                             int16_t w, int16_t h, uint32_t color) {
    int16_t bw=(w+7)/8; uint8_t b=0;
    startWrite();
    for (int16_t j=0;j<h;j++,y++)
        for (int16_t i=0;i<w;i++) {
            if (i&7) b>>=1; else b=bitmap[j*bw+i/8];
            if (b&0x01) writePixel(x+i,y,color);
        }
    endWrite();
}

void LinuxGFX::drawGrayscaleBitmap(int16_t x, int16_t y,
                                    const uint8_t bitmap[], int16_t w, int16_t h) {
    startWrite();
    for (int16_t j=0;j<h;j++,y++)
        for (int16_t i=0;i<w;i++) {
            uint8_t v = bitmap[j*w+i];
            writePixel(x+i, y, 0xFF000000u | ((uint32_t)v<<16) | ((uint32_t)v<<8) | v);
        }
    endWrite();
}
void LinuxGFX::drawGrayscaleBitmap(int16_t x,int16_t y,uint8_t *bitmap,int16_t w,int16_t h)
{ drawGrayscaleBitmap(x,y,(const uint8_t*)bitmap,w,h); }

void LinuxGFX::drawGrayscaleBitmap(int16_t x, int16_t y,
                                    const uint8_t bitmap[], const uint8_t mask[],
                                    int16_t w, int16_t h) {
    int16_t bw=(w+7)/8; uint8_t b=0;
    startWrite();
    for (int16_t j=0;j<h;j++,y++)
        for (int16_t i=0;i<w;i++) {
            if (i&7) b<<=1; else b=mask[j*bw+i/8];
            if (b&0x80) {
                uint8_t v=bitmap[j*w+i];
                writePixel(x+i,y, 0xFF000000u|((uint32_t)v<<16)|((uint32_t)v<<8)|v);
            }
        }
    endWrite();
}
void LinuxGFX::drawGrayscaleBitmap(int16_t x,int16_t y,uint8_t *bitmap,uint8_t *mask,int16_t w,int16_t h)
{ drawGrayscaleBitmap(x,y,(const uint8_t*)bitmap,(const uint8_t*)mask,w,h); }

void LinuxGFX::drawRGBBitmap(int16_t x, int16_t y,
                              const uint32_t bitmap[], const uint8_t mask[],
                              int16_t w, int16_t h) {
    int16_t bw=(w+7)/8; uint8_t b=0;
    startWrite();
    for (int16_t j=0;j<h;j++,y++)
        for (int16_t i=0;i<w;i++) {
            if (i&7) b<<=1; else b=mask[j*bw+i/8];
            if (b&0x80) writePixel(x+i,y,bitmap[j*w+i]);
        }
    endWrite();
}
void LinuxGFX::drawRGBBitmap(int16_t x,int16_t y,uint32_t *bitmap,uint8_t *mask,int16_t w,int16_t h)
{ drawRGBBitmap(x,y,(const uint32_t*)bitmap,(const uint8_t*)mask,w,h); }

void LinuxGFX::drawRGB565Bitmap(int16_t x, int16_t y, const uint16_t *bitmap, int16_t w, int16_t h) {
    if (!m_pBuffer || !bitmap) return;
    int16_t x0 = x, y0 = y, x1 = x+w, y1 = y+h, bx = 0, by = 0;
    if (x0 < 0) { bx = -x0; x0 = 0; }
    if (y0 < 0) { by = -y0; y0 = 0; }
    if (x1 > m_width)  x1 = m_width;
    if (y1 > m_height) y1 = m_height;
    int16_t dw = x1-x0, dh = y1-y0;
    if (dw <= 0 || dh <= 0) return;

#ifdef GFX_USE_DRM_DUMB
    uint32_t dstStride = m_drm[m_drawIdx].pitch / 4;
#else
    uint32_t dstStride = m_pitch / 4;
#endif
    uint32_t       *dst = m_pBuffer + (uint32_t)y0 * dstStride + (uint32_t)x0;
    const uint16_t *src = bitmap    + (uint32_t)by * (uint32_t)w + (uint32_t)bx;

    for (int16_t j = 0; j < dh; j++, dst += dstStride, src += w) {
        for (int16_t i = 0; i < dw; i++) {
            uint16_t c = src[i];
            uint8_t r = ((c >> 11) & 0x1F); r = (r << 3) | (r >> 2);
            uint8_t g = ((c >>  5) & 0x3F); g = (g << 2) | (g >> 4);
            uint8_t b = ( c        & 0x1F); b = (b << 3) | (b >> 2);
            dst[i] = 0xFF000000u | ((uint32_t)r<<16) | ((uint32_t)g<<8) | b;
        }
    }
}

//  Default 5×8 font
static const uint8_t s_font[] = {
    0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x5F,0x00,0x00, 0x00,0x07,0x00,0x07,0x00,
    0x14,0x7F,0x14,0x7F,0x14, 0x24,0x2A,0x7F,0x2A,0x12, 0x23,0x13,0x08,0x64,0x62,
    0x36,0x49,0x55,0x22,0x50, 0x00,0x05,0x03,0x00,0x00, 0x00,0x1C,0x22,0x41,0x00,
    0x00,0x41,0x22,0x1C,0x00, 0x14,0x08,0x3E,0x08,0x14, 0x08,0x08,0x3E,0x08,0x08,
    0x00,0x50,0x30,0x00,0x00, 0x08,0x08,0x08,0x08,0x08, 0x00,0x60,0x60,0x00,0x00,
    0x20,0x10,0x08,0x04,0x02, 0x3E,0x51,0x49,0x45,0x3E, 0x00,0x42,0x7F,0x40,0x00,
    0x42,0x61,0x51,0x49,0x46, 0x21,0x41,0x45,0x4B,0x31, 0x18,0x14,0x12,0x7F,0x10,
    0x27,0x45,0x45,0x45,0x39, 0x3C,0x4A,0x49,0x49,0x30, 0x01,0x71,0x09,0x05,0x03,
    0x36,0x49,0x49,0x49,0x36, 0x06,0x49,0x49,0x29,0x1E, 0x00,0x36,0x36,0x00,0x00,
    0x00,0x56,0x36,0x00,0x00, 0x08,0x14,0x22,0x41,0x00, 0x14,0x14,0x14,0x14,0x14,
    0x00,0x41,0x22,0x14,0x08, 0x02,0x01,0x51,0x09,0x06, 0x32,0x49,0x79,0x41,0x3E,
    0x7E,0x11,0x11,0x11,0x7E, 0x7F,0x49,0x49,0x49,0x36, 0x3E,0x41,0x41,0x41,0x22,
    0x7F,0x41,0x41,0x22,0x1C, 0x7F,0x49,0x49,0x49,0x41, 0x7F,0x09,0x09,0x09,0x01,
    0x3E,0x41,0x49,0x49,0x7A, 0x7F,0x08,0x08,0x08,0x7F, 0x00,0x41,0x7F,0x41,0x00,
    0x20,0x40,0x41,0x3F,0x01, 0x7F,0x08,0x14,0x22,0x41, 0x7F,0x40,0x40,0x40,0x40,
    0x7F,0x02,0x0C,0x02,0x7F, 0x7F,0x04,0x08,0x10,0x7F, 0x3E,0x41,0x41,0x41,0x3E,
    0x7F,0x09,0x09,0x09,0x06, 0x3E,0x41,0x51,0x21,0x5E, 0x7F,0x09,0x19,0x29,0x46,
    0x46,0x49,0x49,0x49,0x31, 0x01,0x01,0x7F,0x01,0x01, 0x3F,0x40,0x40,0x40,0x3F,
    0x1F,0x20,0x40,0x20,0x1F, 0x3F,0x40,0x38,0x40,0x3F, 0x63,0x14,0x08,0x14,0x63,
    0x07,0x08,0x70,0x08,0x07, 0x61,0x51,0x49,0x45,0x43, 0x00,0x7F,0x41,0x41,0x00,
    0x02,0x04,0x08,0x10,0x20, 0x00,0x41,0x41,0x7F,0x00, 0x04,0x02,0x01,0x02,0x04,
    0x40,0x40,0x40,0x40,0x40, 0x00,0x01,0x02,0x04,0x00, 0x20,0x54,0x54,0x54,0x78,
    0x7F,0x48,0x44,0x44,0x38, 0x38,0x44,0x44,0x44,0x20, 0x38,0x44,0x44,0x48,0x7F,
    0x38,0x54,0x54,0x54,0x18, 0x08,0x7E,0x09,0x01,0x02, 0x0C,0x52,0x52,0x52,0x3E,
    0x7F,0x08,0x04,0x04,0x78, 0x00,0x44,0x7D,0x40,0x00, 0x20,0x40,0x44,0x3D,0x00,
    0x7F,0x10,0x28,0x44,0x00, 0x00,0x41,0x7F,0x40,0x00, 0x7C,0x04,0x18,0x04,0x78,
    0x7C,0x08,0x04,0x04,0x78, 0x38,0x44,0x44,0x44,0x38, 0x7C,0x14,0x14,0x14,0x08,
    0x08,0x14,0x14,0x18,0x7C, 0x7C,0x08,0x04,0x04,0x08, 0x48,0x54,0x54,0x54,0x20,
    0x04,0x3F,0x44,0x40,0x20, 0x3C,0x40,0x40,0x20,0x7C, 0x1C,0x20,0x40,0x20,0x1C,
    0x3C,0x40,0x30,0x40,0x3C, 0x44,0x28,0x10,0x28,0x44, 0x0C,0x50,0x50,0x50,0x3C,
    0x44,0x64,0x54,0x4C,0x44, 0x00,0x08,0x36,0x41,0x00, 0x00,0x00,0x7F,0x00,0x00,
    0x00,0x41,0x36,0x08,0x00, 0x10,0x08,0x08,0x10,0x08, 0x78,0x46,0x41,0x46,0x78,
};

//  Text rendering
void LinuxGFX::drawChar(int16_t x, int16_t y, unsigned char c,
                         uint32_t color, uint32_t bg, uint8_t sx, uint8_t sy) {
    bool transBg = ((bg >> 24) == 0);

    if (!m_pFont) {
        if (x >= m_width || y >= m_height || (x + 6*sx - 1) < 0 || (y + 8*sy - 1) < 0) return;
        if (c < 32 || c > 126) c = '?';
        const uint8_t *glyph = s_font + (c - 32) * 5;
        startWrite();
        for (int8_t col = 0; col < 5; col++) {
            uint8_t bits = glyph[col];
            for (int8_t row = 0; row < 8; row++, bits >>= 1) {
                if (bits & 1) {
                    if (sx==1&&sy==1) writePixel(x+col, y+row, color);
                    else writeFillRect(x+col*sx, y+row*sy, sx, sy, color);
                } else if (!transBg) {
                    if (sx==1&&sy==1) writePixel(x+col, y+row, bg);
                    else writeFillRect(x+col*sx, y+row*sy, sx, sy, bg);
                }
            }
        }
        if (!transBg) {
            for (int8_t row = 0; row < 8; row++) {
                if (sx==1&&sy==1) writePixel(x+5, y+row, bg);
                else writeFillRect(x+5*sx, y+row*sy, sx, sy, bg);
            }
        }
        endWrite();
    } else {
        if (c < m_pFont->first || c > m_pFont->last) return;
        uint8_t ci = c - m_pFont->first;
        const GFXglyph *gl   = &m_pFont->glyph[ci];
        const uint8_t  *bits = m_pFont->bitmap + gl->bitmapOffset;
        int16_t gx = x + gl->xOffset, gy = y + gl->yOffset;
        int16_t gw = gl->width, gh = gl->height;
        uint8_t bit = 0, bits8 = 0;
        startWrite();
        for (int16_t gy2 = 0; gy2 < gh; gy2++)
            for (int16_t gx2 = 0; gx2 < gw; gx2++) {
                if (!(bit++ & 7)) bits8 = *bits++;
                if (bits8 & 0x80) {
                    if (sx==1&&sy==1) writePixel(gx+gx2, gy+gy2, color);
                    else writeFillRect(gx+gx2*sx, gy+gy2*sy, sx, sy, color);
                }
                bits8 <<= 1;
            }
        endWrite();
    }
}

void LinuxGFX::drawChar(int16_t x, int16_t y, unsigned char c,
                         uint32_t color, uint32_t bg, uint8_t size) {
    drawChar(x, y, c, color, bg, size, size);
}

void LinuxGFX::writeText(const char *text) {
    while (*text) {
        unsigned char c = *text++;
        if (c == '\n') {
            m_cursorX = 0;
            m_cursorY += m_textSizeY * (m_pFont ? m_pFont->yAdvance : 8);
        } else if (c != '\r') {
            int16_t adv = m_pFont ? m_pFont->glyph[c - m_pFont->first].xAdvance : 6;
            if (m_textWrap && (m_cursorX + m_textSizeX * adv > m_width)) {
                m_cursorX = 0;
                m_cursorY += m_textSizeY * (m_pFont ? m_pFont->yAdvance : 8);
            }
            drawChar(m_cursorX, m_cursorY, c, m_textColor, m_textBgColor,
                     m_textSizeX, m_textSizeY);
            m_cursorX += m_textSizeX * adv;
        }
    }
}

void LinuxGFX::writeTextF(const char *fmt, ...) {
    va_list args, args_copy;
    va_start(args, fmt);
    va_copy(args_copy, args);
    int size = vsnprintf(nullptr, 0, fmt, args);
    if (size < 0) { va_end(args); va_end(args_copy); return; }
    std::vector<char> buf(size + 1);
    vsnprintf(buf.data(), buf.size(), fmt, args_copy);
    va_end(args); va_end(args_copy);
    writeText(buf.data());
}

void LinuxGFX::setFont     (const GFXfont *f)         { m_pFont = f; }
void LinuxGFX::setCursor   (int16_t x, int16_t y)     { m_cursorX = x; m_cursorY = y; }
void LinuxGFX::setTextColor(uint32_t c)                { m_textColor = c; m_textBgColor = GFX_TRANSPARENT; }
void LinuxGFX::setTextColor(uint32_t c, uint32_t bg)   { m_textColor = c; m_textBgColor = bg; }
void LinuxGFX::setTextColorTransparentBg(uint32_t c)   { m_textColor = c; m_textBgColor = GFX_TRANSPARENT; }
void LinuxGFX::setTextSize (uint8_t s)                 { m_textSizeX = m_textSizeY = s ? s : 1; }
void LinuxGFX::setTextSize (uint8_t sx, uint8_t sy)    { m_textSizeX = sx?sx:1; m_textSizeY = sy?sy:1; }
void LinuxGFX::setTextWrap (bool w)                    { m_textWrap = w; }

// Control & dimensions
void LinuxGFX::setRotation(uint8_t r) {
    m_rotation = r % 4;
    if (m_rotation == 1 || m_rotation == 3) std::swap(m_width, m_height);
}
uint8_t  LinuxGFX::getRotation()  const { return m_rotation; }
void     LinuxGFX::invertDisplay(bool i) { m_inverted = i; }
int16_t  LinuxGFX::width()        const { return m_width; }
int16_t  LinuxGFX::height()       const { return m_height; }
int16_t  LinuxGFX::getCursorX()   const { return m_cursorX; }
int16_t  LinuxGFX::getCursorY()   const { return m_cursorY; }

void LinuxGFX::drawFastVLineInternal(int16_t x, int16_t y, int16_t h, uint32_t c) { writeFastVLine(x,y,h,c); }
void LinuxGFX::drawFastHLineInternal(int16_t x, int16_t y, int16_t w, uint32_t c) { writeFastHLine(x,y,w,c); }

//  Color helpers

// Backwards-compatible: color565(r,g,b) now returns full-precision opaque ARGB8888.
// The name "color565" is kept so old code compiles without changes.
uint32_t LinuxGFX::color565(uint8_t r, uint8_t g, uint8_t b) {
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

uint32_t LinuxGFX::color565(uint32_t rgb) {
    // Accept 0xRRGGBB — return opaque ARGB8888
    return 0xFF000000u | (rgb & 0x00FFFFFFu);
}

uint32_t LinuxGFX::color888(uint8_t r, uint8_t g, uint8_t b) {
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

uint32_t LinuxGFX::colorRGB(uint8_t r, uint8_t g, uint8_t b) {
    return color888(r, g, b);
}

uint32_t LinuxGFX::colorARGB(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

uint32_t LinuxGFX::fromRGB565(uint16_t c) {
    uint8_t r = ((c >> 11) & 0x1F) << 3;   // 5-bit → 8-bit
    uint8_t g = ((c >>  5) & 0x3F) << 2;   // 6-bit → 8-bit
    uint8_t b = ( c        & 0x1F) << 3;   // 5-bit → 8-bit
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}