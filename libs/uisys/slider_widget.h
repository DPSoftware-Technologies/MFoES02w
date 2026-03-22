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
    float value    = 0.0f;
    float minVal   = 0.0f;
    float maxVal   = 1.0f;
    bool  dragging = false;

    std::string label;
    Callback    onChange;
    Font        font;

    uint32_t colTrack  = 0xFF212021u;  // was 0x2104  →  rgb(33,32,33)
    uint32_t colFill   = 0xFF00FF00u;  // was 0x07E0  →  rgb(0,255,0)
    uint32_t colThumb  = 0xFFFFFFFFu;  // was 0xFFFF  →  rgb(255,255,255)
    uint32_t colBorder = 0xFF8C8E8Cu;  // was 0x8C71  →  rgb(140,142,140)
    uint32_t colText   = 0xFFFFFFFFu;  // was 0xFFFF  →  rgb(255,255,255)

    static constexpr int THUMB_SIZE = 20;

    bool  trackHitTest(int tx, int ty) const;
    void  getThumbRect(int& ox, int& oy, int& ow, int& oh) const;
    float pointToValue(int tx, int ty) const;
    float mappedValue() const;

    bool visible_widget = true;

public:
    SliderWidget();
    SliderWidget(int x, int y, int w, int h,
                 SliderOrientation ori, const std::string& label,
                 float minVal = 0.0f, float maxVal = 1.0f,
                 Callback cb = nullptr, Font font = Font::Medium());

    void  setValue(float v);
    float getValue()      const;
    float getNormalized() const;

    void setFont(const Font& f);
    void setVisible(bool v);
    bool isVisible() const;
    void setCallback(Callback cb);
    void setColors(uint32_t track, uint32_t fill, uint32_t thumb,
                   uint32_t border, uint32_t text);

    void handleEvent(const TouchEventData& e);

    //  Draw — templated, must remain in header 

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
        gfx.setTextColor(colText, GFX_TRANSPARENT);

        if (orientation == SliderOrientation::HORIZONTAL) {
            gfx.setCursor(x, y - font.charH() - 2);
            gfx.writeText(label.c_str());
            gfx.setCursor(x + w - font.textWidth((int)strlen(valBuf)), y - font.charH() - 2);
            gfx.setTextColor(colFill, GFX_TRANSPARENT);
            gfx.writeText(valBuf);
        } else {
            gfx.setCursor(x + (w - font.textWidth((int)label.size())) / 2, y - font.charH() - 2);
            gfx.writeText(label.c_str());
            gfx.setCursor(x + (w - font.textWidth((int)strlen(valBuf))) / 2, y + h + 4);
            gfx.setTextColor(colFill, GFX_TRANSPARENT);
            gfx.writeText(valBuf);
        }
    }
};

} // namespace uisys
