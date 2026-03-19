#pragma once
#include "gfx.h"
#include <cmath>
#include <cstdio>
#include <cstring>

// Analog Galvanometer-style gauge widget for LinuxGFX (Adafruit GFX syntax)
//
// Usage:
//   GaugeWidget gauge(gfx, cx, cy, radius, minVal, maxVal);
//   gauge.setLabel("CH0");
//   gauge.setUnit("V");
//   gauge.drawBackground();   // call ONCE after init(), before the loop
//   gauge.draw(value);        // call every frame inside the loop

class GaugeWidget {
public:
    GaugeWidget(LinuxGFX& gfx, int16_t cx, int16_t cy, int16_t radius,
                float minVal = 0.0f, float maxVal = 5.0f)
        : _gfx(gfx), _cx(cx), _cy(cy), _r(radius),
          _min(minVal), _max(maxVal),
          _prevNeedleX(-1), _prevNeedleY(-1),
          _prevTailX(-1),   _prevTailY(-1)
    {
        strncpy(_label, "ADC", sizeof(_label) - 1);  _label[sizeof(_label)-1] = '\0';
        strncpy(_unit,  "V",   sizeof(_unit)  - 1);  _unit [sizeof(_unit) -1] = '\0';
    }

    void setLabel(const char* label) { strncpy(_label, label, sizeof(_label)-1); }
    void setUnit (const char* unit)  { strncpy(_unit,  unit,  sizeof(_unit) -1); }

    // ── Static face — call once after init() ──────────────────────────────────
    void drawBackground() {
        const int16_t r  = _r;
        const int16_t cx = _cx;
        const int16_t cy = _cy;

        // Face fill — dark blue-grey
        _gfx.fillCircle(cx, cy, r,     color(0x12, 0x18, 0x28));

        // Outer bezel — two rings for a machined-metal look
        _gfx.drawCircle(cx, cy, r,     color(0x90, 0xA0, 0xB8));
        _gfx.drawCircle(cx, cy, r - 1, color(0x50, 0x60, 0x78));
        _gfx.drawCircle(cx, cy, r - 2, color(0x28, 0x30, 0x48));

        // Subtle inner reference ring
        _gfx.drawCircle(cx, cy, r - 9, color(0x22, 0x2C, 0x42));

        // ── Ticks & labels ────────────────────────────────────────────────────
        // Sweep convention (same as a D'Arsonval/Weston panel meter):
        //   Start: lower-left  = math angle 200° (CW sweep in screen coords)
        //   End  : lower-right = math angle 200° − 220° = −20° = 340°
        //   Parameterisation: angle(t) = 200° − t×220°,  t∈[0,1]
        //                     t=0 → left (min),  t=1 → right (max)

        const int numMajor  = 10;
        const int numMinor  = 5;
        const int totalTicks = numMajor * numMinor;

        for (int i = 0; i <= totalTicks; i++) {
            float t    = (float)i / (float)totalTicks;
            float deg  = 200.0f - t * 220.0f;
            float rad  = deg * (float)M_PI / 180.0f;
            float cosA = cosf(rad);
            float sinA = sinf(rad);

            bool    isMajor = (i % numMinor == 0);
            int16_t tickLen = isMajor ? (r / 6) : (r / 12);
            uint16_t tc     = isMajor ? color(0xC8, 0xD8, 0xF0)
                                      : color(0x48, 0x58, 0x74);

            int16_t outerR = r - 9;
            int16_t innerR = outerR - tickLen;

            int16_t x0 = cx + (int16_t)((float)outerR * cosA);
            int16_t y0 = cy + (int16_t)((float)outerR * sinA);
            int16_t x1 = cx + (int16_t)((float)innerR * cosA);
            int16_t y1 = cy + (int16_t)((float)innerR * sinA);

            _gfx.drawLine(x0,   y0, x1,   y1, tc);
            if (isMajor) {
                // Extra pixel width on major ticks
                _gfx.drawLine(x0+1, y0, x1+1, y1, tc);
            }

            // Label on major ticks
            if (isMajor) {
                float val = _min + t * (_max - _min);
                char buf[12];
                if ((_max - _min) <= 2.0f)
                    snprintf(buf, sizeof(buf), "%.1f", val);
                else
                    snprintf(buf, sizeof(buf), "%.0f", val);

                float labelR = (float)(innerR - 11);
                int16_t lx = cx + (int16_t)(labelR * cosA) - (int16_t)(strlen(buf) * 3);
                int16_t ly = cy + (int16_t)(labelR * sinA) - 4;

                _gfx.setTextSize(1);
                _gfx.setTextColor(color(0x70, 0x98, 0xC0));
                _gfx.setCursor(lx, ly);
                _gfx.writeText(buf);
            }
        }

        // ── Danger-zone arc (top 20% of range, near the right end) ───────────
        for (int i = 0; i <= 44; i++) {
            float t   = 0.80f + (float)i / 44.0f * 0.20f;
            float deg = 200.0f - t * 220.0f;
            float rad = deg * (float)M_PI / 180.0f;
            int16_t dotR = r - 5;
            int16_t dx = cx + (int16_t)((float)dotR * cosf(rad));
            int16_t dy = cy + (int16_t)((float)dotR * sinf(rad));
            _gfx.drawPixel(dx,   dy,   color(0xFF, 0x28, 0x08));
            _gfx.drawPixel(dx+1, dy,   color(0xFF, 0x28, 0x08));
            _gfx.drawPixel(dx,   dy+1, color(0xFF, 0x28, 0x08));
        }

        // ── Instrument label ──────────────────────────────────────────────────
        _gfx.setTextSize(2);
        _gfx.setTextColor(color(0x20, 0xA0, 0xFF));
        // Centre the label under the pivot
        int16_t lblW = (int16_t)(strlen(_label) * 12);   // 12px per char at size 2
        _gfx.setCursor(cx - lblW / 2, cy + r / 4);
        _gfx.writeText(_label);

        // Unit, smaller, just below label
        _gfx.setTextSize(1);
        _gfx.setTextColor(color(0x48, 0x70, 0xA0));
        _gfx.setCursor(cx + 4, cy + r / 4 + 20);
        _gfx.writeText(_unit);

        // ── Pivot ─────────────────────────────────────────────────────────────
        _drawPivot();
    }

    // ── Animated needle — call every frame ───────────────────────────────────
    void draw(float value) {
        float v = value < _min ? _min : (value > _max ? _max : value);
        float t   = (v - _min) / (_max - _min);
        float deg = 200.0f - t * 220.0f;
        float rad = deg * (float)M_PI / 180.0f;

        float cosA = cosf(rad);
        float sinA = sinf(rad);

        int16_t needleLen = _r - _r / 5;
        int16_t tailLen   = _r / 8;

        int16_t nx  = _cx + (int16_t)((float) needleLen * cosA);
        int16_t ny  = _cy + (int16_t)((float) needleLen * sinA);
        int16_t ntx = _cx - (int16_t)((float) tailLen   * cosA);
        int16_t nty = _cy - (int16_t)((float) tailLen   * sinA);

        // Erase previous needle by overdrawing in face colour
        if (_prevNeedleX >= 0) {
            uint16_t bg = color(0x12, 0x18, 0x28);
            _gfx.drawLine(_cx, _cy, _prevNeedleX,   _prevNeedleY,   bg);
            _gfx.drawLine(_cx, _cy, _prevNeedleX+1, _prevNeedleY,   bg);
            _gfx.drawLine(_cx, _cy, _prevTailX,     _prevTailY,     bg);
            // Erase tip
            _gfx.fillCircle(_prevNeedleX, _prevNeedleY, 2, bg);
        }

        // Needle colour interpolates green → yellow → red
        uint8_t nr = (uint8_t)(t < 0.5f ? (t * 2.0f * 200.0f)         : 200);
        uint8_t ng = (uint8_t)(t < 0.5f ? 220 : ((1.0f - t) * 2.0f * 220.0f));
        uint16_t nc = color(nr, ng, 0x08);

        // Draw needle (2px wide)
        _gfx.drawLine(_cx, _cy, nx,   ny, nc);
        _gfx.drawLine(_cx, _cy, nx+1, ny, nc);
        // Counterweight tail
        _gfx.drawLine(_cx, _cy, ntx, nty, color(0x80, 0x28, 0x20));
        // Bright tip dot
        _gfx.fillCircle(nx, ny, 2, color(0xFF, 0xFF, 0xFF));

        // Restore pivot on top of needle
        _drawPivot();

        _prevNeedleX = nx;  _prevNeedleY = ny;
        _prevTailX   = ntx; _prevTailY   = nty;

        // ── Digital readout ───────────────────────────────────────────────────
        char buf[24];
        snprintf(buf, sizeof(buf), "%6.3f %s", value, _unit);

        int16_t bx = _cx - 44;
        int16_t by = _cy + _r / 2 + 10;

        _gfx.fillRect    (bx,     by,     92, 18, color(0x00, 0x06, 0x18));
        _gfx.drawRect    (bx,     by,     92, 18, color(0x18, 0x48, 0xA0));
        _gfx.drawFastHLine(bx+1,  by+1,  90,     color(0x28, 0x68, 0xD0)); // top shine
        _gfx.setTextSize(1);
        _gfx.setTextColor(color(0x00, 0xFF, 0x88));
        _gfx.setCursor(bx + 6, by + 5);
        _gfx.writeText(buf);
    }

private:
    LinuxGFX& _gfx;
    int16_t   _cx, _cy, _r;
    float     _min, _max;
    char      _label[16];
    char      _unit[8];
    int16_t   _prevNeedleX, _prevNeedleY;
    int16_t   _prevTailX,   _prevTailY;

    void _drawPivot() {
        _gfx.fillCircle(_cx, _cy, 6, color(0xC0, 0xC8, 0xD8));
        _gfx.fillCircle(_cx, _cy, 4, color(0x10, 0x14, 0x22));
        _gfx.drawCircle(_cx, _cy, 6, color(0x80, 0x90, 0xA8));
    }

    inline uint16_t color(uint8_t r, uint8_t g, uint8_t b) {
        return LinuxGFX::color565(r, g, b);
    }
};