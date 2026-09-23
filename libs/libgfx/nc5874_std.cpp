// =============================================================================
// Freestanding support for the GFX_NC5874 back-end — implementation.
// See nc5874_std.h. Built only when GFX_NC5874 is defined.
// =============================================================================

#ifdef GFX_NC5874

#include "nc5874_std.h"

// ===== <cstdlib> =============================================================
//
// Every block carries a small header with its size so realloc() can copy the
// old contents; the platform hook only needs plain malloc/free.

namespace {

struct BlockHeader {
    size_t size;
    size_t pad;     ///< keeps the payload 8-byte aligned
};

BlockHeader *headerOf (void *p) {
    return (BlockHeader *) p - 1;
}

uint32_t s_randState = 0x12345678u;

} // namespace

void *malloc (size_t n) {
    BlockHeader *h = (BlockHeader *) gfx_nc5874_malloc(sizeof(BlockHeader) + n);
    if (!h) {
        return nullptr;
    }
    h->size = n;
    return h + 1;
}

void *calloc (size_t count, size_t size) {
    size_t n = count * size;
    void *p = malloc(n);
    if (p) {
        memset(p, 0, n);
    }
    return p;
}

void *realloc (void *p, size_t n) {
    if (!p) {
        return malloc(n);
    }
    size_t old = headerOf(p)->size;
    if (n <= old) {
        headerOf(p)->size = n;
        return p;
    }
    void *q = malloc(n);
    if (!q) {
        return nullptr;
    }
    memcpy(q, p, old);
    free(p);
    return q;
}

void free (void *p) {
    if (p) {
        gfx_nc5874_free(headerOf(p));
    }
}

int rand (void) {
    s_randState = s_randState * 1103515245u + 12345u;
    return (int) ((s_randState >> 16) & 0x7fff);
}

int abs (int v) {
    return v < 0 ? -v : v;
}

// ===== C++ runtime ===========================================================

void *operator new (size_t n)            { return malloc(n); }
void *operator new[] (size_t n)          { return malloc(n); }
void  operator delete (void *p) noexcept           { free(p); }
void  operator delete[] (void *p) noexcept         { free(p); }
void  operator delete (void *p, size_t) noexcept   { free(p); }
void  operator delete[] (void *p, size_t) noexcept { free(p); }

extern "C" {
    void *__dso_handle = nullptr;

    int __cxa_atexit (void (*)(void *), void *, void *) {
        return 0;   // static destructors never run on this target
    }

    void __cxa_pure_virtual (void) {
        gfx_nc5874_log("GFX: pure virtual call\n");
        for (;;) {
        }
    }
}

// ===== <cmath> ===============================================================

float fabsf (float x) {
    return x < 0.0f ? -x : x;
}

double fabs (double x) {
    return x < 0.0 ? -x : x;
}

float floorf (float x) {
    if (x >= 8388608.0f || x <= -8388608.0f) {
        return x;                       // already integral (|x| >= 2^23)
    }
    int i = (int) x;
    float f = (float) i;
    return (f > x) ? f - 1.0f : f;
}

double floor (double x) {
    if (x >= 4503599627370496.0 || x <= -4503599627370496.0) {
        return x;
    }
    long long i = (long long) x;
    double f = (double) i;
    return (f > x) ? f - 1.0 : f;
}

float ceilf (float x) {
    float f = floorf(x);
    return (f < x) ? f + 1.0f : f;
}

float roundf (float x) {
    return x < 0.0f ? -floorf(-x + 0.5f) : floorf(x + 0.5f);
}

double round (double x) {
    return x < 0.0 ? -floor(-x + 0.5) : floor(x + 0.5);
}

float fmodf (float x, float y) {
    if (y == 0.0f) {
        return 0.0f;
    }
    float q = x / y;
    q = (q < 0.0f) ? ceilf(q) : floorf(q);
    return x - q * y;
}

float sqrtf (float x) {
    if (x <= 0.0f) {
        return 0.0f;
    }
    union { float f; uint32_t u; } v = { x };
    v.u = (v.u >> 1) + 0x1fc00000u;     // initial guess from the exponent
    float g = v.f;
    for (int i = 0; i < 4; i++) {
        g = 0.5f * (g + x / g);         // Newton-Raphson
    }
    return g;
}

double sqrt (double x) {
    return (double) sqrtf((float) x);
}

namespace {

const float kPi     = 3.14159265358979f;
const float kHalfPi = 1.57079632679490f;
const float kTwoPi  = 6.28318530717959f;

/// sin on [-pi/2, pi/2], odd polynomial (error < 2e-7).
float sinCore (float x) {
    float x2 = x * x;
    return x * (1.0f + x2 * (-1.6666667e-1f + x2 * (8.3333310e-3f +
           x2 * (-1.9840874e-4f + x2 * 2.7525562e-6f))));
}

} // namespace

float sinf (float x) {
    x = fmodf(x, kTwoPi);               // -> (-2pi, 2pi)
    if (x > kPi) {
        x -= kTwoPi;
    } else if (x < -kPi) {
        x += kTwoPi;
    }
    if (x > kHalfPi) {                  // sin(pi - x) = sin(x)
        x = kPi - x;
    } else if (x < -kHalfPi) {
        x = -kPi - x;
    }
    return sinCore(x);
}

float cosf (float x) {
    return sinf(x + kHalfPi);
}

float atan2f (float y, float x) {
    if (x == 0.0f && y == 0.0f) {
        return 0.0f;
    }
    float ax = fabsf(x), ay = fabsf(y);
    float a = (ax > ay) ? ay / ax : ax / ay;    // 0..1
    float s = a * a;
    // atan(a) on [0,1], max error ~1e-5 rad
    float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    if (ay > ax) {
        r = kHalfPi - r;
    }
    if (x < 0.0f) {
        r = kPi - r;
    }
    return (y < 0.0f) ? -r : r;
}

// ===== <cstdio> ==============================================================

namespace {

struct Out {
    char  *buf;
    size_t size;
    size_t len;

    void put (char c) {
        if (len + 1 < size) {
            buf[len] = c;
        }
        len++;
    }
};

void putPadded (Out &o, const char *s, size_t n, int width, bool left, char padc) {
    int pad = width > (int) n ? width - (int) n : 0;
    if (!left) {
        while (pad-- > 0) {
            o.put(padc);
        }
    }
    for (size_t i = 0; i < n; i++) {
        o.put(s[i]);
    }
    if (left) {
        while (pad-- > 0) {
            o.put(' ');
        }
    }
}

size_t utoa (char *tmp, unsigned long long v, unsigned base, bool upper) {
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char rev[24];
    size_t n = 0;
    do {
        rev[n++] = digits[v % base];
        v /= base;
    } while (v);
    for (size_t i = 0; i < n; i++) {
        tmp[i] = rev[n - 1 - i];
    }
    return n;
}

} // namespace

int vsnprintf (char *buf, size_t size, const char *fmt, va_list ap) {
    Out o = { buf, size, 0 };

    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            o.put(*fmt);
            continue;
        }
        fmt++;

        bool left = false, zero = false, plus = false;
        for (;; fmt++) {
            if (*fmt == '-') {
                left = true;
            } else if (*fmt == '0') {
                zero = true;
            } else if (*fmt == '+') {
                plus = true;
            } else {
                break;
            }
        }
        int width = 0;
        if (*fmt == '*') {
            width = va_arg(ap, int);
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt++ - '0');
        }
        int prec = -1;
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            if (*fmt == '*') {
                prec = va_arg(ap, int);
                fmt++;
            }
            while (*fmt >= '0' && *fmt <= '9') {
                prec = prec * 10 + (*fmt++ - '0');
            }
        }
        int lng = 0;
        while (*fmt == 'l' || *fmt == 'h' || *fmt == 'z') {
            if (*fmt == 'l') {
                lng++;
            }
            fmt++;
        }

        char tmp[48];
        size_t n = 0;
        char padc = (zero && !left) ? '0' : ' ';

        switch (*fmt) {
        case 'd':
        case 'i': {
            long long v = (lng >= 2) ? va_arg(ap, long long)
                        : (lng == 1) ? (long long) va_arg(ap, long) : (long long) va_arg(ap, int);
            bool neg = v < 0;
            unsigned long long u = neg ? (unsigned long long) (-v) : (unsigned long long) v;
            if (neg) {
                tmp[n++] = '-';
            } else if (plus) {
                tmp[n++] = '+';
            }
            n += utoa(tmp + n, u, 10, false);
            putPadded(o, tmp, n, width, left, padc);
            break;
        }
        case 'u':
        case 'x':
        case 'X':
        case 'o': {
            unsigned long long u = (lng >= 2) ? va_arg(ap, unsigned long long)
                                 : (lng == 1) ? (unsigned long long) va_arg(ap, unsigned long)
                                 : (unsigned long long) va_arg(ap, unsigned);
            unsigned base = (*fmt == 'u') ? 10 : (*fmt == 'o') ? 8 : 16;
            n = utoa(tmp, u, base, *fmt == 'X');
            putPadded(o, tmp, n, width, left, padc);
            break;
        }
        case 'p': {
            tmp[0] = '0';
            tmp[1] = 'x';
            n = 2 + utoa(tmp + 2, (unsigned long long) (uintptr_t) va_arg(ap, void *), 16, false);
            putPadded(o, tmp, n, width, left, ' ');
            break;
        }
        case 'c':
            tmp[0] = (char) va_arg(ap, int);
            putPadded(o, tmp, 1, width, left, ' ');
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) {
                s = "(null)";
            }
            size_t len = strlen(s);
            if (prec >= 0 && (size_t) prec < len) {
                len = (size_t) prec;
            }
            putPadded(o, s, len, width, left, ' ');
            break;
        }
        case 'f':
        case 'F': {
            double v = va_arg(ap, double);
            if (prec < 0) {
                prec = 6;
            }
            if (prec > 9) {
                prec = 9;
            }
            if (v < 0.0) {
                tmp[n++] = '-';
                v = -v;
            } else if (plus) {
                tmp[n++] = '+';
            }
            double scale = 1.0;
            for (int i = 0; i < prec; i++) {
                scale *= 10.0;
            }
            unsigned long long whole = (unsigned long long) v;
            unsigned long long frac = (unsigned long long) ((v - (double) whole) * scale + 0.5);
            if ((double) frac >= scale) {
                whole++;
                frac = 0;
            }
            n += utoa(tmp + n, whole, 10, false);
            if (prec > 0) {
                tmp[n++] = '.';
                char fr[24];
                size_t fn = utoa(fr, frac, 10, false);
                for (int i = (int) fn; i < prec; i++) {
                    tmp[n++] = '0';
                }
                for (size_t i = 0; i < fn; i++) {
                    tmp[n++] = fr[i];
                }
            }
            putPadded(o, tmp, n, width, left, padc);
            break;
        }
        case '%':
            o.put('%');
            break;
        case '\0':
            fmt--;
            break;
        default:
            o.put('%');
            o.put(*fmt);
            break;
        }
    }

    if (size) {
        buf[o.len < size ? o.len : size - 1] = '\0';
    }
    return (int) o.len;
}

int snprintf (char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

int fprintf (FILE *stream, const char *fmt, ...) {
    (void) stream;
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    gfx_nc5874_log(line);
    return n;
}

int printf (const char *fmt, ...) {
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    gfx_nc5874_log(line);
    return n;
}

#endif // GFX_NC5874
