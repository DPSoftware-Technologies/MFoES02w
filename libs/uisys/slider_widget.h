#pragma once
#include "hwinterface/gt911.h"
#include "font.h"
#include <cstdint>
#include <functional>
#include <string>
#include <cstdio>
#include <cstring>

namespace uisys {

enum class SliderOrientation { HORIZONTAL, VERTICAL };

class SliderWidget {
public:
    using Callback = std::function<void(float value)>;

private:
    int x, y, w, h;
    SliderOrientation orientation;
    float value  = 0.0f;
    float minVal = 0.0f;
    float maxVal = 1.0f;
    bool  dragging = false;

    std::string label;
    Callback    onChange;
    Font        font;

    uint16_t colTrack  = 0x2104;
    uint16_t colFill   = 0x07E0;
    uint16_t colThumb  = 0xFFFF;
    uint16_t colBorder = 0x8C71;
    uint16_t colText   = 0xFFFF;

    static constexpr int THUMB_SIZE = 20;

    bool trackHitTest(int tx, int ty) const {
        return tx >= x && tx <= x+w && ty >= y && ty <= y+h;
    }

    void getThumbRect(int& ox, int& oy, int& ow, int& oh) const {
        if (orientation == SliderOrientation::HORIZONTAL) {
            ox = x + (int)(value * (w - THUMB_SIZE));
            oy = y + (h - THUMB_SIZE) / 2;
        } else {
            ox = x + (w - THUMB_SIZE) / 2;
            oy = y + (int)((1.0f - value) * (h - THUMB_SIZE));
        }
        ow = oh = THUMB_SIZE;
    }

    float pointToValue(int tx, int ty) const {
        float v;
        if (orientation == SliderOrientation::HORIZONTAL)
            v = (float)(tx - x - THUMB_SIZE/2) / (float)(w - THUMB_SIZE);
        else
            v = 1.0f - (float)(ty - y - THUMB_SIZE/2) / (float)(h - THUMB_SIZE);
        return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    }

    float mappedValue() const { return minVal + value * (maxVal - minVal); }

bool visible_widget = true;

public:
    SliderWidget() : x(0), y(0), w(200), h(40), orientation(SliderOrientation::HORIZONTAL) {}

    SliderWidget(int x, int y, int w, int h,
                 SliderOrientation ori, const std::string& label,
                 float minVal = 0.0f, float maxVal = 1.0f,
                 Callback cb = nullptr, Font font = Font::Medium())
        : x(x), y(y), w(w), h(h), orientation(ori),
          minVal(minVal), maxVal(maxVal), label(label), onChange(cb), font(font) {}

    void setValue(float v) {
        value = (v - minVal) / (maxVal - minVal);
        value = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    }

    float getValue()      const { return mappedValue(); }
    float getNormalized() const { return value; }

    void setFont(const Font& f)   { font = f; }
        void setVisible(bool v) { visible_widget = v; }
    bool isVisible()   const { return visible_widget; }

    void setCallback(Callback cb) { onChange = cb; }
    void setColors(uint16_t track, uint16_t fill, uint16_t thumb, uint16_t border, uint16_t text) {
        colTrack=track; colFill=fill; colThumb=thumb; colBorder=border; colText=text;
    }

    void handleEvent(const TouchEventData& e) {
        if (!visible_widget) return;
        switch (e.event) {
            case TouchEvent::PRESS:
                if (trackHitTest(e.point.x, e.point.y)) {
                    dragging = true;
                    value = pointToValue(e.point.x, e.point.y);
                    if (onChange) onChange(mappedValue());
                }
                break;
            case TouchEvent::MOVE:
                if (dragging) { value = pointToValue(e.point.x, e.point.y); if (onChange) onChange(mappedValue()); }
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
        gfx.fillRect(x, y, w, h, colTrack);
        gfx.drawRect(x, y, w, h, colBorder);

        if (orientation == SliderOrientation::HORIZONTAL) {
            int fillW = (int)(value * (w - THUMB_SIZE)) + THUMB_SIZE/2;
            if (fillW > 0) gfx.fillRect(x, y + h/2 - 3, fillW, 6, colFill);
        } else {
            int fillH = h - (int)((1.0f - value) * (h - THUMB_SIZE)) - THUMB_SIZE/2;
            if (fillH > 0) gfx.fillRect(x + w/2 - 3, y + h - fillH, 6, fillH, colFill);
        }

        int tx, ty, tw, th;
        getThumbRect(tx, ty, tw, th);
        gfx.fillRect(tx, ty, tw, th, dragging ? colFill : colThumb);
        gfx.drawRect(tx, ty, tw, th, colBorder);

        char valBuf[16];
        snprintf(valBuf, sizeof(valBuf), "%.1f", mappedValue());

        font.apply(gfx);
        gfx.setTextColor(colText, 0x0000);

        if (orientation == SliderOrientation::HORIZONTAL) {
            gfx.setCursor(x, y - font.charH() - 2);
            gfx.writeText(label.c_str());
            gfx.setCursor(x + w - font.textWidth((int)strlen(valBuf)), y - font.charH() - 2);
            gfx.setTextColor(colFill, 0x0000);
            gfx.writeText(valBuf);
        } else {
            gfx.setCursor(x + (w - font.textWidth((int)label.size())) / 2, y - font.charH() - 2);
            gfx.writeText(label.c_str());
            gfx.setCursor(x + (w - font.textWidth((int)strlen(valBuf))) / 2, y + h + 4);
            gfx.setTextColor(colFill, 0x0000);
            gfx.writeText(valBuf);
        }
    }
};

} // namespace uisys
