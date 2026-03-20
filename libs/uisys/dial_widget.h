#pragma once
#include "hwinterface/gt911.h"
#include "font.h"
#include <cstdint>
#include <functional>
#include <string>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace uisys {

class DialWidget {
public:
    using Callback = std::function<void(float value)>;

private:
    int   cx, cy, radius;
    float value    = 0.0f;
    float minVal   = 0.0f;
    float maxVal   = 1.0f;
    bool  dragging = false;

    std::string label;
    Callback    onChange;
    Font        font;

    uint16_t colBg   = 0x2104;
    uint16_t colArc  = 0x07E0;
    uint16_t colKnob = 0x4208;
    uint16_t colText = 0xFFFF;

    static constexpr float START_ANGLE = 225.0f;
    static constexpr float SWEEP       = 270.0f;
    static constexpr float PI          = 3.14159265f;

    float degToRad(float d) const { return d * PI / 180.0f; }
    float valueToAngle(float v) const { return START_ANGLE + v * SWEEP; }

    float pointToValue(int tx, int ty) const {
        float dx  = (float)(tx - cx);
        float dy  = (float)(ty - cy);
        float ang = std::atan2(dy, dx) * 180.0f / PI;
        if (ang < 0) ang += 360.0f;
        float rel = ang - START_ANGLE;
        if (rel < 0) rel += 360.0f;
        if (rel > SWEEP) rel = (rel - SWEEP < 360.0f - rel) ? SWEEP : 0.0f;
        return rel / SWEEP;
    }

    bool hitTest(int tx, int ty) const {
        float dx = (float)(tx - cx), dy = (float)(ty - cy);
        return std::sqrt(dx*dx + dy*dy) <= (float)radius;
    }

    void clampValue() { if (value < 0.0f) value = 0.0f; if (value > 1.0f) value = 1.0f; }
    float mappedValue() const { return minVal + value * (maxVal - minVal); }

bool visible_widget = true;

public:
    DialWidget() : cx(0), cy(0), radius(50) {}

    DialWidget(int cx, int cy, int radius, const std::string& label,
               float minVal = 0.0f, float maxVal = 1.0f,
               Callback cb = nullptr, Font font = Font::Medium())
        : cx(cx), cy(cy), radius(radius), minVal(minVal), maxVal(maxVal),
          label(label), onChange(cb), font(font) {}

    void setValue(float v) { value = (v - minVal) / (maxVal - minVal); clampValue(); }
    float getValue()      const { return mappedValue(); }
    float getNormalized() const { return value; }

    void setFont(const Font& f)   { font = f; }
        void setVisible(bool v) { visible_widget = v; }
    bool isVisible()   const { return visible_widget; }

    void setCallback(Callback cb) { onChange = cb; }
    void setColors(uint16_t bg, uint16_t arc, uint16_t knob, uint16_t text) {
        colBg=bg; colArc=arc; colKnob=knob; colText=text;
    }

    void handleEvent(const TouchEventData& e) {
        if (!visible_widget) return;
        switch (e.event) {
            case TouchEvent::PRESS:
                if (hitTest(e.point.x, e.point.y)) {
                    dragging = true;
                    value = pointToValue(e.point.x, e.point.y);
                    clampValue();
                    if (onChange) onChange(mappedValue());
                }
                break;
            case TouchEvent::MOVE:
                if (dragging) { value = pointToValue(e.point.x, e.point.y); clampValue(); if (onChange) onChange(mappedValue()); }
                break;
            case TouchEvent::RELEASE:
                dragging = false;
                break;
            default: break;
        }
    }

    template<typename GFX>
    void draw(GFX& gfx) const {
        if (!visible_widget) return;
        gfx.fillCircle(cx, cy, radius, colBg);
        gfx.drawCircle(cx, cy, radius, colText);

        int   steps  = 60;
        int   r      = radius - 6;
        float endAng = valueToAngle(value);

        for (int i = 0; i < steps; i++) {
            float a0       = degToRad(START_ANGLE + (SWEEP * i)     / steps);
            float a1       = degToRad(START_ANGLE + (SWEEP * (i+1)) / steps);
            float progAng  = START_ANGLE + (SWEEP * i) / steps;
            uint16_t col   = (progAng <= endAng) ? colArc : (uint16_t)0x4208;

            int x0 = cx + (int)(r * std::cos(a0)), y0 = cy + (int)(r * std::sin(a0));
            int x1 = cx + (int)(r * std::cos(a1)), y1 = cy + (int)(r * std::sin(a1));
            gfx.drawLine(x0,   y0,   x1,   y1,   col);
            gfx.drawLine(x0+1, y0,   x1+1, y1,   col);
            gfx.drawLine(x0,   y0+1, x1,   y1+1, col);
        }

        float ang = degToRad(valueToAngle(value));
        int r1 = radius-14, r2 = radius-4;
        gfx.drawLine(cx+(int)(r1*std::cos(ang)), cy+(int)(r1*std::sin(ang)),
                     cx+(int)(r2*std::cos(ang)), cy+(int)(r2*std::sin(ang)), 0xFFFF);
        gfx.drawLine(cx+(int)(r1*std::cos(ang))+1, cy+(int)(r1*std::sin(ang)),
                     cx+(int)(r2*std::cos(ang))+1, cy+(int)(r2*std::sin(ang)), 0xFFFF);

        gfx.fillCircle(cx, cy, 4, colKnob);

        char valBuf[16];
        snprintf(valBuf, sizeof(valBuf), "%.1f", mappedValue());

        font.apply(gfx);
        gfx.setTextColor(colText, colBg);

        // Value centered in dial
        gfx.setCursor(cx - font.textWidth((int)strlen(valBuf)) / 2, cy - font.charH() / 2);
        gfx.writeText(valBuf);

        // Label below dial
        gfx.setCursor(cx - font.textWidth((int)label.size()) / 2, cy + radius + 4);
        gfx.writeText(label.c_str());
    }
};

} // namespace uisys
