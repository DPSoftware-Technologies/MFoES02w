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

    uint32_t colBg   = 0xFF212021u;  // was 0x2104  →  rgb(33,32,33)
    uint32_t colArc  = 0xFF00FF00u;  // was 0x07E0  →  rgb(0,255,0)
    uint32_t colKnob = 0xFF424142u;  // was 0x4208  →  rgb(66,65,66)
    uint32_t colText = 0xFFFFFFFFu;  // was 0xFFFF  →  rgb(255,255,255)

    static constexpr float START_ANGLE = 225.0f;
    static constexpr float SWEEP       = 270.0f;
    static constexpr float PI          = 3.14159265f;

    float degToRad(float d) const;
    float valueToAngle(float v) const;
    float pointToValue(int tx, int ty) const;
    bool  hitTest(int tx, int ty) const;
    void  clampValue();
    float mappedValue() const;

    bool visible_widget = true;

public:
    DialWidget();
    DialWidget(int cx, int cy, int radius, const std::string& label,
               float minVal = 0.0f, float maxVal = 1.0f,
               Callback cb = nullptr, Font font = Font::Medium());

    void  setValue(float v);
    float getValue()      const;
    float getNormalized() const;

    void setFont(const Font& f);
    void setVisible(bool v);
    bool isVisible() const;
    void setCallback(Callback cb);
    void setColors(uint32_t bg, uint32_t arc, uint32_t knob, uint32_t text);

    void handleEvent(const TouchEventData& e);

    //  Draw — templated, must remain in header 

    template<typename GFX>
    void draw(GFX& gfx) const {
        if (!visible_widget) return;
        gfx.fillCircle(cx, cy, radius, colBg);
        gfx.drawCircle(cx, cy, radius, colText);

        int   steps  = 60;
        int   r      = radius - 6;
        float endAng = valueToAngle(value);

        for (int i = 0; i < steps; i++) {
            float a0      = degToRad(START_ANGLE + (SWEEP * i)       / steps);
            float a1      = degToRad(START_ANGLE + (SWEEP * (i+1))   / steps);
            float progAng = START_ANGLE + (SWEEP * i) / steps;
            uint32_t col  = (progAng <= endAng) ? colArc : 0xFF424142u;  // was 0x4208 dim grey

            int x0 = cx + (int)(r * std::cos(a0)), y0 = cy + (int)(r * std::sin(a0));
            int x1 = cx + (int)(r * std::cos(a1)), y1 = cy + (int)(r * std::sin(a1));
            gfx.drawLine(x0,   y0,   x1,   y1,   col);
            gfx.drawLine(x0+1, y0,   x1+1, y1,   col);
            gfx.drawLine(x0,   y0+1, x1,   y1+1, col);
        }

        float ang = degToRad(valueToAngle(value));
        int r1 = radius-14, r2 = radius-4;
        gfx.drawLine(cx+(int)(r1*std::cos(ang)), cy+(int)(r1*std::sin(ang)),
                     cx+(int)(r2*std::cos(ang)), cy+(int)(r2*std::sin(ang)), 0xFFFFFFFFu);
        gfx.drawLine(cx+(int)(r1*std::cos(ang))+1, cy+(int)(r1*std::sin(ang)),
                     cx+(int)(r2*std::cos(ang))+1, cy+(int)(r2*std::sin(ang)), 0xFFFFFFFFu);

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
