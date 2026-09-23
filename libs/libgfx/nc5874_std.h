#ifndef NC5874_STD_H
#define NC5874_STD_H

// =============================================================================
// Freestanding support for the GFX_NC5874 back-end
// =============================================================================
//
// The Nationalchip 5874 set-top-box target runs bare-metal under U-Boot: no OS,
// no C library, no C++ standard library, no FPU. This header stands in for the
// pieces of <cstdlib>/<cstring>/<cstdio>/<cmath>/<algorithm>/<vector> that
// libgfx actually uses, so GFX.cpp / DrawReplay.cpp build unchanged apart from
// their include lines.
//
// Build with: -DGFX_NC5874 -msoft-float -ffreestanding -fno-exceptions
//             -fno-rtti -fno-threadsafe-statics -nostdlib
// and link a soft-float runtime (e.g. compiler-rt builtins built -msoft-float):
// the toolchain's own libgcc uses FPU instructions the 24KEc core lacks.
//
// ----- Platform hooks (the application must provide these, extern "C") ------
//
//   void    *gfx_nc5874_malloc (size_t n);   // heap allocation, may return NULL
//   void     gfx_nc5874_free   (void *p);    // accepts NULL
//   void     gfx_nc5874_log    (const char *s);   // debug text output
//   uint32_t gfx_nc5874_millis (void);       // free-running millisecond clock
//
// ----- C library functions (the application must provide these, extern "C") -
//
//   memcpy, memmove, memset, memcmp, strlen, strcmp
//

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

extern "C" {
    void    *gfx_nc5874_malloc (size_t n);
    void     gfx_nc5874_free   (void *p);
    void     gfx_nc5874_log    (const char *s);
    uint32_t gfx_nc5874_millis (void);

    void    *memcpy  (void *dst, const void *src, size_t n);
    void    *memmove (void *dst, const void *src, size_t n);
    void    *memset  (void *dst, int c, size_t n);
    int      memcmp  (const void *a, const void *b, size_t n);
    size_t   strlen  (const char *s);
    int      strcmp  (const char *a, const char *b);
}

// ----- <cstdlib> -------------------------------------------------------------

void *malloc  (size_t n);
void *calloc  (size_t count, size_t size);
void *realloc (void *p, size_t n);
void  free    (void *p);
int   rand    (void);
int   abs     (int v);

// ----- <cstdio> --------------------------------------------------------------

typedef struct nc5874_file FILE;
#define stderr ((FILE *) 2)
#define stdout ((FILE *) 1)

int vsnprintf (char *buf, size_t size, const char *fmt, va_list ap);
int snprintf  (char *buf, size_t size, const char *fmt, ...);
int fprintf   (FILE *stream, const char *fmt, ...);
int printf    (const char *fmt, ...);

// ----- <cmath> (single + the few double functions libgfx uses) ---------------

float  sqrtf  (float x);
float  floorf (float x);
float  ceilf  (float x);
float  fabsf  (float x);
float  roundf (float x);
float  fmodf  (float x, float y);
float  sinf   (float x);
float  cosf   (float x);
float  atan2f (float y, float x);
double round  (double x);
double floor  (double x);
double fabs   (double x);
double sqrt   (double x);

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

// ----- operator new / delete -------------------------------------------------

inline void *operator new (size_t, void *p) noexcept { return p; }
inline void *operator new[] (size_t, void *p) noexcept { return p; }

// ----- <algorithm> / <utility> / <vector> subset -----------------------------

namespace std {

using ::size_t;

template <typename T> inline void swap (T &a, T &b) { T t = a; a = b; b = t; }
template <typename T> inline const T &min (const T &a, const T &b) { return (b < a) ? b : a; }
template <typename T> inline const T &max (const T &a, const T &b) { return (a < b) ? b : a; }
inline int   abs (int v)     { return v < 0 ? -v : v; }
inline long  abs (long v)    { return v < 0 ? -v : v; }
inline float abs (float v)   { return v < 0 ? -v : v; }

/**
 * Minimal std::vector for trivially-copyable element types (all libgfx uses:
 * bytes, pixels, chars, pointers, small POD structs). Growth is by doubling;
 * elements are copied with memcpy, so non-trivial types are not supported.
 */
template <typename T>
class vector {
public:
    typedef T        value_type;
    typedef T       *iterator;
    typedef const T *const_iterator;

    vector () : m_data(nullptr), m_size(0), m_cap(0) {}
    explicit vector (size_t n) : m_data(nullptr), m_size(0), m_cap(0) { resize(n); }
    vector (size_t n, const T &v) : m_data(nullptr), m_size(0), m_cap(0) { resize(n, v); }
    vector (const vector &o) : m_data(nullptr), m_size(0), m_cap(0) { assign(o.begin(), o.end()); }
    vector (vector &&o) noexcept : m_data(o.m_data), m_size(o.m_size), m_cap(o.m_cap) {
        o.m_data = nullptr;
        o.m_size = o.m_cap = 0;
    }
    ~vector () { ::free(m_data); }

    vector &operator= (const vector &o) {
        if (this != &o) {
            assign(o.begin(), o.end());
        }
        return *this;
    }
    vector &operator= (vector &&o) noexcept {
        if (this != &o) {
            ::free(m_data);
            m_data = o.m_data; m_size = o.m_size; m_cap = o.m_cap;
            o.m_data = nullptr; o.m_size = o.m_cap = 0;
        }
        return *this;
    }

    size_t   size     () const { return m_size; }
    size_t   capacity () const { return m_cap; }
    bool     empty    () const { return m_size == 0; }
    T       *data     ()       { return m_data; }
    const T *data     () const { return m_data; }

    iterator       begin ()       { return m_data; }
    iterator       end   ()       { return m_data + m_size; }
    const_iterator begin () const { return m_data; }
    const_iterator end   () const { return m_data + m_size; }

    T       &operator[] (size_t i)       { return m_data[i]; }
    const T &operator[] (size_t i) const { return m_data[i]; }
    T       &back ()       { return m_data[m_size - 1]; }
    const T &back () const { return m_data[m_size - 1]; }

    void clear () { m_size = 0; }

    void reserve (size_t n) {
        if (n <= m_cap) {
            return;
        }
        T *p = (T *) ::realloc(m_data, n * sizeof(T));
        if (p) {
            m_data = p;
            m_cap = n;
        }
    }

    void resize (size_t n) {
        grow(n);
        if (n > m_size) {
            memset(m_data + m_size, 0, (n - m_size) * sizeof(T));
        }
        if (n <= m_cap) {
            m_size = n;
        }
    }

    void resize (size_t n, const T &v) {
        grow(n);
        for (size_t i = m_size; i < n && i < m_cap; i++) {
            m_data[i] = v;
        }
        if (n <= m_cap) {
            m_size = n;
        }
    }

    void push_back (const T &v) {
        grow(m_size + 1);
        if (m_size < m_cap) {
            m_data[m_size++] = v;
        }
    }

    template <typename It>
    void assign (It first, It last) {
        m_size = 0;
        insert(end(), first, last);
    }

    /// Append-only insert (libgfx only ever inserts at end()).
    template <typename It>
    iterator insert (iterator pos, It first, It last) {
        size_t at = (size_t) (pos - m_data);
        size_t n = 0;
        for (It i = first; i != last; ++i) {
            n++;
        }
        grow(m_size + n);
        if (m_size + n > m_cap) {
            return end();
        }
        if (at < m_size) {
            memmove(m_data + at + n, m_data + at, (m_size - at) * sizeof(T));
        }
        size_t k = at;
        for (It i = first; i != last; ++i) {
            m_data[k++] = *i;
        }
        m_size += n;
        return m_data + at;
    }

private:
    void grow (size_t need) {
        if (need <= m_cap) {
            return;
        }
        size_t c = m_cap ? m_cap * 2 : 16;
        while (c < need) {
            c *= 2;
        }
        reserve(c);
    }

    T     *m_data;
    size_t m_size;
    size_t m_cap;
};

} // namespace std

#endif // NC5874_STD_H
