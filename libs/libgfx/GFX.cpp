// GFX.cpp – LinuxGFX implementation for Buildroot / bare Linux on RPi Zero 2W
//
// Two back-ends:
//   • /dev/fb0  (default, CPU rendering + optional off-screen multi-buffering)
//   • OpenGL ES 2.0 via EGL + GBM/DRM (define GFX_USE_OPENGL_ES)
//
// Build examples:
//   # Framebuffer back-end:
//   g++ -O2 -std=c++14 -o demo demo.cpp GFX.cpp
//
//   # OpenGL ES 2.0 back-end (needs mesa/vc4 or v3d driver + libgbm):
//   g++ -O2 -std=c++14 -DGFX_USE_OPENGL_ES -o demo demo.cpp GFX.cpp \
//       -lEGL -lGLESv2 -lgbm -ldrm

#include "GFX.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

// ─────────────────────────────────────────────────────────────────────────────
//  OPENGL ES 2.0 / EGL / GBM / DRM BACK-END
// ─────────────────────────────────────────────────────────────────────────────
#ifdef GFX_USE_OPENGL_ES

#include <gbm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

// ─── GL error checking ───────────────────────────────────────────────────────
static void checkGLError(const char *op) {
    GLenum err = glGetError();
    if (err != GL_NO_ERROR)
        fprintf(stderr, "GL error in %s: 0x%04X\n", op, err);
}

// ─── GLSL shaders ────────────────────────────────────────────────────────────
static const char *s_flatVS =
    "attribute vec2 aPos;\n"
    "uniform mat4 uMVP;\n"
    "void main() { gl_Position = uMVP * vec4(aPos, 0.0, 1.0); }\n";

static const char *s_flatFS =
    "precision mediump float;\n"
    "uniform vec4 uColor;\n"
    "void main() { gl_FragColor = uColor; }\n";

static const char *s_texVS =
    "attribute vec2 aPos;\n"
    "attribute vec2 aUV;\n"
    "uniform mat4 uMVP;\n"
    "varying vec2 vUV;\n"
    "void main() { vUV = aUV; gl_Position = uMVP * vec4(aPos, 0.0, 1.0); }\n";

static const char *s_texFS =
    "precision mediump float;\n"
    "uniform sampler2D uTex;\n"
    "varying vec2 vUV;\n"
    "void main() { gl_FragColor = texture2D(uTex, vUV); }\n";

// ─── Orthographic projection (column-major, pixel-space → NDC) ───────────────
static void buildOrtho(float *m, float w, float h) {
    m[ 0]=2.f/w; m[ 1]=0;      m[ 2]=0; m[ 3]=0;
    m[ 4]=0;     m[ 5]=-2.f/h; m[ 6]=0; m[ 7]=0;
    m[ 8]=0;     m[ 9]=0;      m[10]=1; m[11]=0;
    m[12]=-1.f;  m[13]=1.f;    m[14]=0; m[15]=1;
}

static void rgb565ToFloat(uint16_t color, float &r, float &g, float &b) {
    r = ((color >> 11) & 0x1F) / 31.f;
    g = ((color >>  5) & 0x3F) / 63.f;
    b =  (color        & 0x1F) / 31.f;
}

// ─── DRM/EGL initialisation ──────────────────────────────────────────────────
bool LinuxGFX::initDrmEgl(const char *drmdev) {
    // 1. Open DRM device
    m_drmFd = open(drmdev, O_RDWR | O_CLOEXEC);
    if (m_drmFd < 0) {
        fprintf(stderr, "GFX: cannot open DRM device %s: %s\n", drmdev, strerror(errno));
        return false;
    }

    // 2. Find a connected connector + preferred mode
    drmModeRes *res = drmModeGetResources(m_drmFd);
    if (!res) { fprintf(stderr, "GFX: drmModeGetResources failed\n"); return false; }

    drmModeConnector *conn = nullptr;
    for (int i = 0; i < res->count_connectors && !conn; i++) {
        drmModeConnector *c = drmModeGetConnector(m_drmFd, res->connectors[i]);
        if (c && c->connection == DRM_MODE_CONNECTED && c->count_modes > 0) {
            conn = c;
            m_drmConnId = c->connector_id;
        } else {
            drmModeFreeConnector(c);
        }
    }
    if (!conn) { fprintf(stderr, "GFX: no connected DRM connector\n"); drmModeFreeResources(res); return false; }

    // Pick first (preferred) mode
    m_drmModeIdx = 0;
    m_width  = (int16_t)conn->modes[0].hdisplay;
    m_height = (int16_t)conn->modes[0].vdisplay;

    // Find CRTC for this encoder/connector
    drmModeEncoder *enc = drmModeGetEncoder(m_drmFd, conn->encoder_id);
    m_drmCrtcId = enc ? enc->crtc_id : res->crtcs[0];
    if (enc) drmModeFreeEncoder(enc);
    drmModeFreeConnector(conn);
    drmModeFreeResources(res);

    // 3. Create GBM device + surface
    gbm_device  *gbmDev = gbm_create_device(m_drmFd);
    gbm_surface *gbmSurf = gbm_surface_create(
        gbmDev, (uint32_t)m_width, (uint32_t)m_height,
        GBM_FORMAT_XRGB8888,
        GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!gbmSurf) { fprintf(stderr, "GFX: gbm_surface_create failed\n"); return false; }

    m_pGbmDevice  = gbmDev;
    m_pGbmSurface = gbmSurf;

    // 4. EGL setup
    m_eglDisplay = eglGetDisplay((EGLNativeDisplayType)gbmDev);
    eglInitialize(m_eglDisplay, nullptr, nullptr);
    eglBindAPI(EGL_OPENGL_ES_API);

    static const EGLint configAttribs[] = {
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };
    EGLConfig eglCfg;
    EGLint    numCfg = 0;
    eglChooseConfig(m_eglDisplay, configAttribs, &eglCfg, 1, &numCfg);
    if (numCfg == 0) { fprintf(stderr, "GFX: eglChooseConfig failed\n"); return false; }

    static const EGLint ctxAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    m_eglContext = eglCreateContext(m_eglDisplay, eglCfg, EGL_NO_CONTEXT, ctxAttribs);
    m_eglSurface = eglCreateWindowSurface(m_eglDisplay, eglCfg, (EGLNativeWindowType)gbmSurf, nullptr);

    if (!eglMakeCurrent(m_eglDisplay, m_eglSurface, m_eglSurface, m_eglContext)) {
        fprintf(stderr, "GFX: eglMakeCurrent failed\n");
        return false;
    }

    fprintf(stderr, "GFX: OpenGL ES 2.0 init OK (%dx%d)\n", m_width, m_height);
    return true;
}

void LinuxGFX::cleanupDrmEgl() {
    if (m_eglDisplay != EGL_NO_DISPLAY) {
        eglMakeCurrent(m_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (m_eglSurface != EGL_NO_SURFACE) eglDestroySurface(m_eglDisplay, m_eglSurface);
        if (m_eglContext != EGL_NO_CONTEXT)  eglDestroyContext(m_eglDisplay, m_eglContext);
        eglTerminate(m_eglDisplay);
    }
    if (m_pGbmSurface) gbm_surface_destroy((gbm_surface*)m_pGbmSurface);
    if (m_pGbmDevice)  gbm_device_destroy ((gbm_device*) m_pGbmDevice);
    if (m_drmFd >= 0)  close(m_drmFd);
}

// ─── Constructor / Destructor ────────────────────────────────────────────────
LinuxGFX::LinuxGFX(const char *drmdev)
    : m_drmFd(-1), m_pGbmDevice(nullptr), m_pGbmSurface(nullptr),
      m_eglDisplay(EGL_NO_DISPLAY), m_eglContext(EGL_NO_CONTEXT), m_eglSurface(EGL_NO_SURFACE),
      m_shaderFlat(0), m_uFlatColor(0), m_uFlatMVP(0), m_vboQuad(0),
      m_shaderTex(0),  m_uTexMVP(0),    m_uTexSampler(0),
      m_scratchTex(0), m_scratchW(0),   m_scratchH(0),
      m_drmConnId(0),  m_drmCrtcId(0),  m_drmModeIdx(0),
      m_width(0), m_height(0),
      m_cursorX(0), m_cursorY(0),
      m_textColor(0xFFFF), m_textBgColor(0x0000),
      m_textSizeX(1), m_textSizeY(1),
      m_textWrap(true), m_rotation(0),
      m_inverted(false), m_inTransaction(false),
      m_pFont(nullptr), m_fontSizeMultiplied(true)
{
    if (!initDrmEgl(drmdev ? drmdev : "/dev/dri/card0"))
        return;

    glViewport(0, 0, m_width, m_height);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    initGLResources();
}

LinuxGFX::~LinuxGFX() {
    if (m_scratchTex) glDeleteTextures(1, &m_scratchTex);
    if (m_vboQuad)    glDeleteBuffers(1, &m_vboQuad);
    if (m_shaderFlat) glDeleteProgram(m_shaderFlat);
    if (m_shaderTex)  glDeleteProgram(m_shaderTex);
    cleanupDrmEgl();
}

// ─── GL resource init ─────────────────────────────────────────────────────────
GLuint LinuxGFX::compileShader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0; glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        if (len > 1) {
            char *log = (char*)malloc(len);
            glGetShaderInfoLog(s, len, nullptr, log);
            fprintf(stderr, "GFX shader error: %s\n", log);
            free(log);
        }
        glDeleteShader(s); return 0;
    }
    return s;
}

GLuint LinuxGFX::linkProgram(GLuint vs, GLuint fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs); glAttachShader(p, fs);
    glBindAttribLocation(p, 0, "aPos");
    glBindAttribLocation(p, 1, "aUV");
    glLinkProgram(p);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0; glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
        if (len > 1) {
            char *log = (char*)malloc(len);
            glGetProgramInfoLog(p, len, nullptr, log);
            fprintf(stderr, "GFX link error: %s\n", log);
            free(log);
        }
        glDeleteProgram(p); glDeleteShader(vs); glDeleteShader(fs); return 0;
    }
    glDeleteShader(vs); glDeleteShader(fs);
    return p;
}

void LinuxGFX::initGLResources() {
    m_shaderFlat = linkProgram(compileShader(GL_VERTEX_SHADER, s_flatVS),
                               compileShader(GL_FRAGMENT_SHADER, s_flatFS));
    m_uFlatColor = glGetUniformLocation(m_shaderFlat, "uColor");
    m_uFlatMVP   = glGetUniformLocation(m_shaderFlat, "uMVP");

    m_shaderTex   = linkProgram(compileShader(GL_VERTEX_SHADER, s_texVS),
                                compileShader(GL_FRAGMENT_SHADER, s_texFS));
    m_uTexMVP     = glGetUniformLocation(m_shaderTex, "uMVP");
    m_uTexSampler = glGetUniformLocation(m_shaderTex, "uTex");

    static const float kQuad[] = {
        0.f,0.f, 0.f,0.f,
        1.f,0.f, 1.f,0.f,
        0.f,1.f, 0.f,1.f,
        1.f,1.f, 1.f,1.f,
    };
    glGenBuffers(1, &m_vboQuad);
    glBindBuffer(GL_ARRAY_BUFFER, m_vboQuad);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuad), kQuad, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// ─── GPU draw helpers ────────────────────────────────────────────────────────
void LinuxGFX::drawGLRect(int16_t x, int16_t y, int16_t w, int16_t h,
                           float r, float g, float b, float a) {
    if (w <= 0 || h <= 0 || !m_shaderFlat) return;

    float ortho[16]; buildOrtho(ortho, (float)m_width, (float)m_height);
    float model[16] = { (float)w,0,0,0, 0,(float)h,0,0, 0,0,1,0, (float)x,(float)y,0,1 };
    float mvp[16];
    for (int col=0;col<4;col++)
        for (int row=0;row<4;row++) {
            mvp[col*4+row]=0;
            for (int k=0;k<4;k++) mvp[col*4+row]+=ortho[k*4+row]*model[col*4+k];
        }

    glUseProgram(m_shaderFlat);
    glUniformMatrix4fv(m_uFlatMVP, 1, GL_FALSE, mvp);
    glUniform4f(m_uFlatColor, r, g, b, a);
    glBindBuffer(GL_ARRAY_BUFFER, m_vboQuad);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    checkGLError("drawGLRect");
}

void LinuxGFX::uploadAndDrawTex(int16_t x, int16_t y, int16_t w, int16_t h,
                                 const uint16_t *pixels) {
    if (!pixels || w<=0 || h<=0 || !m_shaderTex) return;

    if (!m_scratchTex || m_scratchW!=w || m_scratchH!=h) {
        if (m_scratchTex) glDeleteTextures(1, &m_scratchTex);
        glGenTextures(1, &m_scratchTex);
        m_scratchW=w; m_scratchH=h;
    }
    glBindTexture(GL_TEXTURE_2D, m_scratchTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, pixels);

    float ortho[16]; buildOrtho(ortho, (float)m_width, (float)m_height);
    float model[16] = { (float)w,0,0,0, 0,(float)h,0,0, 0,0,1,0, (float)x,(float)y,0,1 };
    float mvp[16];
    for (int col=0;col<4;col++)
        for (int row=0;row<4;row++) {
            mvp[col*4+row]=0;
            for (int k=0;k<4;k++) mvp[col*4+row]+=ortho[k*4+row]*model[col*4+k];
        }

    glUseProgram(m_shaderTex);
    glUniformMatrix4fv(m_uTexMVP, 1, GL_FALSE, mvp);
    glUniform1i(m_uTexSampler, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_scratchTex);
    glBindBuffer(GL_ARRAY_BUFFER, m_vboQuad);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(0); glDisableVertexAttribArray(1);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    checkGLError("uploadAndDrawTex");
}

// ─── Back-end specific API ───────────────────────────────────────────────────

void LinuxGFX::swapBuffers() {
    eglSwapBuffers(m_eglDisplay, m_eglSurface);
}

void LinuxGFX::setPixel(int16_t x, int16_t y, uint16_t color) {
    if (x<0||x>=m_width||y<0||y>=m_height) return;
    float r,g,b; rgb565ToFloat(color,r,g,b);
    drawGLRect(x,y,1,1,r,g,b,1.f);
}

uint16_t LinuxGFX::getPixel(int16_t x, int16_t y) const {
    (void)x;(void)y; return 0; // readback is slow; stub
}

void LinuxGFX::writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (x<0){w+=x;x=0;} if(y<0){h+=y;y=0;}
    if (x+w>m_width) w=m_width-x;
    if (y+h>m_height)h=m_height-y;
    if (w<=0||h<=0) return;
    float r,g,b; rgb565ToFloat(color,r,g,b);
    drawGLRect(x,y,w,h,r,g,b,1.f);
}

void LinuxGFX::fillScreen(uint16_t color) {
    float r,g,b; rgb565ToFloat(color,r,g,b);
    glClearColor(r,g,b,1.f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void LinuxGFX::drawRGBBitmap(int16_t x, int16_t y, const uint16_t bitmap[], int16_t w, int16_t h)
{ uploadAndDrawTex(x,y,w,h,bitmap); }

void LinuxGFX::drawRGBBitmap(int16_t x, int16_t y, uint16_t *bitmap, int16_t w, int16_t h)
{ uploadAndDrawTex(x,y,w,h,bitmap); }

#else
// ═════════════════════════════════════════════════════════════════════════════
//  /dev/fb0 FRAMEBUFFER BACK-END
// ═════════════════════════════════════════════════════════════════════════════

#include <linux/fb.h>

LinuxGFX::LinuxGFX(const char *fbdev)
    : m_fbFd(-1), m_pFbMem(nullptr), m_fbMemSize(0),
      m_pitch(0), m_depth(0), m_pBuffer(nullptr),
      m_bufferCount(1), m_drawBufferIndex(0), m_displayBufferIndex(0),
      m_multiBufferEnabled(false),
      m_width(0), m_height(0),
      m_cursorX(0), m_cursorY(0),
      m_textColor(0xFFFF), m_textBgColor(0x0000),
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
        close(m_fbFd); m_fbFd=-1; return;
    }

    // Request 16-bit colour if not already set
    if (vinfo.bits_per_pixel != 16) {
        vinfo.bits_per_pixel = 16;
        ioctl(m_fbFd, FBIOPUT_VSCREENINFO, &vinfo);
        ioctl(m_fbFd, FBIOGET_FSCREENINFO, &finfo);
        ioctl(m_fbFd, FBIOGET_VSCREENINFO, &vinfo);
    }

    m_width  = (int16_t)vinfo.xres;
    m_height = (int16_t)vinfo.yres;
    m_pitch  = finfo.line_length;
    m_depth  = vinfo.bits_per_pixel;

    m_fbMemSize = (size_t)finfo.smem_len;
    m_pFbMem    = (uint8_t*)mmap(nullptr, m_fbMemSize,
                                  PROT_READ | PROT_WRITE, MAP_SHARED, m_fbFd, 0);
    if (m_pFbMem == MAP_FAILED) {
        fprintf(stderr, "GFX: mmap failed: %s\n", strerror(errno));
        m_pFbMem = nullptr; close(m_fbFd); m_fbFd=-1; return;
    }

    m_pBuffer = (uint16_t*)m_pFbMem;   // direct write to fb by default
    _initializeMultiBuffer();

    fprintf(stderr, "GFX: framebuffer %s OK (%dx%d @ %d bpp, pitch=%d)\n",
            dev, m_width, m_height, m_depth, m_pitch);
}

LinuxGFX::~LinuxGFX() {
    _cleanupMultiBuffer();
    if (m_pFbMem && m_pFbMem != MAP_FAILED)
        munmap(m_pFbMem, m_fbMemSize);
    if (m_fbFd >= 0) close(m_fbFd);
}

// ─── Direct pixel access ──────────────────────────────────────────────────────
void LinuxGFX::setPixel(int16_t x, int16_t y, uint16_t color) {
    if (x<0||x>=m_width||y<0||y>=m_height||!m_pBuffer) return;
    m_pBuffer[y * (m_pitch/2) + x] = color;
}

uint16_t LinuxGFX::getPixel(int16_t x, int16_t y) const {
    if (x<0||x>=m_width||y<0||y>=m_height||!m_pBuffer) return 0;
    return m_pBuffer[y * (m_pitch/2) + x];
}

void LinuxGFX::writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    for (int16_t i=y; i<y+h; i++) writeFastHLine(x, i, w, color);
}

void LinuxGFX::fillScreen(uint16_t color) {
    fillRect(0, 0, m_width, m_height, color);
}

void LinuxGFX::drawRGBBitmap(int16_t x, int16_t y, const uint16_t bitmap[], int16_t w, int16_t h) {
    startWrite();
    for (int16_t j=0;j<h;j++,y++)
        for (int16_t i=0;i<w;i++)
            writePixel(x+i, y, bitmap[j*w+i]);
    endWrite();
}

void LinuxGFX::drawRGBBitmap(int16_t x, int16_t y, uint16_t *bitmap, int16_t w, int16_t h) {
    startWrite();
    for (int16_t j=0;j<h;j++,y++)
        for (int16_t i=0;i<w;i++)
            writePixel(x+i, y, bitmap[j*w+i]);
    endWrite();
}

// ─── Flush off-screen buffer → /dev/fb0 ──────────────────────────────────────
void LinuxGFX::_flushToFb() {
    if (!m_pFbMem || !m_pBuffer) return;
    // If draw buffer is already the fb, nothing to do
    if ((uint8_t*)m_pBuffer == m_pFbMem) return;
    memcpy(m_pFbMem, m_pBuffer, (size_t)m_width * (size_t)m_height * 2);
}

// ─── Multi-buffer ─────────────────────────────────────────────────────────────
void LinuxGFX::_initializeMultiBuffer() {
    for (uint8_t i=0;i<3;i++) {
        m_buffers[i].pData=nullptr; m_buffers[i].bOwned=false; m_buffers[i].bReady=false;
    }
    m_bufferCount=1; m_drawBufferIndex=0; m_displayBufferIndex=0; m_multiBufferEnabled=false;
    m_buffers[0].pData = m_pBuffer; m_buffers[0].bOwned=false;
}

void LinuxGFX::_cleanupMultiBuffer() {
    for (uint8_t i=0;i<3;i++) {
        if (m_buffers[i].bOwned && m_buffers[i].pData) {
            free(m_buffers[i].pData); m_buffers[i].pData=nullptr;
        }
    }
    m_multiBufferEnabled=false;
}

bool LinuxGFX::enableMultiBuffer(uint8_t numBuffers) {
    if (numBuffers<1||numBuffers>3) numBuffers=2;
    size_t bufSize = (size_t)m_width * (size_t)m_height * sizeof(uint16_t);

    for (uint8_t i=0;i<m_bufferCount;i++)
        if (m_buffers[i].bOwned && m_buffers[i].pData) {
            free(m_buffers[i].pData); m_buffers[i].pData=nullptr; m_buffers[i].bOwned=false;
        }

    m_bufferCount = numBuffers;
    for (uint8_t i=0;i<m_bufferCount;i++) {
        m_buffers[i].pData = (uint16_t*)malloc(bufSize);
        if (!m_buffers[i].pData) {
            for (uint8_t j=0;j<i;j++) { free(m_buffers[j].pData); m_buffers[j].pData=nullptr; }
            m_bufferCount=1; m_buffers[0].pData=m_pBuffer; m_buffers[0].bOwned=false;
            m_multiBufferEnabled=false; return false;
        }
        m_buffers[i].bOwned=true; m_buffers[i].bReady=false;
        memset(m_buffers[i].pData, 0, bufSize);
    }
    m_drawBufferIndex=0; m_displayBufferIndex=0;
    m_multiBufferEnabled=true;
    m_pBuffer = m_buffers[0].pData;
    return true;
}

bool LinuxGFX::isMultiBuffered()        const { return m_multiBufferEnabled; }
uint8_t LinuxGFX::getBufferCount()         const { return m_bufferCount; }
uint8_t LinuxGFX::getDrawBufferIndex()     const { return m_drawBufferIndex; }
uint8_t LinuxGFX::getDisplayBufferIndex()  const { return m_displayBufferIndex; }

void LinuxGFX::swapBuffers(bool autoclear) {
    if (!m_multiBufferEnabled) { _flushToFb(); return; }

    m_buffers[m_drawBufferIndex].bReady = true;
    m_displayBufferIndex = m_drawBufferIndex;

    // Present: memcpy to /dev/fb0
    if (m_pFbMem)
        memcpy(m_pFbMem, m_buffers[m_displayBufferIndex].pData,
               (size_t)m_width * (size_t)m_height * 2);

    m_drawBufferIndex = (m_drawBufferIndex + 1) % m_bufferCount;
    if (autoclear)
        memset(m_buffers[m_drawBufferIndex].pData, 0,
               (size_t)m_width * (size_t)m_height * 2);
    m_pBuffer = m_buffers[m_drawBufferIndex].pData;
}

bool LinuxGFX::selectDrawBuffer(uint8_t idx) {
    if (!m_multiBufferEnabled || idx>=m_bufferCount) return false;
    m_drawBufferIndex=idx; m_pBuffer=m_buffers[idx].pData; return true;
}

bool LinuxGFX::selectDisplayBuffer(uint8_t idx) {
    if (!m_multiBufferEnabled || idx>=m_bufferCount) return false;
    m_displayBufferIndex=idx;
    if (m_pFbMem)
        memcpy(m_pFbMem, m_buffers[idx].pData, (size_t)m_width*(size_t)m_height*2);
    return true;
}

void LinuxGFX::clearBuffer(int8_t idx, uint16_t color) {
    uint32_t n = (uint32_t)m_width * (uint32_t)m_height;
    auto clearOne = [&](uint8_t i) {
        if (!m_buffers[i].pData) return;
        if (color==0) { memset(m_buffers[i].pData, 0, n*2); }
        else { for (uint32_t j=0;j<n;j++) m_buffers[i].pData[j]=color; }
    };
    if (idx==-1)     { for (uint8_t i=0;i<m_bufferCount;i++) clearOne(i); }
    else if (idx==-2){ clearOne(m_drawBufferIndex); }
    else if ((uint8_t)idx<m_bufferCount) clearOne((uint8_t)idx);
}

uint16_t* LinuxGFX::getBuffer(uint8_t idx) {
    return idx<m_bufferCount ? m_buffers[idx].pData : nullptr;
}

bool LinuxGFX::attachExternalBuffer(uint8_t idx, uint16_t *pBuf) {
    if (idx>=3 || !pBuf) return false;
    if (m_buffers[idx].bOwned && m_buffers[idx].pData) free(m_buffers[idx].pData);
    m_buffers[idx].pData=pBuf; m_buffers[idx].bOwned=false; m_buffers[idx].bReady=false;
    if (idx>=m_bufferCount) m_bufferCount=idx+1;
    return true;
}

bool LinuxGFX::detachExternalBuffer(uint8_t idx) {
    if (idx>=m_bufferCount) return false;
    if (!m_buffers[idx].bOwned) { m_buffers[idx].pData=nullptr; m_buffers[idx].bReady=false; return true; }
    return false;
}

#endif // GFX_USE_OPENGL_ES

// ═════════════════════════════════════════════════════════════════════════════
//  COMMON CODE  (same for both back-ends)
// ═════════════════════════════════════════════════════════════════════════════

void LinuxGFX::startWrite(void) { m_inTransaction=true;  }
void LinuxGFX::endWrite  (void) { m_inTransaction=false; }

void LinuxGFX::drawPixel(int16_t x, int16_t y, uint16_t color) {
    startWrite(); writePixel(x,y,color); endWrite();
}

void LinuxGFX::writePixel(int16_t x, int16_t y, uint16_t color) {
    if (x<0||x>=m_width||y<0||y>=m_height) return;
    setPixel(x,y,color);
}

void LinuxGFX::writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
    for (int16_t i=y;i<y+h;i++) writePixel(x,i,color);
}

void LinuxGFX::writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
    if (y<0||y>=m_height) return;
    int16_t xs=MAX(0,x), xe=MIN((int16_t)m_width,(int16_t)(x+w));
    for (int16_t i=xs;i<xe;i++) writePixel(i,y,color);
}

void LinuxGFX::writeLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
    int16_t dx=ABS(x1-x0), dy=ABS(y1-y0);
    int16_t sx=x0<x1?1:-1, sy=y0<y1?1:-1;
    int16_t err=dx-dy, x=x0, y=y0;
    while (true) {
        writePixel(x,y,color);
        if (x==x1&&y==y1) break;
        int16_t e2=2*err;
        if (e2>-dy){err-=dy;x+=sx;}
        if (e2< dx){err+=dx;y+=sy;}
    }
}

void LinuxGFX::drawFastVLine(int16_t x,int16_t y,int16_t h,uint16_t c){startWrite();writeFastVLine(x,y,h,c);endWrite();}
void LinuxGFX::drawFastHLine(int16_t x,int16_t y,int16_t w,uint16_t c){startWrite();writeFastHLine(x,y,w,c);endWrite();}
void LinuxGFX::drawLine(int16_t x0,int16_t y0,int16_t x1,int16_t y1,uint16_t c){startWrite();writeLine(x0,y0,x1,y1,c);endWrite();}

void LinuxGFX::drawRect(int16_t x,int16_t y,int16_t w,int16_t h,uint16_t c) {
    drawFastHLine(x,y,    w,c); drawFastHLine(x,y+h-1,w,c);
    drawFastVLine(x,y,    h,c); drawFastVLine(x+w-1,y,h,c);
}

void LinuxGFX::fillRect(int16_t x,int16_t y,int16_t w,int16_t h,uint16_t c){
    startWrite();writeFillRect(x,y,w,h,c);endWrite();
}

// ─── Circles ──────────────────────────────────────────────────────────────────
void LinuxGFX::drawCircleHelper(int16_t x0,int16_t y0,int16_t r,uint8_t c,uint16_t color){
    int16_t f=1-r,ddx=1,ddy=-2*r,x=0,y=r;
    while(x<y){
        if(f>=0){y--;ddy+=2;f+=ddy;}
        x++;ddx+=2;f+=ddx;
        if(c&0x4){writePixel(x0+x,y0+y,color);writePixel(x0+y,y0+x,color);}
        if(c&0x2){writePixel(x0+x,y0-y,color);writePixel(x0+y,y0-x,color);}
        if(c&0x8){writePixel(x0-y,y0+x,color);writePixel(x0-x,y0+y,color);}
        if(c&0x1){writePixel(x0-y,y0-x,color);writePixel(x0-x,y0-y,color);}
    }
}

void LinuxGFX::fillCircleHelper(int16_t x0,int16_t y0,int16_t r,uint8_t c,int16_t delta,uint16_t color){
    int16_t f=1-r,ddx=1,ddy=-2*r,x=0,y=r;
    while(x<y){
        if(f>=0){y--;ddy+=2;f+=ddy;}
        x++;ddx+=2;f+=ddx;
        if(c&0x1){writeFastVLine(x0+x,y0-y,2*y+1+delta,color);writeFastVLine(x0+y,y0-x,2*x+1+delta,color);}
        if(c&0x2){writeFastVLine(x0-x,y0-y,2*y+1+delta,color);writeFastVLine(x0-y,y0-x,2*x+1+delta,color);}
    }
}

void LinuxGFX::drawCircle(int16_t x0,int16_t y0,int16_t r,uint16_t color){
    startWrite();
    int16_t f=1-r,ddx=1,ddy=-2*r,x=0,y=r;
    writePixel(x0,y0+r,color);writePixel(x0,y0-r,color);
    writePixel(x0+r,y0,color);writePixel(x0-r,y0,color);
    while(x<y){
        if(f>=0){y--;ddy+=2;f+=ddy;}
        x++;ddx+=2;f+=ddx;
        writePixel(x0+x,y0+y,color);writePixel(x0-x,y0+y,color);
        writePixel(x0+x,y0-y,color);writePixel(x0-x,y0-y,color);
        writePixel(x0+y,y0+x,color);writePixel(x0-y,y0+x,color);
        writePixel(x0+y,y0-x,color);writePixel(x0-y,y0-x,color);
    }
    endWrite();
}

void LinuxGFX::fillCircle(int16_t x0,int16_t y0,int16_t r,uint16_t color){
    startWrite();writeFastVLine(x0,y0-r,2*r+1,color);fillCircleHelper(x0,y0,r,3,0,color);endWrite();
}

// ─── Rounded rectangles ───────────────────────────────────────────────────────
void LinuxGFX::drawRoundRect(int16_t x,int16_t y,int16_t w,int16_t h,int16_t r,uint16_t color){
    startWrite();
    int16_t mx=((w<h)?w:h)/2; if(r>mx)r=mx;
    writeFastHLine(x+r,y,    w-2*r,color); writeFastHLine(x+r,y+h-1,w-2*r,color);
    writeFastVLine(x,  y+r,  h-2*r,color); writeFastVLine(x+w-1,y+r,h-2*r,color);
    drawCircleHelper(x+r,    y+r,    r,1,color);
    drawCircleHelper(x+w-r-1,y+r,    r,2,color);
    drawCircleHelper(x+w-r-1,y+h-r-1,r,4,color);
    drawCircleHelper(x+r,    y+h-r-1,r,8,color);
    endWrite();
}

void LinuxGFX::fillRoundRect(int16_t x,int16_t y,int16_t w,int16_t h,int16_t r,uint16_t color){
    startWrite();
    int16_t mx=((w<h)?w:h)/2; if(r>mx)r=mx;
    writeFillRect(x+r,y,w-2*r,h,color);
    fillCircleHelper(x+w-r-1,y+r,r,1,h-2*r-1,color);
    fillCircleHelper(x+r,    y+r,r,2,h-2*r-1,color);
    endWrite();
}

// ─── Triangles ────────────────────────────────────────────────────────────────
void LinuxGFX::drawTriangle(int16_t x0,int16_t y0,int16_t x1,int16_t y1,int16_t x2,int16_t y2,uint16_t color){
    drawLine(x0,y0,x1,y1,color);drawLine(x1,y1,x2,y2,color);drawLine(x2,y2,x0,y0,color);
}

void LinuxGFX::fillTriangle(int16_t x0,int16_t y0,int16_t x1,int16_t y1,int16_t x2,int16_t y2,uint16_t color){
    if(y0>y1){SWAP(y0,y1);SWAP(x0,x1);}
    if(y1>y2){SWAP(y1,y2);SWAP(x1,x2);}
    if(y0>y1){SWAP(y0,y1);SWAP(x0,x1);}
    if(y0==y2){
        int16_t a=MIN(x0,MIN(x1,x2)), b=MAX(x0,MAX(x1,x2));
        drawFastHLine(a,y0,b-a+1,color); return;
    }
    startWrite();
    int16_t dx01=x1-x0,dy01=y1-y0,dx02=x2-x0,dy02=y2-y0,dx12=x2-x1,dy12=y2-y1;
    int32_t sa=0,sb=0;
    int16_t last=(y1==y2)?y1:y1-1;
    for(int16_t y=y0;y<=last;y++){
        int16_t a=x0+sa/dy01, b=x0+sb/dy02;
        sa+=dx01; sb+=dx02;
        if(a>b)SWAP(a,b);
        writeFastHLine(a,y,b-a+1,color);
    }
    sa=0; sb=(int32_t)dx02*(y1-y0);
    for(int16_t y=y1;y<=y2;y++){
        int16_t a=x1+sa/dy12, b=x0+sb/dy02;
        sa+=dx12; sb+=dx02;
        if(a>b)SWAP(a,b);
        writeFastHLine(a,y,b-a+1,color);
    }
    endWrite();
}

// ─── Bitmaps ──────────────────────────────────────────────────────────────────
void LinuxGFX::drawBitmap(int16_t x,int16_t y,const uint8_t bitmap[],int16_t w,int16_t h,uint16_t color){
    int16_t bw=(w+7)/8; uint8_t b=0;
    startWrite();
    for(int16_t j=0;j<h;j++,y++) for(int16_t i=0;i<w;i++){
        if(i&7)b<<=1; else b=bitmap[j*bw+i/8];
        if(b&0x80)writePixel(x+i,y,color);
    }
    endWrite();
}

void LinuxGFX::drawBitmap(int16_t x,int16_t y,const uint8_t bitmap[],int16_t w,int16_t h,uint16_t color,uint16_t bg){
    int16_t bw=(w+7)/8; uint8_t b=0;
    startWrite();
    for(int16_t j=0;j<h;j++,y++) for(int16_t i=0;i<w;i++){
        if(i&7)b<<=1; else b=bitmap[j*bw+i/8];
        writePixel(x+i,y,(b&0x80)?color:bg);
    }
    endWrite();
}

void LinuxGFX::drawBitmap(int16_t x,int16_t y,uint8_t *bitmap,int16_t w,int16_t h,uint16_t color)
{ drawBitmap(x,y,(const uint8_t*)bitmap,w,h,color); }
void LinuxGFX::drawBitmap(int16_t x,int16_t y,uint8_t *bitmap,int16_t w,int16_t h,uint16_t color,uint16_t bg)
{ drawBitmap(x,y,(const uint8_t*)bitmap,w,h,color,bg); }

void LinuxGFX::drawXBitmap(int16_t x,int16_t y,const uint8_t bitmap[],int16_t w,int16_t h,uint16_t color){
    int16_t bw=(w+7)/8; uint8_t b=0;
    startWrite();
    for(int16_t j=0;j<h;j++,y++) for(int16_t i=0;i<w;i++){
        if(i&7)b>>=1; else b=bitmap[j*bw+i/8];
        if(b&0x01)writePixel(x+i,y,color);
    }
    endWrite();
}

void LinuxGFX::drawGrayscaleBitmap(int16_t x,int16_t y,const uint8_t bitmap[],int16_t w,int16_t h){
    startWrite();
    for(int16_t j=0;j<h;j++,y++) for(int16_t i=0;i<w;i++)
        writePixel(x+i,y,(uint8_t)bitmap[j*w+i]);
    endWrite();
}
void LinuxGFX::drawGrayscaleBitmap(int16_t x,int16_t y,uint8_t *bitmap,int16_t w,int16_t h)
{ drawGrayscaleBitmap(x,y,(const uint8_t*)bitmap,w,h); }

void LinuxGFX::drawGrayscaleBitmap(int16_t x,int16_t y,const uint8_t bitmap[],const uint8_t mask[],int16_t w,int16_t h){
    int16_t bw=(w+7)/8; uint8_t b=0;
    startWrite();
    for(int16_t j=0;j<h;j++,y++) for(int16_t i=0;i<w;i++){
        if(i&7)b<<=1; else b=mask[j*bw+i/8];
        if(b&0x80)writePixel(x+i,y,(uint8_t)bitmap[j*w+i]);
    }
    endWrite();
}
void LinuxGFX::drawGrayscaleBitmap(int16_t x,int16_t y,uint8_t *bitmap,uint8_t *mask,int16_t w,int16_t h)
{ drawGrayscaleBitmap(x,y,(const uint8_t*)bitmap,(const uint8_t*)mask,w,h); }

void LinuxGFX::drawRGBBitmap(int16_t x,int16_t y,const uint16_t bitmap[],const uint8_t mask[],int16_t w,int16_t h){
    int16_t bw=(w+7)/8; uint8_t b=0;
    startWrite();
    for(int16_t j=0;j<h;j++,y++) for(int16_t i=0;i<w;i++){
        if(i&7)b<<=1; else b=mask[j*bw+i/8];
        if(b&0x80)writePixel(x+i,y,bitmap[j*w+i]);
    }
    endWrite();
}
void LinuxGFX::drawRGBBitmap(int16_t x,int16_t y,uint16_t *bitmap,uint8_t *mask,int16_t w,int16_t h)
{ drawRGBBitmap(x,y,(const uint16_t*)bitmap,(const uint8_t*)mask,w,h); }

// ─── Default 5×8 font ────────────────────────────────────────────────────────
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

// ─── Text rendering ───────────────────────────────────────────────────────────
void LinuxGFX::drawChar(int16_t x,int16_t y,unsigned char c,uint16_t color,uint16_t bg,uint8_t sx,uint8_t sy){
    if (!m_pFont) {
        if (x>=m_width||y>=m_height||(x+6*sx-1)<0||(y+8*sy-1)<0) return;
        if (c<32||c>126) c='?';
        const uint8_t *glyph=s_font+(c-32)*5;
        startWrite();
        for (int8_t col=0;col<5;col++) {
            uint8_t bits=glyph[col];
            for (int8_t row=0;row<8;row++,bits>>=1) {
                uint16_t px=(bits&1)?color:bg;
                if (sx==1&&sy==1) writePixel(x+col,y+row,px);
                else writeFillRect(x+col*sx,y+row*sy,sx,sy,px);
            }
        }
        for (int8_t row=0;row<8;row++) {
            if(sx==1&&sy==1)writePixel(x+5,y+row,bg);
            else writeFillRect(x+5*sx,y+row*sy,sx,sy,bg);
        }
        endWrite();
    } else {
        if (c<m_pFont->first||c>m_pFont->last) return;
        uint8_t ci=c-m_pFont->first;
        const GFXglyph *gl=&m_pFont->glyph[ci];
        const uint8_t  *bits=m_pFont->bitmap+gl->bitmapOffset;
        int16_t gx=x+gl->xOffset, gy=y+gl->yOffset;
        int16_t gw=gl->width, gh=gl->height;
        uint8_t bit=0,bits8=0;
        startWrite();
        for (int16_t gy2=0;gy2<gh;gy2++)
            for (int16_t gx2=0;gx2<gw;gx2++) {
                if(!(bit++&7))bits8=*bits++;
                if(bits8&0x80) {
                    if(sx==1&&sy==1) writePixel(gx+gx2,gy+gy2,color);
                    else writeFillRect(gx+gx2*sx,gy+gy2*sy,sx,sy,color);
                }
                bits8<<=1;
            }
        endWrite();
    }
}

void LinuxGFX::drawChar(int16_t x,int16_t y,unsigned char c,uint16_t color,uint16_t bg,uint8_t size)
{ drawChar(x,y,c,color,bg,size,size); }

void LinuxGFX::writeText(const char *text) {
    while (*text) {
        char c=*text++;
        if (c=='\n') { m_cursorX=0; m_cursorY+=m_textSizeY*(m_pFont?m_pFont->yAdvance:8); }
        else if (c!='\r') {
            int16_t adv=m_pFont?m_pFont->glyph[c-m_pFont->first].xAdvance:6;
            if (m_textWrap && (m_cursorX+m_textSizeX*adv>m_width)) {
                m_cursorX=0; m_cursorY+=m_textSizeY*(m_pFont?m_pFont->yAdvance:8);
            }
            drawChar(m_cursorX,m_cursorY,c,m_textColor,m_textBgColor,m_textSizeX,m_textSizeY);
            m_cursorX+=m_textSizeX*adv;
        }
    }
}

void LinuxGFX::setFont      (const GFXfont *f)       { m_pFont=f; }
void LinuxGFX::setCursor    (int16_t x,int16_t y)    { m_cursorX=x; m_cursorY=y; }
void LinuxGFX::setTextColor (uint16_t c)              { m_textColor=c; m_textBgColor=c; }
void LinuxGFX::setTextColor (uint16_t c,uint16_t bg)  { m_textColor=c; m_textBgColor=bg; }
void LinuxGFX::setTextSize  (uint8_t s)               { m_textSizeX=m_textSizeY=s?s:1; }
void LinuxGFX::setTextSize  (uint8_t sx,uint8_t sy)   { m_textSizeX=sx?sx:1; m_textSizeY=sy?sy:1; }
void LinuxGFX::setTextWrap  (bool w)                  { m_textWrap=w; }

// ─── Control & dimensions ────────────────────────────────────────────────────
void LinuxGFX::setRotation(uint8_t r) {
    m_rotation=r%4;
    if (m_rotation==1||m_rotation==3) SWAP(m_width,m_height);
}
uint8_t  LinuxGFX::getRotation()   const { return m_rotation; }
void     LinuxGFX::invertDisplay(bool i) { m_inverted=i; }
int16_t  LinuxGFX::width()         const { return m_width; }
int16_t  LinuxGFX::height()        const { return m_height; }
int16_t  LinuxGFX::getCursorX()    const { return m_cursorX; }
int16_t  LinuxGFX::getCursorY()    const { return m_cursorY; }

void LinuxGFX::drawFastVLineInternal(int16_t x,int16_t y,int16_t h,uint16_t c){writeFastVLine(x,y,h,c);}
void LinuxGFX::drawFastHLineInternal(int16_t x,int16_t y,int16_t w,uint16_t c){writeFastHLine(x,y,w,c);}

uint16_t LinuxGFX::color565(uint8_t r,uint8_t g,uint8_t b)
{ return ((r&0xF8)<<8)|((g&0xFC)<<3)|(b>>3); }
uint16_t LinuxGFX::color565(uint32_t rgb)
{ return color565((rgb>>16)&0xFF,(rgb>>8)&0xFF,rgb&0xFF); }
